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

#include "local_service_instance.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>

#include "score/mw/com/com_error_domain.h"
#include "score/mw/com/types.h"
#include "score/mw/log/logging.h"
#include "score/socom/runtime.hpp"
#include "score/someip/constants.h"
#include "src/serializer/serializer.h"

using score::mw::com::GenericProxy;
using score::mw::com::SamplePtr;

namespace score::someip_gateway::gatewayd {

LocalServiceInstance::LocalServiceInstance(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    GenericProxy&& ipc_proxy, socom::Enabled_server_connector::Uptr server_connector)
    : service_instance_config_(std::move(service_instance_config)),
      service_type_config_(std::move(service_type_config)),
      ipc_proxy_(std::move(ipc_proxy)),
      server_connector_(std::move(server_connector)) {}

Result<std::unique_ptr<LocalServiceInstance>> LocalServiceInstance::Create(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    GenericProxy&& ipc_proxy, socom::Runtime& socom_runtime) {
    socom::Service_interface_identifier const iface{
        service_type_config->service_type_name()->string_view(),
        {service_type_config->service_version_major(),
         static_cast<uint16_t>(service_type_config->service_version_minor())}};

    // TODO: Handle multiple instances. Needs to be converted from integer ID to string.
    // For initial impl, just use service name again.
    socom::Service_instance const inst{service_type_config->service_type_name()->string_view()};

    socom::Server_service_interface_definition const server_config{
        iface, socom::to_num_of_methods(0),
        socom::to_num_of_events(service_type_config->events()->size())};

    auto disabled_server_connector = socom_runtime.make_server_connector(
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
                return MakeUnexpected(socom::Error::runtime_error_request_rejected);
            },
        });

    if (!disabled_server_connector.has_value()) {
        score::mw::log::LogError()
            << "[gatewayd] Failed to create server connector for '"
            << service_type_config->service_type_name()->string_view() << "'";
        return MakeUnexpected(socom::Error::runtime_error_request_rejected);
    }
    std::cout << "[gatewayd] LocalServiceInstance - Enabled server_connector for "
              << service_type_config->service_type_name()->string_view() << std::endl;
    auto server_connector =
        socom::Disabled_server_connector::enable(std::move(disabled_server_connector).value());

    // Create the instance
    auto instance = std::unique_ptr<LocalServiceInstance>(
        new LocalServiceInstance(std::move(service_instance_config), std::move(service_type_config),
                                 std::move(ipc_proxy), std::move(server_connector)));

    // Set up IPC event handlers (must be done after instance creation since handlers capture this)
    auto events = instance->ipc_proxy_.GetEvents();
    socom::Event_id socom_event_id{0U};
    for (auto event_config : *instance->service_type_config_->events()) {
        auto result = events.find(event_config->event_name()->string_view());
        if (result == events.cend()) {
            score::mw::log::LogWarn()
                << "[gatewayd] Failed to find " << event_config->event_name()->string_view()
                << " event in generic ipc_proxy.";
            ++socom_event_id;
            continue;
        }
        auto& ipc_event = result->second;

        auto service_type_name = instance->service_type_config_->service_type_name()->string_view();
        auto event_name = event_config->event_name()->string_view();
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
        auto& event_context =
            instance->event_contexts_
                .emplace(event_name, EventContext{event_config, serializer, socom_event_id})
                .first->second;

        ipc_event.SetReceiveHandler([instance_ptr = instance.get(), &ipc_event, &event_context]() {
            ipc_event.GetNewSamples(
                [instance_ptr, &event_context](SamplePtr<void> sample) {
                    auto maybe_payload = instance_ptr->server_connector_->allocate_event_payload(
                        event_context.socom_event_id);
                    if (!maybe_payload.has_value()) {
                        std::cout << "[gatewayd] LocalServiceInstance - Failed to allocate event "
                                     "payload for event "
                                  << event_context.socom_event_id << ": "
                                  << maybe_payload.error().Message() << std::endl;
                        return;
                    }
                    // Writable_payload was constructed with header_size = 0.
                    // Therefore use .data()[0] to write the header first.
                    auto& payload = *maybe_payload;

                    std::size_t pos = 0;

                    // TODO: Design decision: the gateway needs to generate the SOME/IP message
                    // including the header in order to have the E2E protection in the ASIL
                    // context.
                    std::uint16_t service_id = instance_ptr->service_type_config_->service_id();
                    payload.wdata()[pos++] = static_cast<std::byte>(service_id >> 8);
                    payload.wdata()[pos++] = static_cast<std::byte>(service_id & 0xFF);

                    std::uint16_t method_id = event_context.config->event_id();
                    payload.wdata()[pos++] = static_cast<std::byte>(method_id >> 8);
                    payload.wdata()[pos++] = static_cast<std::byte>(method_id & 0xFF);

                    // Length set by someipd
                    pos += 4;

                    // TODO: get client ID during registration at the someipd
                    std::uint16_t client_id = 0xFFFF;
                    payload.wdata()[pos++] = static_cast<std::byte>(client_id >> 8);
                    payload.wdata()[pos++] = static_cast<std::byte>(client_id & 0xFF);

                    std::uint16_t session_id = 0x0000;
                    payload.wdata()[pos++] = static_cast<std::byte>(session_id >> 8);
                    payload.wdata()[pos++] = static_cast<std::byte>(session_id & 0xFF);

                    std::uint8_t protocol_version = 1;
                    payload.wdata()[pos++] = static_cast<std::byte>(protocol_version);

                    std::uint8_t interface_version =
                        instance_ptr->service_type_config_->service_version_major();
                    payload.wdata()[pos++] = static_cast<std::byte>(interface_version);

                    std::uint8_t message_type = 0x02;  // NOTIFICATION
                    payload.wdata()[pos++] = static_cast<std::byte>(message_type);

                    std::uint8_t return_code = 0x00;  // Unused
                    payload.wdata()[pos++] = static_cast<std::byte>(return_code);

                    std::size_t written_length = 0;
                    auto serialize_result = score_com_serializer_serialize(
                        event_context.serializer,
                        reinterpret_cast<uint8_t*>(payload.wdata().data() + pos),
                        payload.wdata().size() - pos, sample.get(), &written_length);
                    if (serialize_result != score_com_serializer_result_ok) {
                        score::mw::log::LogError()
                            << "[gatewayd] Serialization failed for "
                            << event_context.config->event_name()->string_view();
                        return;
                    }
                    pos += written_length;
                    // Shrink the payload to the actual written size (header + serialized data)
                    payload.shrink(pos);

                    instance_ptr->server_connector_->update_event(event_context.socom_event_id,
                                                                  std::move(payload));
                },
                someip::kMaxSampleCount);
        });

        ipc_event.Subscribe(someip::kMaxSampleCount);
        ++socom_event_id;
    }

    return instance;
}

