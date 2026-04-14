/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
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

#include "routing.h"

#include <cstring>
#include <iostream>
#include <thread>

#include "score/mw/log/logging.h"
#include "src/common/constants.h"

namespace score::someipd {

using score::someip::kAnyInstance;
using score::someip::kSomeipFullHeaderSize;
using score::someip::max_sample_count;

Routing::Routing(std::shared_ptr<const score::mw_someip_config::Root> config,
                 SomeipMessageTransferProxy ipc_proxy, SomeipMessageTransferSkeleton ipc_skeleton)
    : config_(config), ipc_proxy_(std::move(ipc_proxy)), ipc_skeleton_(std::move(ipc_skeleton)) {}

bool Routing::Init() {
    auto runtime = vsomeip::runtime::get();
    application_ = runtime->create_application("someipd");
    if (!application_->init()) {
        std::cerr << "[someipd] vsomeip application initialization failed" << std::endl;
        return false;
    }
    payload_ = runtime->create_payload();
    std::cout << "[someipd] vsomeip application initialized successfully" << std::endl;
    return true;
}

void Routing::SetupSubscriptions() {
    for (const auto service_type : *config_->service_types()) {
        auto service_instances = service_type->remote_service_instances();
        if (service_instances && service_instances->size() > 0) {
            for (const auto remote_service_instance : *service_instances) {
                application_->request_service(service_type->service_id(),
                                              remote_service_instance->instance_id());

                // Register a message handler for each event
                for (const auto event : *service_type->events()) {
                    application_->register_message_handler(
                        service_type->service_id(), remote_service_instance->instance_id(),
                        event->event_id(), [this](const std::shared_ptr<vsomeip::message>& msg) {
                            std::cout << "[someipd] Received SOME/IP event: service=0x" << std::hex
                                      << msg->get_service() << " event=0x" << msg->get_method()
                                      << std::dec << " payload=" << msg->get_payload()->get_length()
                                      << "B" << std::endl;
                            auto maybe_message = ipc_skeleton_.message_.Allocate();
                            if (!maybe_message.has_value()) {
                                std::cerr << "[someipd] Failed to allocate IPC message: "
                                          << maybe_message.error().Message() << std::endl;
                                return;
                            }
                            auto message_sample = std::move(maybe_message).value();

                            // TODO: For now the gatewayd requires the event ID to be sent as well.
                            // This is obsolete when SOCOM is used and there are separate channels
                            // for each event.
                            {
                                const auto svc = msg->get_service();
                                message_sample->data[0] = static_cast<std::byte>(svc >> 8);
                                message_sample->data[1] = static_cast<std::byte>(svc & 0xFF);
                                const auto event_id = msg->get_method();
                                message_sample->data[2] = static_cast<std::byte>(event_id >> 8);
                                message_sample->data[3] = static_cast<std::byte>(event_id & 0xFF);
                            }

                            memcpy(message_sample->data + kSomeipFullHeaderSize,
                                   msg->get_payload()->get_data(),
                                   msg->get_payload()->get_length());
                            message_sample->size =
                                msg->get_payload()->get_length() + kSomeipFullHeaderSize;

                            ipc_skeleton_.message_.Send(std::move(message_sample));
                        });

                    // TODO: Do Eventgroup handling. Currently just create one group for each event
                    // with the same ID as the event
                    std::set<vsomeip::eventgroup_t> groups{event->event_id()};
                    application_->request_event(service_type->service_id(),
                                                remote_service_instance->instance_id(),
                                                event->event_id(), groups);

                    application_->subscribe(service_type->service_id(),
                                            remote_service_instance->instance_id(),
                                            event->event_id());
                }
            }
        }
    }
}

void Routing::SetupOfferings() {
    for (const auto service_type : *config_->service_types()) {
        auto service_instances = service_type->local_service_instances();
        if (service_instances && service_instances->size() > 0) {
            for (const auto local_service_instance : *service_type->local_service_instances()) {
                for (const auto event : *service_type->events()) {
                    // TODO: Do Eventgroup handling. Currently just create one group for each event
                    // with the same ID as the event
                    std::set<vsomeip::eventgroup_t> groups{event->event_id()};
                    application_->offer_event(service_type->service_id(),
                                              local_service_instance->instance_id(),
                                              event->event_id(), groups);
                }
                std::cout << "[someipd] Offering service 0x" << std::hex
                          << service_type->service_id() << " instance 0x"
                          << local_service_instance->instance_id() << std::dec << std::endl;
                application_->offer_service(service_type->service_id(),
                                            local_service_instance->instance_id());
            }
        }
    }
}

InstanceId Routing::LookupInstanceId(ServiceId service_id) const {
    for (const auto service_type : *config_->service_types()) {
        if (service_type->service_id() != service_id) {
            continue;
        }
        return service_type->local_service_instances()->Get(0)->instance_id();
    }
    return kAnyInstance;
}

// exchange event data
void Routing::ProcessMessages(std::atomic<bool>& shutdown_requested) {
    while (!shutdown_requested.load()) {
        // TODO: Use ReceiveHandler + async runtime instead of polling
        ipc_proxy_.message_.GetNewSamples(
            [this](auto message_sample) {
                // TODO: Check if size is larger than capacity of data
                score::cpp::span<const std::byte> message(message_sample->data,
                                                          message_sample->size);

                // Check if sample size is valid and contains at least a SOME/IP header
                if (message.size() < kSomeipFullHeaderSize) {
                    std::cerr << "[someipd] IPC message too small (size=" << message.size()
                              << ", expected >=" << kSomeipFullHeaderSize << "), dropping"
                              << std::endl;
                    return;
                }

                // Read service_id and event_id from the SOME/IP header (big-endian)
                const auto service_id =
                    static_cast<ServiceId>((std::to_integer<uint16_t>(message[0]) << 8) |
                                           std::to_integer<uint16_t>(message[1]));
                const auto event_id =
                    static_cast<EventId>((std::to_integer<uint16_t>(message[2]) << 8) |
                                         std::to_integer<uint16_t>(message[3]));

                // TODO: Finding out the instance should get obsolete when using SOCOM.
                // For now, just use the first instance of the service.
                const InstanceId instance_id = LookupInstanceId(service_id);

                auto payload_data = message.subspan(kSomeipFullHeaderSize);

                payload_->set_data(reinterpret_cast<const vsomeip_v3::byte_t*>(payload_data.data()),
                                   payload_data.size());
                std::cout << "[someipd] Forwarding IPC message to SOME/IP: service=0x" << std::hex
                          << service_id << " instance=0x" << instance_id << " event=0x" << event_id
                          << std::dec << " payload=" << payload_data.size() << "B" << std::endl;
                application_->notify(service_id, instance_id, event_id, payload_);
            },
            max_sample_count);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::cout << "[someipd] Message loop exited, shutting down..." << std::endl;
}

void Routing::Run(std::atomic<bool>& shutdown_requested) {
    // Register state handler before start() so offers/subscriptions are made
    // only once the vsomeip routing daemon has accepted the application
    // (ST_REGISTERED).
    application_->register_state_handler([this](vsomeip::state_type_e state) {
        if (state == vsomeip::state_type_e::ST_REGISTERED) {
            std::cout << "[someipd] Application registered with routing daemon." << std::endl;
            std::cout << "[someipd] Setting up subscriptions..." << std::endl;
            SetupSubscriptions();
            std::cout << "[someipd] Setting up offerings..." << std::endl;
            SetupOfferings();
        }
    });
    std::cout << "[someipd] Starting network stack processing..." << std::endl;
    processing_thread_ = std::thread([this]() { application_->start(); });
    std::cout << "[someipd] Network stack started, entering message loop." << std::endl;
    ProcessMessages(shutdown_requested);
    std::cout << "[someipd] Stopping network stack processing..." << std::endl;
    if (application_) {
        application_->stop();
    }
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }

    std::cout << "[someipd] Network stack stopped." << std::endl;
}

}  // namespace score::someipd
