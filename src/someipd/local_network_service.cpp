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

#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"

namespace score::someipd {

LocalNetworkService::LocalNetworkService(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    std::shared_ptr<vsomeip::application> vsomeip_app, socom::Runtime& socom_runtime)
    : service_instance_config_(std::move(service_instance_config)),
      service_type_config_(std::move(service_type_config)),
      vsomeip_app_(std::move(vsomeip_app)) {
    socom::Service_interface_identifier const iface{
        service_type_config_->service_type_name()->string_view(),
        {service_type_config_->service_version_major(),
         static_cast<uint16_t>(service_type_config_->service_version_minor())}};

    // TODO: handle multiple instances. For now just expect one instance per service.
    socom::Service_instance const inst{service_type_config_->service_type_name()->string_view()};

    socom::Service_interface_definition const client_connector_config{
        iface, socom::to_num_of_methods(0),
        socom::to_num_of_events(service_type_config_->events()->size())};

    // Callbacks capture `this`. on_service_state_change fires only after make_client_connector
    // returns (initial state is always not_available), so client_connector_ is set by then.
    auto connector_result = socom_runtime.make_client_connector(
        client_connector_config, inst,
        {
            .on_service_state_change =
                [this](socom::Client_connector const&, socom::Service_state state,
                       socom::Server_service_interface_definition const&) {
                    std::cout << "[someipd] LocalNetworkService - on_service_state_change called"
                              << std::endl;
                    if (state != socom::Service_state::available) {
                        return;
                    }
                    // Subscribe to all events
                    socom::Event_id socom_event_id{0};
                    for (auto event : *service_type_config_->events()) {
                        std::cout << "[someipd] LocalNetworkService - Subscribing to event "
                                  << event->event_name()->string_view() << " (socom_id "
                                  << socom_event_id << ") for service "
                                  << service_type_config_->service_type_name()->string_view()
                                  << std::endl;
                        (void)client_connector_->subscribe_event(socom_event_id,
                                                                 socom::Event_mode::update);
                        ++socom_event_id;
                    }
                },
            .on_event_update =
                [this](socom::Client_connector const&, socom::Event_id event_id,
                       socom::Payload payload) {
                    std::cout << "[someipd] LocalNetworkService - on_event_update for event_id "
                              << event_id << " with payload size " << payload.data().size()
                              << std::endl;
                    forward_to_vsomeip(event_id, std::move(payload));
                },
            .on_event_requested_update =
                [](socom::Client_connector const&, socom::Event_id, socom::Payload) {
                    // There should be no need for "on-demand event updates"
                },
            .on_event_payload_allocate =
                [](socom::Client_connector const&,
                   socom::Event_id) -> score::Result<socom::Writable_payload> {
                // Payload allocation is handled by the IPC binding (read-only SHM slot from
                // gatewayd). This callback is never called in normal operation.
                assert(false && "on_event_payload_allocate must not be called on LocalNetworkService");
                return MakeUnexpected(socom::Error::logic_error_id_out_of_range);
            },
        });

    if (!connector_result.has_value()) {
        std::cerr << "[someipd] Failed to create client connector for '"
                  << service_type_config_->service_type_name()->string_view() << "'\n";
        return;
    }
    client_connector_ = std::move(connector_result).value();
}

void LocalNetworkService::forward_to_vsomeip(socom::Event_id event_id, socom::Payload payload) {
    auto const event_index = static_cast<std::size_t>(event_id);
    auto const* const events = service_type_config_->events();
    if (event_index >= events->size()) {
        std::cerr << "[someipd] event_id " << event_index << " out of range, dropping\n";
        return;
    }
    auto const* const event_config = (*events)[event_index];

    auto const full_msg = payload.data();
    // Drop the SOME/IP header part, fields are set via config lookup
    auto const event_data = full_msg.subspan(someip::kSomeipFullHeaderSize, full_msg.size() - someip::kSomeipFullHeaderSize);

    auto vsomeip_payload = vsomeip::runtime::get()->create_payload();
    vsomeip_payload->set_data(reinterpret_cast<const vsomeip_v3::byte_t*>(event_data.data()),
                              static_cast<vsomeip_v3::length_t>(event_data.size()));

    vsomeip_app_->notify(service_type_config_->service_id(),
                         service_instance_config_->instance_id(), event_config->event_id(),
                         vsomeip_payload);

    std::cout << "[someipd] Forwarded SOCom event " << event_index << " (vsomeip event_id=0x"
              << std::hex << event_config->event_id() << std::dec
              << ") to SOME/IP: event_data=" << event_data.size() << "B\n";
}

void LocalNetworkService::Create(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    std::shared_ptr<vsomeip::application> vsomeip_app, socom::Runtime& socom_runtime,
    std::vector<std::unique_ptr<LocalNetworkService>>& instances) {
    if (service_instance_config == nullptr) {
        std::cerr << "[someipd] ERROR: Service instance config is nullptr!\n";
        return;
    }
    instances.push_back(std::make_unique<LocalNetworkService>(
        service_instance_config, service_type_config, vsomeip_app, socom_runtime));
    std::cout << "[someipd] LocalNetworkService created for service 0x" << std::hex
              << service_type_config->service_id() << " instance 0x"
              << service_instance_config->instance_id() << std::dec << "\n";
}

}  // namespace score::someipd
