/********************************************************************************
 * Copyright (c) 2025 ETAS GmbH
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

#include <score/mw/com/runtime.h>

#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <score/span.hpp>
#include <thread>
#include <vsomeip/defines.hpp>
#include <vsomeip/primitive_types.hpp>
#include <vsomeip/vsomeip.hpp>

#include "src/gatewayd/interfaces/someip_message_service.h"

const char* someipd_name = "someipd";

static const vsomeip::service_t service_id = 0x1111;
static const vsomeip::instance_t service_instance_id = 0x2222;
static const vsomeip::method_t service_method_id = 0x3333;

static const std::size_t max_sample_count = 10;

#define SAMPLE_SERVICE_ID 0x1234
#define RESPONSE_SAMPLE_SERVICE_ID 0x4321
#define SAMPLE_INSTANCE_ID 0x5678
#define SAMPLE_METHOD_ID 0x0421

#define SAMPLE_EVENT_ID 0x8778
#define SAMPLE_GET_METHOD_ID 0x0001
#define SAMPLE_SET_METHOD_ID 0x0002

#define SAMPLE_EVENTGROUP_ID 0x4465

#define OTHER_SAMPLE_SERVICE_ID 0x0248
#define OTHER_SAMPLE_INSTANCE_ID 0x5422
#define OTHER_SAMPLE_METHOD_ID 0x1421

using someip_message_service::SomeipMessageServiceProxy;
using someip_message_service::SomeipMessageServiceSkeleton;

// Global flag to control application shutdown
static std::atomic<bool> shutdown_requested{false};

// Signal handler for graceful shutdown
void termination_handler(int /*signal*/) {
    std::cout << "Received termination signal. Initiating graceful shutdown..." << std::endl;
    shutdown_requested.store(true);
}

int main(int argc, const char* argv[]) {
    // Register signal handlers for graceful shutdown
    std::signal(SIGTERM, termination_handler);
    std::signal(SIGINT, termination_handler);

    score::mw::com::runtime::InitializeRuntime(argc, argv);

    auto runtime = vsomeip::runtime::get();
    auto application = runtime->create_application(someipd_name);
    if (!application->init()) {
        std::cerr << "App init failed";
        return 1;
    }

    std::thread([application]() {
        auto handles =
            SomeipMessageServiceProxy::FindService(
                score::mw::com::InstanceSpecifier::Create(std::string("someipd/gatewayd_messages"))
                    .value())
                .value();

        {  // Proxy for receiving messages from gatewayd to be sent via SOME/IP
            auto proxy = SomeipMessageServiceProxy::Create(handles.front()).value();
            proxy.message_.Subscribe(max_sample_count);

            // Skeleton for transmitting messages from the network to gatewayd
            auto create_result = SomeipMessageServiceSkeleton::Create(
                score::mw::com::InstanceSpecifier::Create(std::string("someipd/someipd_messages"))
                    .value());
            // TODO: Error handling
            auto skeleton = std::move(create_result).value();
            (void)skeleton.OfferService();

            application->register_message_handler(
                RESPONSE_SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID,
                [&skeleton](const std::shared_ptr<vsomeip::message>& msg) {
                    auto maybe_message = skeleton.message_.Allocate();
                    if (!maybe_message.has_value()) {
                        std::cerr << "Failed to allocate SOME/IP message:"
                                  << maybe_message.error().Message() << std::endl;
                        return;
                    }
                    auto message_sample = std::move(maybe_message).value();
                    memcpy(message_sample->data + VSOMEIP_FULL_HEADER_SIZE,
                           msg->get_payload()->get_data(), msg->get_payload()->get_length());
                    message_sample->size =
                        msg->get_payload()->get_length() + VSOMEIP_FULL_HEADER_SIZE;
                    skeleton.message_.Send(std::move(message_sample));
                });

            application->request_service(RESPONSE_SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);
            std::set<vsomeip::eventgroup_t> its_groups;
            its_groups.insert(SAMPLE_EVENTGROUP_ID);
            application->request_event(RESPONSE_SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID,
                                       SAMPLE_EVENT_ID, its_groups,
                                       vsomeip::event_type_e::ET_EVENT);
            application->subscribe(RESPONSE_SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID,
                                   SAMPLE_EVENTGROUP_ID);

            std::set<vsomeip::eventgroup_t> groups{SAMPLE_EVENTGROUP_ID};
            application->offer_event(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID,
                                     groups);
            application->offer_service(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID);

            // application->update_service_configuration(
            //     SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, 12345u, true, true, true);
            auto payload = vsomeip::runtime::get()->create_payload();

            std::cout << "SOME/IP daemon started, waiting for messages..." << std::endl;

            // Process messages until shutdown is requested
            while (!shutdown_requested.load()) {
                // TODO: Use ReceiveHandler + async runtime instead of polling
                proxy.message_.GetNewSamples(
                    [&](auto message_sample) {
                        // TODO: Check if size is larger than capacity of data
                        score::cpp::span<const std::byte> message(message_sample->data,
                                                                  message_sample->size);

                        // Check if sample size is valid and contains at least a SOME/IP header
                        if (message.size() < VSOMEIP_FULL_HEADER_SIZE) {
                            std::cerr << "Received too small sample (size: " << message.size()
                                      << ", expected at least: " << VSOMEIP_FULL_HEADER_SIZE
                                      << "). Skipping message." << std::endl;
                            return;
                        }

                        // TODO: Here we need to find a better way how to pass the message to
                        // vsomeip. There doesn't seem to be a public way to just wrap the existing
                        // buffer.
                        auto payload_data = message.subspan(VSOMEIP_FULL_HEADER_SIZE);
                        payload->set_data(
                            reinterpret_cast<const vsomeip_v3::byte_t*>(payload_data.data()),
                            payload_data.size());
                        application->notify(SAMPLE_SERVICE_ID, SAMPLE_INSTANCE_ID, SAMPLE_EVENT_ID,
                                            payload);
                    },
                    max_sample_count);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            std::cout << "Shutting down SOME/IP daemon..." << std::endl;
        }

        application->stop();
    }).detach();

    application->start();
}
