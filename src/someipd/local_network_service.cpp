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

#include "local_network_service.h"

#include <cstddef>
#include <iostream>
#include <memory>

#include "score/mw/log/logging.h"
#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"

namespace score::someipd {

LocalNetworkService::LocalNetworkService(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    std::shared_ptr<vsomeip::application> vsomeip_app,
    socom::Client_connector::Uptr client_connector)
    : service_instance_config_(std::move(service_instance_config)),
      service_type_config_(std::move(service_type_config)),
      vsomeip_app_(std::move(vsomeip_app)),
      client_connector_(std::move(client_connector)) {}

Result<std::unique_ptr<LocalNetworkService>> LocalNetworkService::Create(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    std::shared_ptr<vsomeip::application> vsomeip_app, socom::Runtime& socom_runtime) {
    // Create instance first with null connector - needed because callbacks capture the instance
    // pointer. Callbacks are not invoked until after make_client_connector returns (initial state
    // is always not_available), so this is safe.
    auto instance = std::unique_ptr<LocalNetworkService>(new LocalNetworkService(
        service_instance_config, service_type_config, std::move(vsomeip_app), nullptr));

    socom::Service_interface_identifier const iface{
        service_type_config->service_type_name()->string_view(),
        {service_type_config->service_version_major(),
         static_cast<uint16_t>(service_type_config->service_version_minor())}};

    // TODO: handle multiple instances. For now just expect one instance per service.
    socom::Service_instance const inst{service_type_config->service_type_name()->string_view()};

    socom::Service_interface_definition const client_connector_config{
        iface, socom::to_num_of_methods(0),
        socom::to_num_of_events(service_type_config->events()->size())};

    auto connector_result = socom_runtime.make_client_connector(
        client_connector_config, inst,
        {
            .on_service_state_change =
                [instance_ptr = instance.get()](socom::Client_connector const&,
                                                socom::Service_state state,
                                                socom::Server_service_interface_definition const&) {
                    std::cout << "[someipd] LocalNetworkService - on_service_state_change called"
                              << std::endl;
                    if (state != socom::Service_state::available) {
                        return;
                    }
                    // Subscribe to all events
                    socom::Event_id socom_event_id{0};
                    for (auto event : *instance_ptr->service_type_config_->events()) {
                        std::cout << "[someipd] LocalNetworkService - Subscribing to event "
                                  << event->event_name()->string_view() << " (socom_id "
                                  << socom_event_id << ") for service "
                                  << instance_ptr->service_type_config_->service_type_name()
                                         ->string_view()
                                  << std::endl;
                        (void)instance_ptr->client_connector_->subscribe_event(
                            socom_event_id, socom::Event_mode::update);
                        ++socom_event_id;
                    }
                },
            .on_event_update =
                [instance_ptr = instance.get()](socom::Client_connector const&,
                                                socom::Event_id event_id, socom::Payload payload) {
                    instance_ptr->forward_to_vsomeip(event_id, std::move(payload));
                },
            .on_event_requested_update =
                [](socom::Client_connector const&, socom::Event_id, socom::Payload) {
                    // There should be no need for "on-demand event updates"
                },
            .on_event_payload_allocate = [](socom::Client_connector const&, socom::Event_id)
                -> score::Result<socom::Writable_payload> {
                // Payload allocation is handled by the IPC binding (read-only SHM slot from
                // gatewayd). This callback is never called in normal operation.
                assert(false &&
                       "on_event_payload_allocate must not be called on LocalNetworkService");
                return MakeUnexpected(socom::Error::logic_error_id_out_of_range);
            },
        });

    if (!connector_result.has_value()) {
        score::mw::log::LogError()
            << "[someipd] Failed to create client connector for '"
            << service_type_config->service_type_name()->string_view() << "'";
        return MakeUnexpected(socom::Error::runtime_error_request_rejected);
    }
    instance->client_connector_ = std::move(connector_result).value();
    return instance;
}

void LocalNetworkService::forward_to_vsomeip(socom::Event_id event_id, socom::Payload payload) {
    auto const event_index = static_cast<std::size_t>(event_id);
    auto const* const events = service_type_config_->events();
    if (event_index >= events->size()) {
        score::mw::log::LogError()
            << "[someipd] event_id " << event_index << " out of range, dropping";
        return;
    }
    auto const* const event_config = (*events)[event_index];

    auto const full_msg = payload.data();
    // Drop the SOME/IP header part, fields are set via config lookup
    auto const event_data = full_msg.subspan(someip::kSomeipFullHeaderSize,
                                             full_msg.size() - someip::kSomeipFullHeaderSize);

    auto vsomeip_payload = vsomeip::runtime::get()->create_payload();
    vsomeip_payload->set_data(reinterpret_cast<const vsomeip_v3::byte_t*>(event_data.data()),
                              static_cast<vsomeip_v3::length_t>(event_data.size()));

    vsomeip_app_->notify(service_type_config_->service_id(),
                         service_instance_config_->instance_id(), event_config->event_id(),
                         vsomeip_payload);
}

}  // namespace score::someipd
