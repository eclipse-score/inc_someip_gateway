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

#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <score/assert.hpp>
#include <utility>

#include "score/containers/non_relocatable_vector.h"
#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/types.h"
#include "score/mw/log/logging.h"
#include "score/serializer/serializer.h"
#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"

namespace score::someip_gateway::gatewayd {

namespace {

bool is_valid_remote_config(const mw_someip_config::ServiceInstance* service_instance_config,
                            const mw_someip_config::ServiceType* service_type_config) noexcept {
    if ((service_instance_config == nullptr) ||
        (service_instance_config->instance_specifier() == nullptr) ||
        (service_type_config == nullptr) || (service_type_config->service_type_name() == nullptr) ||
        (service_type_config->events() == nullptr) || service_type_config->events()->empty() ||
        (service_type_config->events()->size() >
         static_cast<std::size_t>(std::numeric_limits<socom::Event_id>::max()))) {
        return false;
    }
    for (const auto* event_config : *service_type_config->events()) {
        if ((event_config == nullptr) || (event_config->event_name() == nullptr)) {
            return false;
        }
    }
    return true;
}

}  // namespace

RemoteServiceInstance::RemoteServiceInstance(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    score::mw::com::GenericSkeleton&& ipc_skeleton,
    std::unordered_map<socom::Event_id, EventContext> event_contexts,
    socom::Client_connector::Uptr client_connector)
    : service_instance_config_(std::move(service_instance_config)),
      service_type_config_(std::move(service_type_config)),
      ipc_skeleton_(std::move(ipc_skeleton)),
      event_contexts_(std::move(event_contexts)),
      client_connector_(std::move(client_connector)) {}

RemoteServiceInstance::~RemoteServiceInstance() {
    client_connector_.reset();
    ipc_skeleton_.StopOfferService();
}

Result<std::unique_ptr<RemoteServiceInstance>> RemoteServiceInstance::Create(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    score::mw::com::GenericSkeleton&& ipc_skeleton, socom::Runtime& socom_runtime) {
    if (!is_valid_remote_config(service_instance_config.get(), service_type_config.get())) {
        score::mw::log::LogError()
            << "[gatewayd] Invalid configuration passed to RemoteServiceInstance::Create";
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }

    auto const service_type_name = service_type_config->service_type_name()->string_view();
    auto offer_result = ipc_skeleton.OfferService();
    if (!offer_result.has_value()) {
        score::mw::log::LogError() << "[gatewayd] Failed to offer IPC skeleton for '"
                                   << service_type_name << "': " << offer_result.error().Message();
        return MakeUnexpected(score::mw::com::ComErrc::kBindingFailure);
    }

    std::unordered_map<socom::Event_id, EventContext> event_contexts{};
    event_contexts.reserve(service_type_config->events()->size());
    auto skeleton_events = ipc_skeleton.GetEvents();
    socom::Event_id socom_event_id{0U};
    for (const auto* event_config : *service_type_config->events()) {
        auto const event_name = event_config->event_name()->string_view();

        auto events_it = skeleton_events.find(event_name);
        if (events_it == skeleton_events.cend()) {
            score::mw::log::LogWarn()
                << "[gatewayd] Event '" << event_name << "' not found in generic IPC skeleton";
            ++socom_event_id;
            continue;
        }
        auto& ipc_event = events_it->second;

        const score_com_serializer* serializer = nullptr;
        auto const get_result =
            score_com_serializer_get(service_type_name.data(), service_type_name.size(),
                                     score_com_serializer_element_type_event, event_name.data(),
                                     event_name.size(), &serializer);
        if ((get_result != score_com_serializer_result_ok) || (serializer == nullptr)) {
            score::mw::log::LogError() << "[gatewayd] Failed to get serializer for "
                                       << service_type_name << "::" << event_name;
            ++socom_event_id;
            continue;
        }

        event_contexts.emplace(socom_event_id, EventContext{event_config, serializer, &ipc_event});
        ++socom_event_id;
    }

    socom::Service_interface_identifier const iface{
        service_type_name,
        {service_type_config->service_version_major(),
         static_cast<std::uint16_t>(service_type_config->service_version_minor())}};

    socom::Service_instance const inst{service_type_name};

    socom::Service_interface_definition const client_config{
        iface, socom::to_num_of_methods(0U),
        socom::to_num_of_events(service_type_config->events()->size())};

    auto instance = std::unique_ptr<RemoteServiceInstance>(new RemoteServiceInstance(
        std::move(service_instance_config), std::move(service_type_config), std::move(ipc_skeleton),
        std::move(event_contexts), nullptr));

    auto connector_result = socom_runtime.make_client_connector(
        client_config, inst,
        {
            .on_service_state_change =
                [instance_ptr = instance.get()](socom::Client_connector const& connector,
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
                    for (auto const& [event_id, _] : instance_ptr->event_contexts_) {
                        if (const auto sub_result =
                                connector.subscribe_event(event_id, socom::Event_mode::update);
                            !sub_result) {
                            score::mw::log::LogError()
                                << "[gatewayd] RemoteServiceInstance - Failed to subscribe event "
                                << event_id << ": " << sub_result.error().Message();
                        }
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
                SCORE_LANGUAGE_FUTURECPP_ASSERT(
                    false &&
                    "on_event_payload_allocate must not be called on RemoteServiceInstance");
                return MakeUnexpected(socom::Error::runtime_error_request_rejected);
            },
        });

    if (!connector_result.has_value()) {
        score::mw::log::LogError()
            << "[gatewayd] Failed to create client connector for '" << service_type_name << "'";
        instance.reset();
        return MakeUnexpected(socom::Error::runtime_error_request_rejected);
    }
    instance->client_connector_ = std::move(connector_result).value();

    return instance;
}

void RemoteServiceInstance::forward_event(socom::Event_id event_id, socom::Payload payload) {
    auto const context_it = event_contexts_.find(event_id);
    if (context_it == event_contexts_.cend()) {
        score::mw::log::LogWarn()
            << "[gatewayd] RemoteServiceInstance - Dropping update for unknown or skipped event "
            << event_id;
        return;
    }
    auto const& event_context = context_it->second;

    auto const raw_data = payload.data();
    if (raw_data.size() < someip::kSomeipFullHeaderSize) {
        score::mw::log::LogError()
            << "[gatewayd] RemoteServiceInstance - Dropping truncated SOME/IP frame ("
            << raw_data.size() << " bytes) for event "
            << event_context.config->event_name()->string_view();
        return;
    }
    auto const message = raw_data.subspan(someip::kSomeipFullHeaderSize);

    auto maybe_sample = event_context.ipc_event->Allocate();
    if (!maybe_sample.has_value()) {
        score::mw::log::LogError()
            << "[gatewayd] Failed to allocate IPC sample: " << maybe_sample.error().Message();
        return;
    }
    auto sample = std::move(maybe_sample).value();
    if (sample.Get() == nullptr) {
        score::mw::log::LogError() << "[gatewayd] Allocated IPC sample pointer is null for event "
                                   << event_context.config->event_name()->string_view();
        return;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    auto const deserialize_result = score_com_serializer_deserialize(
        event_context.serializer, reinterpret_cast<const std::uint8_t*>(message.data()),
        message.size(), sample.Get());
    if (deserialize_result != score_com_serializer_result_ok) {
        score::mw::log::LogError()
            << "[gatewayd] Deserialization failed for event "
            << event_context.config->event_name()->string_view() << ", dropping";
        return;
    }

    if (const auto result = event_context.ipc_event->Send(std::move(sample)); !result) {
        score::mw::log::LogError()
            << "[gatewayd] RemoteServiceInstance - Failed to send IPC event: "
            << result.error().Message();
    }
}

Result<void> RemoteServiceInstance::CreateAsyncRemoteService(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    socom::Runtime& socom_runtime, std::vector<std::unique_ptr<RemoteServiceInstance>>& instances) {
    if (!is_valid_remote_config(service_instance_config.get(), service_type_config.get())) {
        score::mw::log::LogError()
            << "[gatewayd] Invalid configuration passed to CreateAsyncRemoteService";
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }

    auto ipc_instance_specifier_result = score::mw::com::InstanceSpecifier::Create(
        service_instance_config->instance_specifier()->str());
    if (!ipc_instance_specifier_result.has_value()) {
        score::mw::log::LogError()
            << "[gatewayd] Invalid instance specifier '"
            << service_instance_config->instance_specifier()->string_view() << "'";
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }

    score::containers::NonRelocatableVector<score::mw::com::EventInfo> events(
        service_type_config->events()->size());

    auto const service_type_name = service_type_config->service_type_name()->string_view();

    for (const auto* event : *service_type_config->events()) {
        auto const event_name = event->event_name()->string_view();
        const score_com_serializer* serializer = nullptr;
        auto const get_result =
            score_com_serializer_get(service_type_name.data(), service_type_name.size(),
                                     score_com_serializer_element_type_event, event_name.data(),
                                     event_name.size(), &serializer);
        if ((get_result != score_com_serializer_result_ok) || (serializer == nullptr)) {
            score::mw::log::LogError() << "[gatewayd] Failed to get serializer for "
                                       << service_type_name << "::" << event_name;
            return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
        }

        score::mw::com::DataTypeMetaInfo const type_info{
            score_com_serializer_get_sizeof_type(serializer),
            score_com_serializer_get_alignof_type(serializer)};
        events.emplace_back(score::mw::com::EventInfo{event_name, type_info});
    }

    score::mw::com::GenericSkeletonServiceElementInfo service_element_info{};
    service_element_info.events = events;

    auto create_ipc_result = score::mw::com::GenericSkeleton::Create(*ipc_instance_specifier_result,
                                                                     service_element_info);
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
