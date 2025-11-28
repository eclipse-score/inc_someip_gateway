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

#include "remote_service_instance.h"

#include <score/mw/com/types.h>

#include <cstring>
#include <iostream>

using score::mw::com::GenericProxy;
using score::mw::com::SamplePtr;
using someip_message_service::SomeipMessageServiceProxy;

namespace score::someip_gateway::gatewayd {

static const std::size_t max_sample_count = 10;
static const std::size_t SOMEIP_FULL_HEADER_SIZE = 16;

RemoteServiceInstance::RemoteServiceInstance(
    std::shared_ptr<const config::ServiceInstance> service_instance_config,
    echo_service::EchoResponseSkeleton&& ipc_skeleton,
    SomeipMessageServiceProxy someip_message_proxy)
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

                auto maybe_sample = ipc_skeleton_.echo_response_tiny_.Allocate();
                if (!maybe_sample.has_value()) {
                    std::cerr << "Failed to allocate SOME/IP message:"
                              << maybe_sample.error().Message() << std::endl;
                    return;
                }
                auto sample = std::move(maybe_sample).value();

                // TODO: deserialization
                std::memcpy(sample.Get(), payload.data(),
                            std::min(sizeof(echo_service::EchoResponseTiny), payload.size()));

                ipc_skeleton_.echo_response_tiny_.Send(std::move(sample));
            },
            max_sample_count);
    });

    someip_message_proxy_.message_.Subscribe(max_sample_count);
}

namespace {
struct FindServiceContext {
    std::shared_ptr<const config::ServiceInstance> config;
    echo_service::EchoResponseSkeleton skeleton;
    std::vector<std::unique_ptr<RemoteServiceInstance>>& instances;

    FindServiceContext(std::shared_ptr<const config::ServiceInstance> config_,
                       echo_service::EchoResponseSkeleton&& skeleton_,
                       std::vector<std::unique_ptr<RemoteServiceInstance>>& instances_)
        : config(std::move(config_)), skeleton(std::move(skeleton_)), instances(instances_) {}
};

}  // namespace

Result<mw::com::FindServiceHandle> RemoteServiceInstance::CreateAsyncRemoteService(
    std::shared_ptr<const config::ServiceInstance> service_instance_config,
    std::vector<std::unique_ptr<RemoteServiceInstance>>& instances) {
    if (service_instance_config == nullptr) {
        std::cerr << "ERROR: Service instance config is nullptr!" << std::endl;
        return MakeUnexpected(mw::com::impl::ComErrc::kInvalidConfiguration);
    }
    auto ipc_instance_specifier = score::mw::com::InstanceSpecifier::Create(
                                      service_instance_config->instance_specifier()->str())
                                      .value();

    // TODO: Needs to be a generic Skeleton. Just for prototype showcase.
    auto create_ipc_result = echo_service::EchoResponseSkeleton::Create(ipc_instance_specifier);
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

    return SomeipMessageServiceProxy::StartFindService(
        [context = std::move(context)](auto handles, auto find_handle) {
            auto this_config = context->config;

            auto proxy_result = SomeipMessageServiceProxy::Create(handles.front());
            if (!proxy_result.has_value()) {
                std::cerr << "SomeipMessageServiceProxy creation failed for "
                          << this_config->instance_specifier()->string_view() << ": "
                          << proxy_result.error().Message() << "\n";
                return;
            }

            // TODO: Add mutex if callbacks can run concurrently
            context->instances.push_back(std::make_unique<RemoteServiceInstance>(
                this_config, std::move(context->skeleton), std::move(proxy_result).value()));

            std::cout << "SomeipMessageServiceProxy created for "
                      << this_config->instance_specifier()->string_view() << "\n";

            SomeipMessageServiceProxy::StopFindService(find_handle);
        },
        someipd_instance_specifier);
}

}  // namespace score::someip_gateway::gatewayd
