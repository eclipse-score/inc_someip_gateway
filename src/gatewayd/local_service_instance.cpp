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

#include "local_service_instance.h"

#include <algorithm>
#include <iostream>
#include <memory>

#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/types.h"

using score::mw::com::GenericProxy;
using score::mw::com::SamplePtr;

namespace score::someip_gateway::gatewayd {

using network_service::interfaces::message_transfer::SomeipMessageTransferSkeleton;

static const std::size_t max_sample_count = 10;

LocalServiceInstance::LocalServiceInstance(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    GenericProxy&& ipc_proxy,
    // TODO: Decouple this via an interface
    SomeipMessageTransferSkeleton& someip_message_skeleton)
    : service_instance_config_(std::move(service_instance_config)),
      service_type_config_(std::move(service_type_config)),
      ipc_proxy_(std::move(ipc_proxy)),
      someip_message_skeleton_(someip_message_skeleton) {
    // Set up IPC event handlers
    auto& events = ipc_proxy_.GetEvents();

    for (auto event_config : *service_type_config_->events()) {
        auto result = events.find(event_config->event_name()->string_view());
        if (result == events.cend()) {
            std::cerr << "[gatewayd] Failed to find " << event_config->event_name()->string_view()
                      << " event in ipc_proxy." << std::endl;
            continue;
        }
        auto& ipc_event = result->second;

        ipc_event.SetReceiveHandler([this, &ipc_event, event_config]() {
            ipc_event.GetNewSamples(
                [&](SamplePtr<void> sample) {
                    auto maybe_message = someip_message_skeleton_.message_.Allocate();
                    if (!maybe_message.has_value()) {
                        std::cerr << "[gatewayd] Failed to allocate SOME/IP message:"
                                  << maybe_message.error().Message() << std::endl;
                        return;
                    }
                    auto message_sample = std::move(maybe_message).value();
                    score::cpp::span<std::byte> message(
                        message_sample->data,
                        network_service::interfaces::message_transfer::MAX_MESSAGE_SIZE);
                    std::size_t pos = 0;

                    // TODO: Design decision: the gateway needs to generate the SOME/IP message
                    // including the header in order to have the E2E protection in the ASIL
                    // context.
                    std::uint16_t service_id = service_type_config_->service_id();
                    message.data()[pos++] = static_cast<std::byte>(service_id >> 8);
                    message.data()[pos++] = static_cast<std::byte>(service_id & 0xFF);

                    std::uint16_t method_id = event_config->event_id();
                    message.data()[pos++] = static_cast<std::byte>(method_id >> 8);
                    message.data()[pos++] = static_cast<std::byte>(method_id & 0xFF);

                    // Length set by someipd
                    pos += 4;

                    // TODO: get client ID during registration at the someipd
                    std::uint16_t client_id = 0xFFFF;
                    message.data()[pos++] = static_cast<std::byte>(client_id >> 8);
                    message.data()[pos++] = static_cast<std::byte>(client_id & 0xFF);

                    std::uint16_t session_id = 0x0000;
                    message.data()[pos++] = static_cast<std::byte>(session_id >> 8);
                    message.data()[pos++] = static_cast<std::byte>(session_id & 0xFF);

                    std::uint8_t protocol_version = 1;
                    message.data()[pos++] = static_cast<std::byte>(protocol_version);

                    std::uint8_t interface_version = service_type_config_->service_version_major();
                    message.data()[pos++] = static_cast<std::byte>(interface_version);

                    std::uint8_t message_type = 0x02;  // NOTIFICATION
                    message.data()[pos++] = static_cast<std::byte>(message_type);

                    std::uint8_t return_code = 0x00;  // Unused
                    message.data()[pos++] = static_cast<std::byte>(return_code);

                    // Serialize payload
                    // TODO: Call serialization plugin here
                    auto payload = message.subspan(pos);
                    std::size_t payload_size = std::min(payload.size(), ipc_event.GetSampleSize());
                    std::memcpy(payload.data(), sample.get(), payload_size);
                    pos += payload_size;

                    message_sample->size = pos;

                    someip_message_skeleton_.message_.Send(std::move(message_sample));
                },
                max_sample_count);
        });

        ipc_event.Subscribe(max_sample_count);
    }
}

namespace {
struct FindServiceContext {
    std::shared_ptr<const mw_someip_config::ServiceInstance> config;
    std::shared_ptr<const mw_someip_config::ServiceType> service_config;
    SomeipMessageTransferSkeleton& skeleton;
    std::vector<std::unique_ptr<LocalServiceInstance>>& instances;

    FindServiceContext(std::shared_ptr<const mw_someip_config::ServiceInstance> config_,
                       std::shared_ptr<const mw_someip_config::ServiceType> service_config_,
                       SomeipMessageTransferSkeleton& skeleton_,
                       std::vector<std::unique_ptr<LocalServiceInstance>>& instances_)
        : config(std::move(config_)),
          service_config(std::move(service_config_)),
          skeleton(skeleton_),
          instances(instances_) {}
};

}  // namespace

Result<mw::com::FindServiceHandle> LocalServiceInstance::CreateAsyncLocalServices(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    SomeipMessageTransferSkeleton& someip_message_skeleton,
    std::vector<std::unique_ptr<LocalServiceInstance>>& instances) {
    if (service_instance_config == nullptr) {
        std::cerr << "[gatewayd] ERROR: Service instance config is nullptr!" << std::endl;
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }

    // TODO: Error handling for instance specifier creation
    auto instance_specifier = score::mw::com::InstanceSpecifier::Create(
                                  service_instance_config->instance_specifier()->str())
                                  .value();

    // TODO: StartFindService should be modified to handle arbitrarily large lambdas
    // or we need to check whether it is OK to stick with dynamic allocation here.
    auto context = std::make_unique<FindServiceContext>(
        service_instance_config, service_type_config, someip_message_skeleton, instances);

    return GenericProxy::StartFindService(
        [context = std::move(context)](auto handles, auto find_handle) {
            auto& instance_config = context->config;
            auto& service_config = context->service_config;

            auto proxy_result = GenericProxy::Create(handles.front());
            if (!proxy_result.has_value()) {
                std::cerr << "[gatewayd] Proxy creation failed for instance specifier: "
                          << instance_config->instance_specifier()->string_view()
                          << "': " << proxy_result.error().Message() << std::endl;
                return;
            }

            // TODO: Add mutex if callbacks can run concurrently or use futures
            context->instances.push_back(std::make_unique<LocalServiceInstance>(
                instance_config, service_config, std::move(proxy_result).value(),
                context->skeleton));

            std::cout << "[gatewayd] LocalServiceInstance created for instance specifier: "
                      << instance_config->instance_specifier()->string_view() << std::endl;

            GenericProxy::StopFindService(find_handle);
        },
        instance_specifier);
}

}  // namespace score::someip_gateway::gatewayd
