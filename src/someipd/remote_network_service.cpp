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

#include "remote_network_service.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>

#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"

namespace score::someipd {

RemoteNetworkService::RemoteNetworkService(
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

    socom::Service_instance const inst{service_type_config_->service_type_name()->string_view()};

    socom::Server_service_interface_definition const server_config{
        iface, socom::to_num_of_methods(0),
        socom::to_num_of_events(service_type_config_->events()->size())};

    auto disabled = socom_runtime.make_server_connector(
        server_config, inst,
        {
            .on_method_call = [](socom::Enabled_server_connector&, socom::Method_id, socom::Payload,
                                 socom::Method_call_reply_data_opt, socom::Posix_credentials const&)
                -> socom::Method_invocation::Uptr { return nullptr; },
            .on_event_subscription_change = [](socom::Enabled_server_connector&, socom::Event_id,
                                               socom::Event_state) {},
            .on_event_update_request = [](socom::Enabled_server_connector&, socom::Event_id) {},
            .on_method_call_payload_allocate =
                [](socom::Enabled_server_connector&,
                   socom::Method_id) -> score::Result<socom::Writable_payload> {
                return MakeUnexpected(socom::Error::logic_error_id_out_of_range);
            },
        });

    if (!disabled.has_value()) {
        std::cerr << "[someipd] Failed to create server connector for '"
                  << service_type_config_->service_type_name()->string_view() << "'\n";
        return;
    }
    server_connector_ = socom::Disabled_server_connector::enable(std::move(disabled).value());
}

void RemoteNetworkService::setup_vsomeip() {
    auto const service_id = service_type_config_->service_id();
    auto const instance_id = service_instance_config_->instance_id();

    vsomeip_app_->request_service(service_id, instance_id);

    for (std::size_t i = 0; i < service_type_config_->events()->size(); ++i) {
        auto const* const event_config = (*service_type_config_->events())[i];
        auto const socom_event_id = static_cast<socom::Event_id>(i);
        auto const vsomeip_event_id = event_config->event_id();

        vsomeip_app_->register_message_handler(
            service_id, instance_id, vsomeip_event_id,
            [this, socom_event_id](const std::shared_ptr<vsomeip::message>& msg) {
                auto maybe_payload = server_connector_->allocate_event_payload(socom_event_id);
                if (!maybe_payload.has_value()) {
                    return;
                }
                auto& payload = *maybe_payload;
                auto const* const data = msg->get_payload()->get_data();
                auto const size = static_cast<std::size_t>(msg->get_payload()->get_length());
                // Shrink payload to actual size
                payload.shrink(size);
                std::memcpy(payload.wdata().data(), data, size);
                server_connector_->update_event(socom_event_id, std::move(payload));

                std::cout << "[someipd] Forwarded SOME/IP event 0x" << std::hex << msg->get_method()
                          << std::dec << " to SOCom: payload=" << size << "B\n";
            });

        // TODO: Do Eventgroup handling. Currently just create one group per event with the same ID.
        std::set<vsomeip::eventgroup_t> groups{vsomeip_event_id};
        vsomeip_app_->request_event(service_id, instance_id, vsomeip_event_id, groups);
        vsomeip_app_->subscribe(service_id, instance_id, vsomeip_event_id);
    }
}

void RemoteNetworkService::Create(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    std::shared_ptr<vsomeip::application> vsomeip_app, socom::Runtime& socom_runtime,
    std::vector<std::unique_ptr<RemoteNetworkService>>& instances) {
    if (service_instance_config == nullptr) {
        std::cerr << "[someipd] ERROR: Service instance config is nullptr!\n";
        return;
    }
    instances.push_back(std::make_unique<RemoteNetworkService>(
        service_instance_config, service_type_config, vsomeip_app, socom_runtime));
    std::cout << "[someipd] RemoteNetworkService created for service 0x" << std::hex
              << service_type_config->service_id() << " instance 0x"
              << service_instance_config->instance_id() << std::dec << "\n";
}

}  // namespace score::someipd