namespace {
struct FindServiceContext {
    std::shared_ptr<const mw_someip_config::ServiceInstance> config;
    std::shared_ptr<const mw_someip_config::ServiceType> service_config;
    socom::Runtime* socom_runtime;
    std::vector<std::unique_ptr<LocalServiceInstance>>& instances;

    FindServiceContext(std::shared_ptr<const mw_someip_config::ServiceInstance> config_,
                       std::shared_ptr<const mw_someip_config::ServiceType> service_config_,
                       socom::Runtime& socom_runtime_,
                       std::vector<std::unique_ptr<LocalServiceInstance>>& instances_)
        : config(std::move(config_)),
          service_config(std::move(service_config_)),
          socom_runtime(&socom_runtime_),
          instances(instances_) {}
};

}  // namespace

Result<mw::com::FindServiceHandle> LocalServiceInstance::CreateAsyncLocalServices(
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
    socom::Runtime& socom_runtime, std::vector<std::unique_ptr<LocalServiceInstance>>& instances) {
    if (service_instance_config == nullptr) {
        score::mw::log::LogError() << "[gatewayd] ERROR: Service instance config is nullptr!";
        return MakeUnexpected(score::mw::com::ComErrc::kInvalidConfiguration);
    }

    // TODO: Error handling for instance specifier creation
    auto instance_specifier = score::mw::com::InstanceSpecifier::Create(
                                  service_instance_config->instance_specifier()->str())
                                  .value();

    // TODO: StartFindService should be modified to handle arbitrarily large lambdas
    // or we need to check whether it is OK to stick with dynamic allocation here.
    auto context = std::make_unique<FindServiceContext>(
        service_instance_config, service_type_config, socom_runtime, instances);

    return GenericProxy::StartFindService(
        [context = std::move(context)](auto handles, auto find_handle) {
            auto& instance_config = context->config;
            auto& service_config = context->service_config;

            auto proxy_result = GenericProxy::Create(handles.front());
            if (!proxy_result.has_value()) {
                score::mw::log::LogError()
                    << "[gatewayd] Proxy creation failed for instance specifier: "
                    << instance_config->instance_specifier()->string_view()
                    << "': " << proxy_result.error().Message();
                return;
            }

            auto create_result = LocalServiceInstance::Create(instance_config, service_config,
                                                              std::move(proxy_result).value(),
                                                              *context->socom_runtime);
            if (!create_result.has_value()) {
                score::mw::log::LogError()
                    << "[gatewayd] Failed to create LocalServiceInstance for '"
                    << instance_config->instance_specifier()->string_view() << "'";
                return;
            }

            // TODO: Add mutex if callbacks can run concurrently or use futures
            context->instances.push_back(std::move(create_result).value());

            std::cout << "[gatewayd] LocalServiceInstance created for instance specifier: "
                      << instance_config->instance_specifier()->string_view() << std::endl;

            GenericProxy::StopFindService(find_handle);
        },
        instance_specifier);
}

}  // namespace score::someip_gateway::gatewayd
