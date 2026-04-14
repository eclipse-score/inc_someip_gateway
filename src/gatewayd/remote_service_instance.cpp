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
#include "src/common/types.h"
// TODO: Remove dependency on echo service
#include "tests/benchmarks/echo_service.h"

using score::mw::com::GenericProxy;
using score::mw::com::SamplePtr;
using score::someip::EventId;

namespace score::someip_gateway::gatewayd {

using network_service::interfaces::message_transfer::SomeipMessageTransferProxy;

static const std::size_t max_sample_count = 10;
static const std::size_t SOMEIP_FULL_HEADER_SIZE = 16;

RemoteServiceInstance::RemoteServiceInstance(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    score::mw::com::GenericSkeleton&& ipc_skeleton, SomeipMessageTransferProxy someip_message_proxy)
    : service_instance_config_(std::move(service_instance_config)),
      service_type_config_(std::move(service_type_config)),
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
                    std::cerr << "[gatewayd] Received SOME/IP message is too small: "
                              << message.size() << "B, dropping" << std::endl;
                    return;
                }

                // TODO: For now, read event ID from header. This should get obsolete as soon as
                // there is a dedicated channel (via SOCOM) for each event.
                auto rec_event_id =
                    static_cast<EventId>((std::to_integer<uint16_t>(message[2]) << 8) |
                                         std::to_integer<uint16_t>(message[3]));

                std::cout << "[gatewayd] Received SOME/IP event: event=0x" << std::hex
                          << rec_event_id << std::dec
                          << " payload=" << (message.size() - SOMEIP_FULL_HEADER_SIZE) << "B"
                          << std::endl;

                auto payload = message.subspan(SOMEIP_FULL_HEADER_SIZE);

                // Look up event config by event ID. This is needed to find the corresponding IPC
                // event to forward the data.
                const score::mw_someip_config::Event* cfg_event =
                    service_type_config_->events()->LookupByKey(rec_event_id);
                if (cfg_event == nullptr) {
                    std::cerr << "[gatewayd] No config entry for event 0x" << std::hex
                              << rec_event_id << std::dec << ", dropping" << std::endl;
                    return;
                }

                auto events_it = ipc_skeleton_.GetEvents().find(*cfg_event->event_name());
                if (events_it == ipc_skeleton_.GetEvents().cend()) {
                    std::cerr << "[gatewayd] Event '" << cfg_event->event_name()->string_view()
                              << "' not found in IPC skeleton, dropping" << std::endl;
                    return;
                }
                auto& event = const_cast<score::mw::com::GenericSkeletonEvent&>(events_it->second);

                auto maybe_sample = event.Allocate();
                if (!maybe_sample.has_value()) {
                    std::cerr << "[gatewayd] Failed to allocate IPC sample: "
                              << maybe_sample.error().Message() << std::endl;
                    return;
                }
                auto sample = std::move(maybe_sample).value();

                // TODO: deserialization
                std::memcpy(sample.Get(), payload.data(),
                            std::min(sizeof(echo_service::EchoResponseTiny), payload.size()));

                event.Send(std::move(sample));
                std::cout << "[gatewayd] Forwarded event 0x" << std::hex << rec_event_id << std::dec
                          << " to IPC subscribers" << std::endl;
            },
            max_sample_count);
    });

    someip_message_proxy_.message_.Subscribe(max_sample_count);
}

namespace {
struct FindServiceContext {
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config;
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config;
    score::mw::com::GenericSkeleton skeleton;
    std::vector<std::unique_ptr<RemoteServiceInstance>>& instances;

    FindServiceContext(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config_,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config_,
        score::mw::com::GenericSkeleton&& skeleton_,
        std::vector<std::unique_ptr<RemoteServiceInstance>>& instances_)
        : service_instance_config(std::move(service_instance_config_)),
          service_type_config(std::move(service_type_config_)),
          skeleton(std::move(skeleton_)),
          instances(instances_) {}
};

}  // namespace

Result<mw::com::FindServiceHandle> RemoteServiceInstance::CreateAsyncRemoteService(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    std::vector<std::unique_ptr<RemoteServiceInstance>>& instances) {
    if (service_instance_config == nullptr) {
        std::cerr << "[gatewayd] ERROR: Service instance config is nullptr!" << std::endl;
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }

    // TODO: Error handling for instance specifier creation
    auto ipc_instance_specifier = score::mw::com::InstanceSpecifier::Create(
                                      service_instance_config->instance_specifier()->str())
                                      .value();

    score::containers::NonRelocatableVector<score::mw::com::EventInfo> events(
        service_type_config->events()->size());

    for (const auto& event : *service_type_config->events()) {
        if (event == nullptr) {
            std::cerr << "[gatewayd] ERROR: Encountered nullptr in events configuration!"
                      << std::endl;
            return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
        }

        // TODO: Get the event type info from serializer. To support the benchmark app, for now just
        // use the type info of EchoResponseTiny for all events
        score::mw::com::DataTypeMetaInfo type_info{sizeof(echo_service::EchoResponseTiny),
                                                   alignof(echo_service::EchoResponseTiny)};
        events.emplace_back(
            score::mw::com::EventInfo{event->event_name()->string_view(), type_info});
    }

    score::mw::com::GenericSkeletonServiceElementInfo service_element_info;
    service_element_info.events = events;

    // TODO: Error handling
    auto create_ipc_result =
        score::mw::com::GenericSkeleton::Create(ipc_instance_specifier, service_element_info);

    auto ipc_skeleton = std::move(create_ipc_result).value();

    // TODO: Error handling for instance specifier creation
    auto someipd_instance_specifier =
        score::mw::com::InstanceSpecifier::Create(std::string("gatewayd/someipd_messages")).value();

    // TODO: StartFindService should be modified to handle arbitrarily large lambdas
    // or we need to check whether it is OK to stick with dynamic allocation here.
    auto context = std::make_unique<FindServiceContext>(
        service_instance_config, service_type_config, std::move(ipc_skeleton), instances);

    return SomeipMessageTransferProxy::StartFindService(
        [context = std::move(context)](auto handles, auto find_handle) {
            auto& instance_config = context->service_instance_config;

            auto proxy_result = SomeipMessageTransferProxy::Create(handles.front());
            if (!proxy_result.has_value()) {
                std::cerr << "[gatewayd] Proxy creation failed for '"
                          << instance_config->instance_specifier()->string_view()
                          << "': " << proxy_result.error().Message() << std::endl;
                return;
            }

            // TODO: Add mutex if callbacks can run concurrently
            context->instances.push_back(std::make_unique<RemoteServiceInstance>(
                instance_config, context->service_type_config, std::move(context->skeleton),
                std::move(proxy_result).value()));

            std::cout << "[gatewayd] RemoteServiceInstance created for instance specifier: "
                      << instance_config->instance_specifier()->string_view() << std::endl;

            SomeipMessageTransferProxy::StopFindService(find_handle);
        },
        someipd_instance_specifier);
}

}  // namespace score::someip_gateway::gatewayd
