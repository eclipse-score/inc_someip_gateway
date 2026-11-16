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

#include <cstddef>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

#include "score/mw/log/logging.h"
#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"
#include "score/someipd/impl/routing.h"

#if defined(SOMEIPD_STACK_OPENSOMEIP)
#include "opensomeip_host.h"
#else
#include <vsomeip/vsomeip.hpp>
#endif

namespace score::someipd {
namespace {

#if defined(SOMEIPD_STACK_OPENSOMEIP)
std::vector<std::uint16_t> eventgroup_ids_of(mw_someip_config::Event const& event) {
    std::vector<std::uint16_t> groups;
    auto const* const configured = event.eventgroup_ids();
    if ((configured != nullptr) && !configured->empty()) {
        for (auto const group_id : *configured) {
            groups.push_back(group_id);
        }
    } else {
        groups.push_back(event.event_id());
    }
    return groups;
}

::someip::platform::ByteBuffer payload_bytes(socom::Payload const& payload) {
    auto const full_msg = payload.data();
    auto const body = (full_msg.size() >= someip::kSomeipFullHeaderSize)
                          ? full_msg.subspan(someip::kSomeipFullHeaderSize)
                          : full_msg;
    ::someip::platform::ByteBuffer bytes(body.size());
    if (!body.empty()) {
        std::memcpy(bytes.data(), body.data(), body.size());
    }
    return bytes;
}
#endif

}  // namespace

RemoteNetworkService::RemoteNetworkService(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config, Routing& routing,
    socom::Enabled_server_connector::Uptr server_connector)
    : service_instance_config_(std::move(service_instance_config)),
      service_type_config_(std::move(service_type_config)),
      routing_(routing),
      server_connector_(std::move(server_connector)) {}

Result<std::unique_ptr<RemoteNetworkService>> RemoteNetworkService::Create(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config, Routing& routing,
    socom::Runtime& socom_runtime) {
    socom::Service_interface_identifier const iface{
        service_type_config->service_type_name()->string_view(),
        {service_type_config->service_version_major(),
         static_cast<uint16_t>(service_type_config->service_version_minor())}};

    socom::Service_instance const inst{service_type_config->service_type_name()->string_view()};

    // Methods stay unwired on the SOCom connector, matching gatewayd. The opensomeip path still
    // invokes the library when a method call arrives.
    auto const service_id = service_type_config->service_id();
    auto const instance_id = service_instance_config->instance_id();
    socom::Server_service_interface_definition const server_config{
        iface, socom::to_num_of_methods(0),
        socom::to_num_of_events(service_type_config->events()->size())};

    auto disabled = socom_runtime.make_server_connector(
        server_config, inst,
        {
            .on_method_call = [&routing, service_id, instance_id](
                                  socom::Enabled_server_connector&, socom::Method_id method_id,
                                  socom::Payload payload, socom::Method_call_reply_data_opt)
                -> socom::Method_invocation::Uptr {
#if defined(SOMEIPD_STACK_OPENSOMEIP)
                (void)routing.host().call_method(service_id, instance_id, method_id,
                                                 payload_bytes(payload));
#else
                static_cast<void>(routing);
                static_cast<void>(service_id);
                static_cast<void>(instance_id);
                static_cast<void>(method_id);
                static_cast<void>(payload);
#endif
                return nullptr;
            },
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
                                 routing, std::move(server_connector)));
    return instance;
}

bool RemoteNetworkService::accept_sender(std::string const& address, std::uint16_t port) {
    last_sender_address_ = address;
    last_sender_port_ = port;
    // The flatbuffer does not list peer addresses. The allow-list decision stays in someipd, and
    // the endpoint the stack reported is accepted.
    return true;
}

void RemoteNetworkService::deliver_network_event(NetworkEvent const& event) {
    if (!accept_sender(event.sender_address, event.sender_port)) {
        score::mw::log::LogError()
            << "[someipd] RemoteNetworkService - Dropping event " << event.socom_event_id
            << " from " << last_sender_address_ << ":" << last_sender_port_;
        return;
    }
    auto maybe_payload = server_connector_->allocate_event_payload(event.socom_event_id);
    if (!maybe_payload.has_value()) {
        return;
    }
    auto& payload = *maybe_payload;
    auto const* const data = event.payload;
    auto const size = event.payload_size;

    // Build SOME/IP header (16 bytes) + payload.
    // This matches what gatewayd expects when receiving events from someipd.
    auto const message = payload.wdata();
    if ((message.size() < someip::kSomeipFullHeaderSize) ||
        (size > message.size() - someip::kSomeipFullHeaderSize)) {
        score::mw::log::LogError()
            << "[someipd] RemoteNetworkService - Dropping event " << event.socom_event_id
            << " from " << last_sender_address_ << ":" << last_sender_port_ << ": payload of "
            << size << " bytes does not fit";
        return;
    }
    std::size_t pos = 0;

    message[pos++] = static_cast<std::byte>(event.service_id >> 8);
    message[pos++] = static_cast<std::byte>(event.service_id & 0xFF);

    message[pos++] = static_cast<std::byte>(event.event_id >> 8);
    message[pos++] = static_cast<std::byte>(event.event_id & 0xFF);

    // Length (4 bytes) - set to actual payload size + 8 (remaining header bytes)
    auto length = static_cast<std::uint32_t>(size + 8);
    message[pos++] = static_cast<std::byte>((length >> 24) & 0xFF);
    message[pos++] = static_cast<std::byte>((length >> 16) & 0xFF);
    message[pos++] = static_cast<std::byte>((length >> 8) & 0xFF);
    message[pos++] = static_cast<std::byte>(length & 0xFF);

    message[pos++] = static_cast<std::byte>(event.client_id >> 8);
    message[pos++] = static_cast<std::byte>(event.client_id & 0xFF);

    message[pos++] = static_cast<std::byte>(event.session_id >> 8);
    message[pos++] = static_cast<std::byte>(event.session_id & 0xFF);

    message[pos++] = static_cast<std::byte>(event.protocol_version);
    message[pos++] = static_cast<std::byte>(event.interface_version);
    message[pos++] = static_cast<std::byte>(event.message_type);
    message[pos++] = static_cast<std::byte>(event.return_code);

    if ((data != nullptr) && (size > 0)) {
        std::memcpy(message.data() + pos, data, size);
    }
    pos += size;

    payload.shrink(pos);
    if (const auto result =
            server_connector_->update_event(event.socom_event_id, std::move(payload));
        !result) {
        score::mw::log::LogError() << "[someipd] RemoteNetworkService - Failed to update event "
                                   << event.socom_event_id << " from " << last_sender_address_
                                   << ":" << last_sender_port_ << ": " << result.error().Message();
    }
}

void RemoteNetworkService::Setup() {
#if defined(SOMEIPD_STACK_OPENSOMEIP)
    RemoteServiceRequest request;
    request.service_id = service_type_config_->service_id();
    request.instance_id = service_instance_config_->instance_id();
    request.major_version = service_type_config_->service_version_major();
    request.minor_version = service_type_config_->service_version_minor();
    std::set<std::uint16_t> groups;
    for (std::size_t i = 0; i < service_type_config_->events()->size(); ++i) {
        auto const* const event_config = (*service_type_config_->events())[i];
        request.event_ids.push_back(event_config->event_id());
        for (auto const group_id : eventgroup_ids_of(*event_config)) {
            groups.insert(group_id);
        }
    }
    request.eventgroup_ids.assign(groups.begin(), groups.end());
    if (service_type_config_->methods() != nullptr) {
        for (auto const* const method : *service_type_config_->methods()) {
            request.method_ids.push_back(method->method_id());
        }
    }
    request.on_event = [this](ObservedEvent const& observed,
                              ::someip::platform::ByteBuffer const& payload,
                              ObservedSender const& sender) {
        auto const* const events = service_type_config_->events();
        std::size_t socom_index = events->size();
        for (std::size_t i = 0; i < events->size(); ++i) {
            if ((*events)[i]->event_id() == observed.event_id) {
                socom_index = i;
                break;
            }
        }
        if (socom_index >= events->size()) {
            score::mw::log::LogError()
                << "[someipd] RemoteNetworkService - Dropping unknown event 0x"
                << score::mw::log::LogHex16{observed.event_id} << " from " << sender.address << ":"
                << sender.port;
            return;
        }
        NetworkEvent event;
        event.socom_event_id = static_cast<socom::Event_id>(socom_index);
        event.service_id = observed.service_id;
        event.event_id = observed.event_id;
        event.client_id = observed.client_id;
        event.session_id = observed.session_id;
        event.protocol_version = observed.protocol_version;
        event.interface_version = observed.interface_version;
        event.message_type = observed.message_type;
        event.return_code = observed.return_code;
        event.payload = payload.data();
        event.payload_size = payload.size();
        event.sender_address = sender.address;
        event.sender_port = sender.port;
        deliver_network_event(event);
    };
    routing_.host().subscribe_remote(std::move(request));
#else
    auto const service_id = service_type_config_->service_id();
    auto const instance_id = service_instance_config_->instance_id();
    auto const application = routing_.get_application();

    application->request_service(service_id, instance_id);

    for (std::size_t i = 0; i < service_type_config_->events()->size(); ++i) {
        auto const* const event_config = (*service_type_config_->events())[i];
        auto const socom_event_id = static_cast<socom::Event_id>(i);
        auto const vsomeip_event_id = event_config->event_id();

        application->register_message_handler(
            service_id, instance_id, vsomeip_event_id,
            [this, socom_event_id, vsomeip_event_id](const std::shared_ptr<vsomeip::message>& msg) {
                NetworkEvent event;
                event.socom_event_id = socom_event_id;
                event.service_id = msg->get_service();
                event.event_id = vsomeip_event_id;
                event.client_id = msg->get_client();
                event.session_id = msg->get_session();
                event.protocol_version = msg->get_protocol_version();
                event.interface_version = msg->get_interface_version();
                event.message_type = static_cast<std::uint8_t>(msg->get_message_type());
                event.return_code = static_cast<std::uint8_t>(msg->get_return_code());
                event.payload = msg->get_payload()->get_data();
                event.payload_size = static_cast<std::size_t>(msg->get_payload()->get_length());
                deliver_network_event(event);
            });

        // TODO: Do Eventgroup handling. Currently just create one group per event with the same ID.
        std::set<vsomeip::eventgroup_t> groups{vsomeip_event_id};
        application->request_event(service_id, instance_id, vsomeip_event_id, groups);
        application->subscribe(service_id, instance_id, vsomeip_event_id,
                               service_type_config_->service_version_major());
    }
#endif
}

}  // namespace score::someipd
