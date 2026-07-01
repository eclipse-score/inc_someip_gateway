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
#include "score/mw/log/logging.h"
#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/types.h"
#include "src/common/types.h"
#include "src/serializer/serializer.h"

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

    auto service_type_name = service_type_config_->service_type_name()->string_view();
    for (auto event_config : *service_type_config_->events()) {
        auto event_name = event_config->event_name()->string_view();

        auto events_it = ipc_skeleton_.GetEvents().find(*event_config->event_name());
        if (events_it == ipc_skeleton_.GetEvents().cend()) {
            score::mw::log::LogWarn() << "[gatewayd] Event '" << event_name << "' not found in generic IPC skeleton";
            continue;
        }
        auto& ipc_event = const_cast<score::mw::com::GenericSkeletonEvent&>(events_it->second);

        const score_com_serializer* serializer = nullptr;
        auto get_result =
            score_com_serializer_get(service_type_name.data(), service_type_name.size(),
                                     score_com_serializer_element_type_event, event_name.data(),
                                     event_name.size(), &serializer);
        if (get_result != score_com_serializer_result_ok) {
            score::mw::log::LogError() << "[gatewayd] Failed to get serializer for " << service_type_name
                      << "::" << event_name;
            continue;
        }

        event_contexts_.emplace(event_config->event_id(),
                                EventContext{event_config, serializer, &ipc_event});
    }

    // TODO: This should be dispatched centrally
    someip_message_proxy_.message_.SetReceiveHandler([this]() {
        someip_message_proxy_.message_.GetNewSamples(
            [this](auto message_sample) {
                // TODO: Check if size is larger than capacity of data
                score::cpp::span<const std::byte> message(message_sample->data,
                                                          message_sample->size);
                if (message.size() < SOMEIP_FULL_HEADER_SIZE) {
                    score::mw::log::LogError() << "[gatewayd] Received SOME/IP message is too small: "
                              << message.size() << "B, dropping";
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

                auto event_ctx_it = event_contexts_.find(rec_event_id);
                if (event_ctx_it == event_contexts_.end()) {
                    score::mw::log::LogError() << "[gatewayd] No config entry for event 0x" << std::hex
                              << rec_event_id << std::dec << ", dropping";
                    return;
                }
                auto& event_context = event_ctx_it->second;

                auto maybe_sample = event_context.ipc_event->Allocate();
                if (!maybe_sample.has_value()) {
                    score::mw::log::LogError() << "[gatewayd] Failed to allocate IPC sample: "
                              << maybe_sample.error().Message();
                    return;
                }
                auto sample = std::move(maybe_sample).value();

                auto deserialize_result = score_com_serializer_deserialize(
                    event_context.serializer, reinterpret_cast<const uint8_t*>(payload.data()),
                    payload.size(), sample.Get());
                if (deserialize_result != score_com_serializer_result_ok) {
                    score::mw::log::LogError() << "[gatewayd] Deserialization failed for event 0x" << std::hex
                              << rec_event_id << std::dec << ", dropping";
                    return;
                }

                event_context.ipc_event->Send(std::move(sample));
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
        score::mw::log::LogError() << "[gatewayd] ERROR: Service instance config is nullptr!";
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }

    // TODO: Error handling for instance specifier creation
    auto ipc_instance_specifier = score::mw::com::InstanceSpecifier::Create(
                                      service_instance_config->instance_specifier()->str())
                                      .value();

    score::containers::NonRelocatableVector<score::mw::com::EventInfo> events(
        service_type_config->events()->size());

    auto service_type_name = service_type_config->service_type_name()->string_view();

    for (const auto& event : *service_type_config->events()) {
        if (event == nullptr) {
            score::mw::log::LogError() << "[gatewayd] ERROR: Encountered nullptr in events configuration!";
            return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
        }

        auto event_name = event->event_name()->string_view();
        const score_com_serializer* serializer = nullptr;
        auto get_result =
            score_com_serializer_get(service_type_name.data(), service_type_name.size(),
                                     score_com_serializer_element_type_event, event_name.data(),
                                     event_name.size(), &serializer);
        if (get_result != score_com_serializer_result_ok) {
            score::mw::log::LogError() << "[gatewayd] Failed to get serializer for " << service_type_name
                      << "::" << event_name;
            return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
        }

        score::mw::com::DataTypeMetaInfo type_info{
            score_com_serializer_get_sizeof_type(serializer),
            score_com_serializer_get_alignof_type(serializer)};
        events.emplace_back(score::mw::com::EventInfo{event_name, type_info});
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
                score::mw::log::LogError() << "[gatewayd] Proxy creation failed for '"
                          << instance_config->instance_specifier()->string_view()
                          << "': " << proxy_result.error().Message();
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
