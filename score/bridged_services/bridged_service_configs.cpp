/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
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

#include "bridged_service_configs.h"

#include <string>
#include <utility>
#include <vector>

#include "score/someip/constants.h"

namespace score::bridged_services {

namespace mw_com = gateway_ipc_binding::mw_com;

mw_com::Service_configs bridged_service_configs(mw_someip_config::Root const& config,
                                                Daemon const daemon,
                                                Event_size_strategy const& event_size) {
    auto const local_role =
        (daemon == Daemon::gatewayd) ? mw_com::Role::provider : mw_com::Role::consumer;
    auto const remote_role =
        (daemon == Daemon::gatewayd) ? mw_com::Role::consumer : mw_com::Role::provider;

    mw_com::Service_configs services;
    for (auto const* service_type_config : *config.service_types()) {
        mw_com::Role role{};
        if (service_type_config->local_service_instances() != nullptr &&
            !service_type_config->local_service_instances()->empty()) {
            role = local_role;
        } else if (service_type_config->remote_service_instances() != nullptr &&
                   !service_type_config->remote_service_instances()->empty()) {
            role = remote_role;
        } else {
            continue;
        }
        // The binding rejects services without events.
        if (service_type_config->events() == nullptr || service_type_config->events()->empty()) {
            continue;
        }

        auto const service_type_name = service_type_config->service_type_name()->string_view();
        socom::Service_interface_identifier::Version const version{
            service_type_config->service_version_major(),
            static_cast<std::uint16_t>(service_type_config->service_version_minor())};
        std::vector<mw_com::Event_config> events;
        for (auto const* event_config : *service_type_config->events()) {
            auto const event_name = event_config->event_name()->string_view();
            std::size_t const max_payload_size =
                (event_size != nullptr) ? event_size(service_type_name, event_name) : 0U;
            events.push_back({.name = std::string{event_name},
                              .id = mw_com::Mw_com_event_id{event_config->event_id()},
                              .max_payload_size = max_payload_size});
        }

        services.push_back(
            {.interface = {service_type_name, version},
             .instance = socom::Service_instance{service_type_name},
             .instance_specifier = std::string{"bridged_ipc/"}.append(service_type_name),
             .service_id = service_type_config->service_id(),
             .instance_id = config.ipc_instance_id(),
             .role = role,
             .events = std::move(events),
             .max_sample_count = someip::kMaxSampleCount});
    }
    return services;
}

}  // namespace score::bridged_services
