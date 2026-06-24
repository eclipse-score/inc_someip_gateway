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

#include <getopt.h>

#include <array>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vsomeip/vsomeip.hpp>

#include "routing.h"
#include "score/filesystem/path.h"
#include "score/mw/com/runtime.h"
#include "score/mw/log/logging.h"
#include "src/common/constants.h"
#include "src/config/mw_someip_config_generated.h"
#include "src/network_service/interfaces/message_transfer.h"

static const char* someipd_name = "someipd";

using score::someip_gateway::network_service::interfaces::message_transfer::
    SomeipMessageTransferProxy;
using score::someip_gateway::network_service::interfaces::message_transfer::
    SomeipMessageTransferSkeleton;

// Global flag to control application shutdown
static std::atomic<bool> shutdown_requested{false};

void termination_handler(int /*signal*/) {
    score::mw::log::LogWarn() << "Received termination signal. Initiating graceful shutdown...";
    shutdown_requested.store(true);
}

// ===========================================================================
// TC8 standalone mode constants and helpers
// ===========================================================================

// SOME/IP test service constants (shared with normal mode via constants.h for service/instance).
static const vsomeip::service_t kServiceId = 0x1234;
static const vsomeip::instance_t kInstanceId = 0x5678;

// TC8 standalone constants — must match tests/tc8_conformance/config/tc8_someipd_sd.json.
static const vsomeip::event_t kTc8EventId = 0x0777;
static const vsomeip::eventgroup_t kTc8EventgroupId = 0x4455;
static const vsomeip::eventgroup_t kTc8MulticastEventgroupId = 0x4465;   // TC8-SD-013 / TC8-EVT-005
static const vsomeip::event_t kTc8ReliableEventId = 0x0778;             // TCP-only event for TC8-RPC-17
static const vsomeip::eventgroup_t kTc8ReliableEventgroupId = 0x4475;   // TCP-only eventgroup
static const vsomeip::event_t kStaticFieldEventId = 0x0779;             // Static field for TC8-RPC-16
static const vsomeip::eventgroup_t kStaticFieldEventgroupId = 0x4480;   // Eventgroup for kStaticFieldEventId
static const vsomeip::method_t kTc8MethodId = 0x0421;
static const vsomeip::method_t kTc8GetFieldMethodId = 0x0001;           // TC8-FLD-003
static const vsomeip::method_t kTc8SetFieldMethodId = 0x0002;           // TC8-FLD-004

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
static std::shared_ptr<vsomeip::payload> MakeTc8GetFieldPayload(const FieldBuffer& field_buf) {
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
    score::mw::log::LogInfo() << "someipd [TC8 standalone]: offering service 0x"
                              << score::mw::log::LogHex16{kServiceId} << "/0x"
                              << score::mw::log::LogHex16{kInstanceId}
                              << " - no IPC proxy (gatewayd not required)";

    NotifyFieldLoop(app, field_mutex, field_buf);

    score::mw::log::LogInfo() << "someipd [TC8 standalone]: shutting down.";
    app->stop();
}

// ===========================================================================
// Normal IPC mode (requires gatewayd + flatbuffer config)
// ===========================================================================

// Help text, showing usage syntax and available options
void print_help() {
    std::cout << "Syntax: someipd -h/--help\n"
              << "        someipd -c/--configuration <config.bin> "
              << "-s/--service_instance_manifest <manifest.json>\n"
              << "        someipd -t/--tc8-standalone "
              << "-s/--service_instance_manifest <manifest.json>\n"
              << "\n";

    std::cout << "Options:\n"
              << " -h/--help Displays this help\n"
              << " -c/--configuration Specifies the configuration file\n"
              << " -s/--service_instance_manifest Specifies the service instance manifest file\n"
              << " -t/--tc8-standalone Run in TC8 conformance test mode (no IPC, no gatewayd)\n"
              << "\n";
}

