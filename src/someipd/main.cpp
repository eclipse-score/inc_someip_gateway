/********************************************************************************
 * Copyright (c) 2025 Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/

#include <array>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string_view>
#include <thread>
#include <vsomeip/defines.hpp>
#include <vsomeip/primitive_types.hpp>
#include <vsomeip/vsomeip.hpp>

#include "score/containers/non_relocatable_vector.h"
#include "score/mw/com/runtime.h"
#include "score/span.hpp"
#include "src/network_service/interfaces/message_transfer.h"

static const char* someipd_name = "someipd";

// SOME/IP test service constants.
static const vsomeip::service_t kServiceId = 0x1234;
static const vsomeip::instance_t kInstanceId = 0x5678;
static const vsomeip::event_t kEventId = 0x8778;
static const vsomeip::eventgroup_t kEventgroupId = 0x4465;

// TC8 standalone constants — must match tests/tc8_conformance/config/tc8_someipd_sd.json.
static const vsomeip::event_t kTc8EventId = 0x0777;
static const vsomeip::eventgroup_t kTc8EventgroupId = 0x4455;
static const vsomeip::eventgroup_t kTc8MulticastEventgroupId = 0x4465;  // TC8-SD-013 / TC8-EVT-005
static const vsomeip::event_t kTc8ReliableEventId = 0x0778;          // TCP-only event for TC8-RPC-17
static const vsomeip::eventgroup_t kTc8ReliableEventgroupId = 0x4475; // TCP-only eventgroup
static const vsomeip::event_t kStaticFieldEventId = 0x0779;          // Static field for TC8-RPC-16
static const vsomeip::eventgroup_t kStaticFieldEventgroupId = 0x4480; // Eventgroup for kStaticFieldEventId
static const vsomeip::method_t kTc8MethodId = 0x0421;
static const vsomeip::method_t kTc8GetFieldMethodId = 0x0001;  // TC8-FLD-003
static const vsomeip::method_t kTc8SetFieldMethodId = 0x0002;  // TC8-FLD-004

// Remote service constants.
static const vsomeip::service_t kRemoteServiceId = 0x4321;

static const std::size_t kMaxSampleCount = 10;

using SomeipMessageTransferProxy = score::someip_gateway::network_service::interfaces::
    message_transfer::SomeipMessageTransferProxy;
using SomeipMessageTransferSkeleton = score::someip_gateway::network_service::interfaces::
    message_transfer::SomeipMessageTransferSkeleton;

/// Shutdown flag, set by the signal handler.
static std::atomic<bool> shutdown_requested{
    false};  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void termination_handler(int /*signal*/) {
    std::cout << "Received termination signal. Initiating graceful shutdown..." << std::endl;
    shutdown_requested.store(true);
}

// ---------------------------------------------------------------------------
// CLI parsing
// ---------------------------------------------------------------------------

/// Parsed command-line arguments.
struct CliArgs {
    bool standalone{false};
    score::containers::NonRelocatableVector<const char*> lola_argv;
};

