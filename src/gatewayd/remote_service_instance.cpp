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

#include "remote_service_instance.h"

#include <cstring>
#include <iostream>

#include "score/containers/non_relocatable_vector.h"
#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/types.h"

using score::mw::com::GenericProxy;
using score::mw::com::SamplePtr;

namespace score::someip_gateway::gatewayd {

using network_service::interfaces::message_transfer::SomeipMessageTransferProxy;

static const std::size_t max_sample_count = 10;
static const std::size_t SOMEIP_FULL_HEADER_SIZE = 16;

RemoteServiceInstance::RemoteServiceInstance(
    std::shared_ptr<const config::ServiceInstance> service_instance_config,
    score::mw::com::GenericSkeleton&& ipc_skeleton, SomeipMessageTransferProxy someip_message_proxy)
    : service_instance_config_(std::move(service_instance_config)),
      ipc_skeleton_(std::move(ipc_skeleton)),
      someip_message_proxy_(std::move(someip_message_proxy)) {
    // TODO: Error handling
    (void)ipc_skeleton_.OfferService();

    // TODO: This should be dispatched centrally
    someip_message_proxy_.message_.SetReceiveHandler([this]() {
        someip_message_proxy_.message_.GetNewSamples(
            [this](auto message_sample) {
                // TODO: Check if size is larger than capacity of data
                score::cpp::span<const std::byte> message(message_sample->data,
                                                          message_sample->size);
                if (message.size() < SOMEIP_FULL_HEADER_SIZE) {
                    std::cerr << "Received SOME/IP message is too small: " << message.size()
                              << " bytes." << std::endl;
                    return;
                }
                // TODO: Check service id, method id, etc. Maybe do that in the dispatcher already?
                auto payload = message.subspan(SOMEIP_FULL_HEADER_SIZE);

                // TODO: Check based on event/method id within the SOME/IP header which event was
                // received, to forward via the correct IPC event. For now, we assume it's always
                // the echo_response_tiny event.
                auto& event = const_cast<score::mw::com::GenericSkeletonEvent&>(
                    ipc_skeleton_.GetEvents().find("echo_response_tiny")->second);
                auto maybe_sample = event.Allocate();
                if (!maybe_sample.has_value()) {
                    std::cerr << "Failed to allocate SOME/IP message:"
                              << maybe_sample.error().Message() << std::endl;
                    return;
                }
                auto sample = std::move(maybe_sample).value();

                // TODO: deserialization
                std::memcpy(sample.Get(), payload.data(),
                            std::min(sizeof(echo_service::EchoResponseTiny), payload.size()));

                event.Send(std::move(sample));
            },
            max_sample_count);
    });

    someip_message_proxy_.message_.Subscribe(max_sample_count);
}

namespace {
struct FindServiceContext {
    std::shared_ptr<const config::ServiceInstance> config;
    score::mw::com::GenericSkeleton skeleton;
    std::vector<std::unique_ptr<RemoteServiceInstance>>& instances;

    FindServiceContext(std::shared_ptr<const config::ServiceInstance> config_,
                       score::mw::com::GenericSkeleton&& skeleton_,
                       std::vector<std::unique_ptr<RemoteServiceInstance>>& instances_)
        : config(std::move(config_)), skeleton(std::move(skeleton_)), instances(instances_) {}
};

}  // namespace

Result<mw::com::FindServiceHandle> RemoteServiceInstance::CreateAsyncRemoteService(
    std::shared_ptr<const config::ServiceInstance> service_instance_config,
    std::vector<std::unique_ptr<RemoteServiceInstance>>& instances) {
    if (service_instance_config == nullptr) {
        std::cerr << "ERROR: Service instance config is nullptr!" << std::endl;
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }
    auto ipc_instance_specifier = score::mw::com::InstanceSpecifier::Create(
                                      service_instance_config->instance_specifier()->str())
                                      .value();

    score::containers::NonRelocatableVector<score::mw::com::EventInfo> events(
        service_instance_config->events()->size());

    for (const auto& event : *service_instance_config->events()) {
        if (event == nullptr) {
            std::cerr << "ERROR: Encountered nullptr in events configuration!" << std::endl;
            return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
        }

        // TODO: Get the event type info from somewhere. Configuration?
        score::mw::com::DataTypeMetaInfo type_info{sizeof(echo_service::EchoResponseTiny),
                                                   alignof(echo_service::EchoResponseTiny)};
        events.emplace_back(
            score::mw::com::EventInfo{event->event_name()->string_view(), type_info});
    }

    score::mw::com::GenericSkeletonServiceElementInfo create_params;
    create_params.events = events;

    auto create_ipc_result =
        score::mw::com::GenericSkeleton::Create(ipc_instance_specifier, create_params);
    // TODO: Error handling
    auto ipc_skeleton = std::move(create_ipc_result).value();

    std::cout << "Starting discovery of remote service: "
              << service_instance_config->instance_specifier()->string_view() << "\n";

    auto someipd_instance_specifier =
        score::mw::com::InstanceSpecifier::Create(std::string("gatewayd/someipd_messages")).value();

    // TODO: StartFindService should be modified to handle arbitrarily large lambdas
    // or we need to check whether it is OK to stick with dynamic allocation here.
    auto context = std::make_unique<FindServiceContext>(service_instance_config,
                                                        std::move(ipc_skeleton), instances);

    return SomeipMessageTransferProxy::StartFindService(
        [context = std::move(context)](auto handles, auto find_handle) {
            auto this_config = context->config;

            auto proxy_result = SomeipMessageTransferProxy::Create(handles.front());
            if (!proxy_result.has_value()) {
                std::cerr << "SomeipMessageTransferProxy creation failed for "
                          << this_config->instance_specifier()->string_view() << ": "
                          << proxy_result.error().Message() << "\n";
                return;
            }

            // TODO: Add mutex if callbacks can run concurrently
            context->instances.push_back(std::make_unique<RemoteServiceInstance>(
                this_config, std::move(context->skeleton), std::move(proxy_result).value()));

            std::cout << "SomeipMessageTransferProxy created for "
                      << this_config->instance_specifier()->string_view() << "\n";

            SomeipMessageTransferProxy::StopFindService(find_handle);
        },
        someipd_instance_specifier);
}

}  // namespace score::someip_gateway::gatewayd