int main(int argc, char* argv[]) {
    // Register signal handlers for graceful shutdown
    std::signal(SIGTERM, termination_handler);
    std::signal(SIGINT, termination_handler);

    const char* const short_opts = "htc:s:";
    const option long_opts[] = {{"help", no_argument, nullptr, 'h'},
                                {"tc8-standalone", no_argument, nullptr, 't'},
                                {"configuration", required_argument, nullptr, 'c'},
                                {"service_instance_manifest", required_argument, nullptr, 's'},
                                {nullptr, no_argument, nullptr, 0}};

    score::filesystem::Path service_instance_manifest_path{};
    score::filesystem::Path configuration_path{};
    bool tc8_standalone = false;

    while (true) {
        const int opt{getopt_long(argc, argv, short_opts, long_opts, nullptr)};
        if (opt == -1) {
            // No more options
            break;
        }
        switch (static_cast<char>(opt)) {
            case 'h': {
                print_help();
                return 0;
            }
            case 't': {
                tc8_standalone = true;
                break;
            }
            case 'c': {
                configuration_path = score::filesystem::Path{optarg};
                break;
            }
            case 's': {
                service_instance_manifest_path = score::filesystem::Path{optarg};
                break;
            }
            // Unknown option
            default: {
                print_help();
                return 1;
            }
        }
    }

    // ---------------------------------------------------------------------------
    // TC8 standalone mode — no config file, no IPC, no gatewayd
    // ---------------------------------------------------------------------------
    if (tc8_standalone) {
        // TC8 standalone does not use LoLa IPC — skip InitializeRuntime and config parsing.
        auto runtime = vsomeip::runtime::get();
        auto application = runtime->create_application(someipd_name);
        if (!application->init()) {
            score::mw::log::LogFatal() << "SOME/IP application init failed";
            return 1;
        }

        // Work runs in a detached thread; main thread blocks in start() (io_context).
        std::thread(run_standalone_mode, application).detach();
        application->start();
        return 0;
    }

    // ---------------------------------------------------------------------------
    // Normal IPC-bridging mode — requires config file + manifest
    // ---------------------------------------------------------------------------

    // Both configurations are required, otherwise print help and exit
    if (configuration_path.Empty() || service_instance_manifest_path.Empty()) {
        print_help();
        return 1;
    }

    // Read config data
    // TODO: Use memory mapped file instead of copying into buffer
    std::ifstream config_file;
    config_file.open(configuration_path.CStr(), std::ios::binary | std::ios::in);

    if (!config_file.is_open()) {
        score::mw::log::LogFatal() << "Error: Could not open config file " << configuration_path;
        return 1;
    }

    config_file.seekg(0, std::ios::end);
    std::streampos length = config_file.tellg();

    if (length <= 0) {
        score::mw::log::LogFatal() << "Error: Invalid config file size: " << static_cast<std::size_t>(length);
        config_file.close();
        return 1;
    }

    config_file.seekg(0, std::ios::beg);
    auto config_buffer = std::shared_ptr<char>(new char[length]);
    config_file.read(config_buffer.get(), length);
    config_file.close();

    auto config = std::shared_ptr<const score::mw_someip_config::Root>(
        config_buffer, score::mw_someip_config::GetRoot(config_buffer.get()));

    score::mw::com::runtime::InitializeRuntime(
        score::mw::com::runtime::RuntimeConfiguration{service_instance_manifest_path});

    auto handles =
        SomeipMessageTransferProxy::FindService(
            score::mw::com::InstanceSpecifier::Create(std::string("someipd/gatewayd_messages"))
                .value())
            .value();

    // Proxy for receiving messages from gatewayd to be sent via SOME/IP
    auto proxy = SomeipMessageTransferProxy::Create(handles.front()).value();
    proxy.message_.Subscribe(score::someip::max_sample_count);

    // Skeleton for transmitting messages from the network to gatewayd
    // TODO: Error handling for instance specifier creation
    auto create_result = SomeipMessageTransferSkeleton::Create(
        score::mw::com::InstanceSpecifier::Create(std::string("someipd/someipd_messages")).value());

    auto skeleton = std::move(create_result).value();

    // TODO: Error handling
    (void)skeleton.OfferService();

    auto routing = score::someipd::Routing::Create(config, std::move(proxy), std::move(skeleton));
    if (!routing.has_value()) {
        score::mw::log::LogFatal() << "[someipd] Network stack initialization failed";
        return 1;
    }

    score::mw::log::LogInfo() << "[someipd] Starting routing loop...";
    routing.value().Run(shutdown_requested);

    score::mw::log::LogInfo() << "[someipd] Shutting down SOME/IP daemon...";
}