/// Extract --tc8-standalone from argv; return filtered args for LoLa runtime.
static CliArgs parse_args(int argc, const char* argv[]) {
    CliArgs result{false, score::containers::NonRelocatableVector<const char*>(
                              static_cast<std::size_t>(argc))};
    for (int i = 0; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--tc8-standalone") {
            result.standalone = true;
        } else {
            result.lola_argv.emplace_back(argv[i]);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// TC8 standalone mode — message handler helpers
// ---------------------------------------------------------------------------

/// Returns true if the request is fire-and-forget (MT_REQUEST_NO_RETURN).
/// No response must be sent for such messages — TC8-MSG-002.
// REQ: comp_req__tc8_conformance__msg_resp_header
static bool IsTc8FireAndForget(const std::shared_ptr<vsomeip::message>& request) {
    return request->get_message_type() == vsomeip::message_type_e::MT_REQUEST_NO_RETURN;
}

/// Build an echo response payload by copying the request payload — TC8-MSG request/response.
static std::shared_ptr<vsomeip::payload> MakeTc8EchoPayload(
    const std::shared_ptr<vsomeip::message>& request) {
    return request->get_payload();
}

static constexpr std::size_t kMaxFieldDataBytes = 64U;

/// Fixed-size buffer for TC8 field values. Avoids heap allocation for small payloads.
/// TC8 field payloads are always <= kMaxFieldDataBytes bytes.
struct FieldBuffer {
    std::array<vsomeip::byte_t, kMaxFieldDataBytes> data{};
    std::size_t size{0U};
};

/// Build a GET-field response payload from the current field state — TC8-FLD-003.
// REQ: comp_req__tc8_conformance__fld_get_set
static std::shared_ptr<vsomeip::payload> MakeTc8GetFieldPayload(
    const FieldBuffer& field_buf) {
    auto resp_payload = vsomeip::runtime::get()->create_payload();
    resp_payload->set_data(field_buf.data.data(),
                           static_cast<vsomeip::length_t>(field_buf.size));
    return resp_payload;
}

/// Handle SET-field: update field state in-place, return new payload for notification.
/// Returns a notify payload to broadcast, which is always non-null on SET — TC8-FLD-004.
// REQ: comp_req__tc8_conformance__fld_get_set
static std::shared_ptr<vsomeip::payload> HandleTc8SetField(
    const std::shared_ptr<vsomeip::message>& request,
    FieldBuffer& field_buf) {
    auto req_payload = request->get_payload();
    const vsomeip::length_t req_len = req_payload->get_length();
    if (req_len > kMaxFieldDataBytes) {
        // Payload exceeds buffer capacity; return empty payload and skip notify.
        // TC8 payloads are always <= kMaxFieldDataBytes — this is a safety guard.
        return vsomeip::runtime::get()->create_payload();
    }
    field_buf.size = req_len;
    std::memcpy(field_buf.data.data(), req_payload->get_data(), req_len);
    auto notify_payload = vsomeip::runtime::get()->create_payload();
    notify_payload->set_data(req_payload->get_data(), req_len);
    return notify_payload;
}

// ---------------------------------------------------------------------------
// TC8 standalone mode (no IPC, no gatewayd)
// ---------------------------------------------------------------------------

/// Offer both UDP and TCP TC8 test events on the test service.
// REQ: comp_req__tc8_conformance__sd_offer_format
// REQ: comp_req__tc8_conformance__sd_sub_lifecycle
// REQ: comp_req__tc8_conformance__evt_subscription
// REQ: comp_req__tc8_conformance__tcp_transport
static void SetupTc8Events(std::shared_ptr<vsomeip::application> app) {
    // Both unicast (0x4455) and multicast (0x4465) eventgroups — TC8-SD-013 / TC8-EVT-005.
    std::set<vsomeip::eventgroup_t> groups{kTc8EventgroupId, kTc8MulticastEventgroupId};
    // REQ: comp_req__tc8_conformance__fld_initial_value
    // ET_FIELD: vsomeip sends the cached payload to every new subscriber on subscribe ACK,
    // without waiting for the next notify() cycle (TC8-FLD-001 / TC8-FLD-002).
    app->offer_event(kServiceId, kInstanceId, kTc8EventId, groups,
                     vsomeip::event_type_e::ET_FIELD);
    // TCP-only event — RT_RELIABLE so vsomeip accepts TCP subscriptions.
    std::set<vsomeip::eventgroup_t> reliable_groups{kTc8ReliableEventgroupId};
    app->offer_event(kServiceId, kInstanceId, kTc8ReliableEventId, reliable_groups,
                     vsomeip::event_type_e::ET_EVENT, std::chrono::milliseconds::zero(),
                     false, true, nullptr, vsomeip::reliability_type_e::RT_RELIABLE);
    // REQ: comp_req__tc8_conformance__evt_subscription
    // Static field event — 60 000 ms update-cycle (SOMEIPSRV_RPC_16): vsomeip delivers the
    // cached initial value to each new subscriber; no cyclic notify() calls are made.
    std::set<vsomeip::eventgroup_t> static_field_groups{kStaticFieldEventgroupId};
    app->offer_event(kServiceId, kInstanceId, kStaticFieldEventId, static_field_groups,
                     vsomeip::event_type_e::ET_FIELD);
}

/// Periodically broadcast current field value until shutdown_requested.
/// Supports TC8-SD-008 (StopSubscribe cycling) and TC8-FLD-001 (initial value delivery).
// REQ: comp_req__tc8_conformance__fld_initial_value
static void NotifyFieldLoop(std::shared_ptr<vsomeip::application> app,
                            std::shared_ptr<std::mutex> field_mutex,
                            std::shared_ptr<FieldBuffer> field_buf) {
    auto payload = vsomeip::runtime::get()->create_payload();
    while (!shutdown_requested.load()) {
        {
            std::lock_guard<std::mutex> lock(*field_mutex);
            payload->set_data(field_buf->data.data(),
                              static_cast<vsomeip::length_t>(field_buf->size));
        }
        // force=true: send even when ET_FIELD value is unchanged (cyclic notification for RPC_15).
        app->notify(kServiceId, kInstanceId, kTc8EventId, payload, true);
        app->notify(kServiceId, kInstanceId, kTc8ReliableEventId, payload, true);
        std::this_thread::sleep_for(std::chrono::milliseconds(500));  // Match update-cycle: 500ms
    }
}

/// Seed the kStaticFieldEventId cache so vsomeip delivers an initial-value notification
/// to new subscribers (SOMEIPSRV_RPC_16). No further notify() calls are made for this
/// event — the 60 000 ms update-cycle guarantees silence within the test observation window.
// REQ: comp_req__tc8_conformance__evt_subscription
static void SeedStaticFieldCache(std::shared_ptr<vsomeip::application> app) {
    auto static_payload = vsomeip::runtime::get()->create_payload();
    static constexpr vsomeip::byte_t kStaticFieldData[] = {0xBE, 0xEF};
    static_payload->set_data(kStaticFieldData,
                             static_cast<vsomeip::length_t>(sizeof(kStaticFieldData)));
    app->notify(kServiceId, kInstanceId, kStaticFieldEventId, static_payload);
}

/// Offer the test service and block until shutdown. Used by --tc8-standalone.
static void run_standalone_mode(std::shared_ptr<vsomeip::application> app) {
    SetupTc8Events(app);

    // REQ: comp_req__tc8_conformance__fld_initial_value
    // REQ: comp_req__tc8_conformance__fld_get_set
    // Field value shared between notify loop and message handler — protected by mutex.
    auto field_mutex = std::make_shared<std::mutex>();
    auto field_buf = std::make_shared<FieldBuffer>();
    field_buf->data[0] = 0xDE;
    field_buf->data[1] = 0xAD;
    field_buf->size = 2U;

    // REQ: comp_req__tc8_conformance__msg_resp_header
    // REQ: comp_req__tc8_conformance__msg_error_codes
    app->register_message_handler(
        kServiceId, kInstanceId, vsomeip::ANY_METHOD,
        [app, field_mutex, field_buf](const std::shared_ptr<vsomeip::message>& request) {
            if (IsTc8FireAndForget(request)) {
                return;
            }
            auto response = vsomeip::runtime::get()->create_response(request);
            const vsomeip::method_t method = request->get_method();
            if (method == kTc8MethodId) {
                response->set_payload(MakeTc8EchoPayload(request));
            } else if (method == kTc8GetFieldMethodId) {
                std::lock_guard<std::mutex> lock(*field_mutex);
                response->set_payload(MakeTc8GetFieldPayload(*field_buf));
            } else if (method == kTc8SetFieldMethodId) {
                std::lock_guard<std::mutex> lock(*field_mutex);
                std::shared_ptr<vsomeip::payload> notify_payload =
                    HandleTc8SetField(request, *field_buf);
                app->notify(kServiceId, kInstanceId, kTc8EventId, notify_payload);
                app->notify(kServiceId, kInstanceId, kTc8ReliableEventId, notify_payload);
                response->set_return_code(vsomeip::return_code_e::E_OK);
            } else {
                response->set_return_code(vsomeip::return_code_e::E_UNKNOWN_METHOD);
            }
            app->send(response);
        });

    app->offer_service(kServiceId, kInstanceId);
    SeedStaticFieldCache(app);
    std::cout << "someipd [TC8 standalone]: offering service 0x" << std::hex << kServiceId
              << "/0x" << kInstanceId << std::dec
              << " — no IPC proxy (gatewayd not required)" << std::endl;

    NotifyFieldLoop(app, field_mutex, field_buf);

    std::cout << "someipd [TC8 standalone]: shutting down." << std::endl;
    app->stop();
}

// ---------------------------------------------------------------------------
// Normal IPC mode (requires gatewayd)
// ---------------------------------------------------------------------------

/// Create and subscribe the IPC proxy to gatewayd. Blocks until gatewayd offers the service.
static SomeipMessageTransferProxy create_ipc_proxy() {
    auto handles =
        SomeipMessageTransferProxy::FindService(
            score::mw::com::InstanceSpecifier::Create(std::string{"someipd/gatewayd_messages"}).value())
            .value();
    auto proxy = SomeipMessageTransferProxy::Create(handles.front()).value();
    proxy.message_.Subscribe(kMaxSampleCount);
    return proxy;
}

/// Create the IPC skeleton and offer it to gatewayd.
static SomeipMessageTransferSkeleton create_ipc_skeleton() {
    auto result = SomeipMessageTransferSkeleton::Create(
        score::mw::com::InstanceSpecifier::Create(std::string{"someipd/someipd_messages"}).value());
    auto skeleton = std::move(result).value();
    (void)skeleton.OfferService();
    return skeleton;
}

/// Forward incoming SOME/IP messages from the remote service to gatewayd via IPC.
static void register_network_to_ipc_handler(std::shared_ptr<vsomeip::application> app,
                                            SomeipMessageTransferSkeleton& skeleton) {
    app->register_message_handler(
        kRemoteServiceId, kInstanceId, kEventId,
        [&skeleton](const std::shared_ptr<vsomeip::message>& msg) {
            auto maybe_message = skeleton.message_.Allocate();
            if (!maybe_message.has_value()) {
                std::cerr << "Failed to allocate SOME/IP message: "
                          << maybe_message.error().Message() << std::endl;
                return;
            }
            auto sample = std::move(maybe_message).value();
            memcpy(sample->data + VSOMEIP_FULL_HEADER_SIZE, msg->get_payload()->get_data(),
                   msg->get_payload()->get_length());
            sample->size = msg->get_payload()->get_length() + VSOMEIP_FULL_HEADER_SIZE;
            skeleton.message_.Send(std::move(sample));
        });
}

/// Subscribe to the remote service and offer the local service.
static void setup_someip_services(std::shared_ptr<vsomeip::application> app) {
    app->request_service(kRemoteServiceId, kInstanceId);

    std::set<vsomeip::eventgroup_t> groups{kEventgroupId};
    app->request_event(kRemoteServiceId, kInstanceId, kEventId, groups,
                       vsomeip::event_type_e::ET_EVENT);
    app->subscribe(kRemoteServiceId, kInstanceId, kEventgroupId);

    app->offer_event(kServiceId, kInstanceId, kEventId, groups);
    app->offer_service(kServiceId, kInstanceId);
}

/// Read IPC samples and forward them as SOME/IP notifications.
static void forward_ipc_to_someip(SomeipMessageTransferProxy& proxy,
                                  std::shared_ptr<vsomeip::application> app,
                                  std::shared_ptr<vsomeip::payload> payload) {
    proxy.message_.GetNewSamples(
        [&](auto message_sample) {
            score::cpp::span<const std::byte> message(message_sample->data, message_sample->size);
            if (message.size() < VSOMEIP_FULL_HEADER_SIZE) {
                std::cerr << "Received too small sample (size: " << message.size()
                          << ", expected at least: " << VSOMEIP_FULL_HEADER_SIZE << "). Skipping."
                          << std::endl;
                return;
            }
            auto payload_data = message.subspan(VSOMEIP_FULL_HEADER_SIZE);
            payload->set_data(reinterpret_cast<const vsomeip_v3::byte_t*>(payload_data.data()),
                              payload_data.size());
            app->notify(kServiceId, kInstanceId, kEventId, payload);
        },
        kMaxSampleCount);
}

/// Poll IPC and forward to SOME/IP until shutdown.
static void poll_until_shutdown(SomeipMessageTransferProxy& proxy,
                                std::shared_ptr<vsomeip::application> app) {
    auto payload = vsomeip::runtime::get()->create_payload();
    while (!shutdown_requested.load()) {
        forward_ipc_to_someip(proxy, app, payload);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

/// Full IPC-bridging mode: connect to gatewayd and bridge SOME/IP traffic.
static void run_ipc_mode(std::shared_ptr<vsomeip::application> app) {
    auto proxy = create_ipc_proxy();
    auto skeleton = create_ipc_skeleton();

    register_network_to_ipc_handler(app, skeleton);
    setup_someip_services(app);

    std::cout << "SOME/IP daemon started, waiting for messages..." << std::endl;
    poll_until_shutdown(proxy, app);
    std::cout << "Shutting down SOME/IP daemon..." << std::endl;

    app->stop();
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int argc, const char* argv[]) {
    std::signal(SIGTERM, termination_handler);
    std::signal(SIGINT, termination_handler);

    // Strip --tc8-standalone before passing argv to LoLa runtime.
    auto parsed = parse_args(argc, argv);
    int lola_argc = static_cast<int>(parsed.lola_argv.size());
    score::mw::com::runtime::InitializeRuntime(lola_argc, parsed.lola_argv.data());

    auto vsomeip_runtime = vsomeip::runtime::get();
    auto application = vsomeip_runtime->create_application(someipd_name);
    if (!application->init()) {
        std::cerr << "SOME/IP application init failed" << std::endl;
        return EXIT_FAILURE;
    }

    // Work runs in a detached thread; main thread blocks in start() (io_context).
    if (parsed.standalone) {
        std::thread(run_standalone_mode, application).detach();
    } else {
        std::thread(run_ipc_mode, application).detach();
    }

    application->start();
    return EXIT_SUCCESS;
}
