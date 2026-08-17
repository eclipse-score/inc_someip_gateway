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
#include <cstddef>
#include <cstring>
#include <iostream>
#include <set>

#include "score/mw/log/logging.h"
#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"

namespace score::someipd {

RemoteNetworkService::RemoteNetworkService(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    std::shared_ptr<vsomeip::application> vsomeip_app,
    socom::Enabled_server_connector::Uptr server_connector)
    : service_instance_config_(std::move(service_instance_config)),
      service_type_config_(std::move(service_type_config)),
      vsomeip_app_(std::move(vsomeip_app)),
      server_connector_(std::move(server_connector)) {}

Result<std::unique_ptr<RemoteNetworkService>> RemoteNetworkService::Create(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    std::shared_ptr<vsomeip::application> vsomeip_app, socom::Runtime& socom_runtime) {
    socom::Service_interface_identifier const iface{
        service_type_config->service_type_name()->string_view(),
        {service_type_config->service_version_major(),
         static_cast<uint16_t>(service_type_config->service_version_minor())}};

    socom::Service_instance const inst{service_type_config->service_type_name()->string_view()};

    socom::Server_service_interface_definition const server_config{
        iface, socom::to_num_of_methods(0),
        socom::to_num_of_events(service_type_config->events()->size())};

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
        score::mw::log::LogError()
            << "[someipd] Failed to create server connector for '"
            << service_type_config->service_type_name()->string_view() << "'";
        return MakeUnexpected(socom::Error::runtime_error_request_rejected);
    }
    auto server_connector = socom::Disabled_server_connector::enable(std::move(disabled).value());

    auto instance = std::unique_ptr<RemoteNetworkService>(
        new RemoteNetworkService(std::move(service_instance_config), std::move(service_type_config),
                                 std::move(vsomeip_app), std::move(server_connector)));
    return instance;
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
            [this, socom_event_id, vsomeip_event_id](const std::shared_ptr<vsomeip::message>& msg) {
                auto maybe_payload = server_connector_->allocate_event_payload(socom_event_id);
                if (!maybe_payload.has_value()) {
                    return;
                }
                auto& payload = *maybe_payload;
                auto const* const data = msg->get_payload()->get_data();
                auto const size = static_cast<std::size_t>(msg->get_payload()->get_length());

                // Build SOME/IP header (16 bytes) + payload
                // This matches what gatewayd expects when receiving events from someipd.
                // TODO: Check if this is actually required. E2E?
                std::size_t pos = 0;

                std::uint16_t service_id_val = msg->get_service();
                payload.wdata()[pos++] = static_cast<std::byte>(service_id_val >> 8);
                payload.wdata()[pos++] = static_cast<std::byte>(service_id_val & 0xFF);

                std::uint16_t method_id = vsomeip_event_id;
                payload.wdata()[pos++] = static_cast<std::byte>(method_id >> 8);
                payload.wdata()[pos++] = static_cast<std::byte>(method_id & 0xFF);

                // Length (4 bytes) - set to actual payload size + 8 (remaining header bytes)
                std::uint32_t length = static_cast<std::uint32_t>(size + 8);
                payload.wdata()[pos++] = static_cast<std::byte>((length >> 24) & 0xFF);
                payload.wdata()[pos++] = static_cast<std::byte>((length >> 16) & 0xFF);
                payload.wdata()[pos++] = static_cast<std::byte>((length >> 8) & 0xFF);
                payload.wdata()[pos++] = static_cast<std::byte>(length & 0xFF);

                std::uint16_t client_id = msg->get_client();
                payload.wdata()[pos++] = static_cast<std::byte>(client_id >> 8);
                payload.wdata()[pos++] = static_cast<std::byte>(client_id & 0xFF);

                std::uint16_t session_id = msg->get_session();
                payload.wdata()[pos++] = static_cast<std::byte>(session_id >> 8);
                payload.wdata()[pos++] = static_cast<std::byte>(session_id & 0xFF);

                std::uint8_t protocol_version =
                    static_cast<std::uint8_t>(msg->get_protocol_version());
                payload.wdata()[pos++] = static_cast<std::byte>(protocol_version);

                std::uint8_t interface_version =
                    static_cast<std::uint8_t>(msg->get_interface_version());
                payload.wdata()[pos++] = static_cast<std::byte>(interface_version);

                std::uint8_t message_type = static_cast<std::uint8_t>(msg->get_message_type());
                payload.wdata()[pos++] = static_cast<std::byte>(message_type);

                std::uint8_t return_code = static_cast<std::uint8_t>(msg->get_return_code());
                payload.wdata()[pos++] = static_cast<std::byte>(return_code);

                // Copy payload data after header
                std::memcpy(payload.wdata().data() + pos, data, size);
                pos += size;

                // Shrink to actual size (header + payload)
                payload.shrink(pos);
                server_connector_->update_event(socom_event_id, std::move(payload));
            });

        // TODO: Do Eventgroup handling. Currently just create one group per event with the same ID.
        std::set<vsomeip::eventgroup_t> groups{vsomeip_event_id};
        vsomeip_app_->request_event(service_id, instance_id, vsomeip_event_id, groups);
        vsomeip_app_->subscribe(service_id, instance_id, vsomeip_event_id);
    }
}

}  // namespace score::someipd
