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

#include <cstddef>
#include <cstring>
#include <iostream>
#include <memory>

#include "score/containers/non_relocatable_vector.h"
#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/types.h"
#include "score/mw/log/logging.h"
#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"
#include "src/serializer/serializer.h"

using score::mw::com::GenericProxy;
using score::mw::com::SamplePtr;
using score::someip::EventId;

namespace score::someip_gateway::gatewayd {

RemoteServiceInstance::RemoteServiceInstance(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    score::mw::com::GenericSkeleton&& ipc_skeleton, socom::Client_connector::Uptr client_connector,
    std::unordered_map<std::uint16_t, EventContext> event_contexts)
    : service_instance_config_(std::move(service_instance_config)),
      service_type_config_(std::move(service_type_config)),
      ipc_skeleton_(std::move(ipc_skeleton)),
      client_connector_(std::move(client_connector)),
      event_contexts_(std::move(event_contexts)) {}

Result<std::unique_ptr<RemoteServiceInstance>> RemoteServiceInstance::Create(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    score::mw::com::GenericSkeleton&& ipc_skeleton, socom::Runtime& socom_runtime) {
    auto offer_result = ipc_skeleton.OfferService();
    if (!offer_result.has_value()) {
        score::mw::log::LogError() << "[gatewayd] Failed to offer IPC skeleton for '"
                                   << service_type_config->service_type_name()->string_view()
                                   << "': " << offer_result.error().Message();
        return MakeUnexpected(score::mw::com::ComErrc::kBindingFailure);
    }

    std::unordered_map<std::uint16_t, EventContext> event_contexts;
    socom::Event_id socom_event_id{0U};
    auto service_type_name = service_type_config->service_type_name()->string_view();
    for (auto event_config : *service_type_config->events()) {
        auto event_name = event_config->event_name()->string_view();

        auto events_it = ipc_skeleton.GetEvents().find(*event_config->event_name());
        if (events_it == ipc_skeleton.GetEvents().cend()) {
            score::mw::log::LogWarn()
                << "[gatewayd] Event '" << event_name << "' not found in generic IPC skeleton";
            ++socom_event_id;
            continue;
        }
        auto& ipc_event = const_cast<score::mw::com::GenericSkeletonEvent&>(events_it->second);

        const score_com_serializer* serializer = nullptr;
        auto get_result =
            score_com_serializer_get(service_type_name.data(), service_type_name.size(),
                                     score_com_serializer_element_type_event, event_name.data(),
                                     event_name.size(), &serializer);
        if (get_result != score_com_serializer_result_ok) {
            score::mw::log::LogError() << "[gatewayd] Failed to get serializer for "
                                       << service_type_name << "::" << event_name;
            ++socom_event_id;
            continue;
        }

        event_contexts.emplace(socom_event_id, EventContext{event_config, serializer, &ipc_event});
        ++socom_event_id;
    }

    socom::Service_interface_identifier const iface{
        service_type_config->service_type_name()->string_view(),
        {service_type_config->service_version_major(),
         static_cast<uint16_t>(service_type_config->service_version_minor())}};

    socom::Service_instance const inst{service_type_config->service_type_name()->string_view()};

    socom::Service_interface_definition const client_config{
        iface, socom::to_num_of_methods(0),
        socom::to_num_of_events(service_type_config->events()->size())};

    // Create the instance first so callbacks can capture a raw pointer to it.
    auto instance = std::unique_ptr<RemoteServiceInstance>(new RemoteServiceInstance(
        std::move(service_instance_config), std::move(service_type_config), std::move(ipc_skeleton),
        nullptr, std::move(event_contexts)));

    auto connector_result = socom_runtime.make_client_connector(
        client_config, inst,
        {
            .on_service_state_change =
                [instance_ptr = instance.get()](socom::Client_connector const&,
                                                socom::Service_state state,
                                                socom::Server_service_interface_definition const&) {
                    std::cout << "[gatewayd] RemoteServiceInstance - client_connector "
                                 "on_service_state_change: "
                              << "new state=" << static_cast<int>(state) << std::endl;
                    if (state != socom::Service_state::available) {
                        return;
                    }
                    std::cout << "[gatewayd] RemoteServiceInstance - client_connector "
                                 "on_service_state_change: service is now available, subscribing "
                                 "to events\n";
                    for (std::size_t i = 0;
                         i < instance_ptr->service_type_config_->events()->size(); ++i) {
                        (void)instance_ptr->client_connector_->subscribe_event(
                            static_cast<socom::Event_id>(i), socom::Event_mode::update);
                    }
                },
            .on_event_update =
                [instance_ptr = instance.get()](socom::Client_connector const&,
                                                socom::Event_id event_id, socom::Payload payload) {
                    instance_ptr->forward_event(event_id, std::move(payload));
                },
            .on_event_requested_update = [](socom::Client_connector const&, socom::Event_id,
                                            socom::Payload) {},
            .on_event_payload_allocate = [](socom::Client_connector const&, socom::Event_id)
                -> score::Result<socom::Writable_payload> {
                // Payload allocation is handled by the IPC binding (read-only SHM slot from
                // someipd). This callback is never called in normal operation.
                assert(false &&
                       "on_event_payload_allocate must not be called on RemoteServiceInstance");
                return MakeUnexpected(socom::Error::runtime_error_request_rejected);
            },
        });

    if (!connector_result.has_value()) {
        score::mw::log::LogError()
            << "[gatewayd] Failed to create client connector for '"
            << instance->service_type_config_->service_type_name()->string_view() << "'";
        return MakeUnexpected(socom::Error::runtime_error_request_rejected);
    }
    instance->client_connector_ = std::move(connector_result).value();

    return instance;
}

void RemoteServiceInstance::forward_event(socom::Event_id event_id, socom::Payload payload) {
    // TODO: check if the assumption holds.
    auto event_context = event_contexts_[event_id];

    auto events_it =
        ipc_skeleton_.GetEvents().find(event_context.config->event_name()->string_view());
    if (events_it == ipc_skeleton_.GetEvents().cend()) {
        score::mw::log::LogError()
            << "[gatewayd] Event '" << event_context.config->event_name()->string_view()
            << "' not found in IPC skeleton, dropping";
        return;
    }
    auto& ipc_event = const_cast<score::mw::com::GenericSkeletonEvent&>(events_it->second);

    // Extract payload
    auto const message = payload.data().subspan(someip::kSomeipFullHeaderSize);

    auto maybe_sample = ipc_event.Allocate();
    if (!maybe_sample.has_value()) {
        score::mw::log::LogError()
            << "[gatewayd] Failed to allocate IPC sample: " << maybe_sample.error().Message();
        return;
    }
    auto sample = std::move(maybe_sample).value();

    auto deserialize_result = score_com_serializer_deserialize(
        event_context.serializer, reinterpret_cast<const uint8_t*>(message.data()), message.size(),
        sample.Get());
    if (deserialize_result != score_com_serializer_result_ok) {
        score::mw::log::LogError()
            << "[gatewayd] Deserialization failed for event "
            << event_context.config->event_name()->string_view() << ", dropping";
        return;
    }

    ipc_event.Send(std::move(sample));
};

Result<void> RemoteServiceInstance::CreateAsyncRemoteService(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    socom::Runtime& socom_runtime, std::vector<std::unique_ptr<RemoteServiceInstance>>& instances) {
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
            score::mw::log::LogError()
                << "[gatewayd] ERROR: Encountered nullptr in events configuration!";
            return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
        }

        auto event_name = event->event_name()->string_view();
        const score_com_serializer* serializer = nullptr;
        auto get_result =
            score_com_serializer_get(service_type_name.data(), service_type_name.size(),
                                     score_com_serializer_element_type_event, event_name.data(),
                                     event_name.size(), &serializer);
        if (get_result != score_com_serializer_result_ok) {
            score::mw::log::LogError() << "[gatewayd] Failed to get serializer for "
                                       << service_type_name << "::" << event_name;
            return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
        }

        score::mw::com::DataTypeMetaInfo type_info{
            score_com_serializer_get_sizeof_type(serializer),
            score_com_serializer_get_alignof_type(serializer)};
        events.emplace_back(score::mw::com::EventInfo{event_name, type_info});
    }

    score::mw::com::GenericSkeletonServiceElementInfo service_element_info;
    service_element_info.events = events;

    auto create_ipc_result =
        score::mw::com::GenericSkeleton::Create(ipc_instance_specifier, service_element_info);
    if (!create_ipc_result.has_value()) {
        score::mw::log::LogError() << "[gatewayd] Failed to create IPC skeleton for '"
                                   << service_instance_config->instance_specifier()->string_view()
                                   << "': " << create_ipc_result.error().Message();
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }
    auto ipc_skeleton = std::move(create_ipc_result).value();

    auto create_result = RemoteServiceInstance::Create(service_instance_config, service_type_config,
                                                       std::move(ipc_skeleton), socom_runtime);
    if (!create_result.has_value()) {
        score::mw::log::LogError()
            << "[gatewayd] Failed to create RemoteServiceInstance for '"
            << service_instance_config->instance_specifier()->string_view() << "'";
        return MakeUnexpected(score::mw::com::ComErrc::kBindingFailure);
    }

    instances.push_back(std::move(create_result).value());

    std::cout << "[gatewayd] RemoteServiceInstance created for instance specifier: "
              << service_instance_config->instance_specifier()->string_view() << "\n";
    return {};
}
}  // namespace score::someip_gateway::gatewayd
