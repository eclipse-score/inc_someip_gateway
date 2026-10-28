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

#include "score/someipd/impl/gatewayd_services.h"

#include <utility>

#include "score/bridged_services/bridged_service_configs.h"
#include "score/config/mw_someip_config_generated.h"
#include "score/mw/log/logging.h"

namespace score::someipd {

using score::bridged_services::bridged_service_configs;
using score::bridged_services::Daemon;

GatewaydServices::GatewaydServices(Routing& routing, socom::Runtime& socom_runtime) noexcept
    : routing_{routing}, socom_runtime_{socom_runtime} {}

bool GatewaydServices::Add(
    score::cpp::span<std::byte const> gateway_config,
    gateway_ipc_binding::mw_com::Add_bridged_services const& add_bridged_services) {
    // The services point into it.
    auto const buffer =
        std::make_shared<std::vector<std::byte>>(gateway_config.begin(), gateway_config.end());
    flatbuffers::Verifier verifier{reinterpret_cast<std::uint8_t const*>(buffer->data()),
                                   buffer->size()};
    if (!mw_someip_config::VerifyRootBuffer(verifier)) {
        score::mw::log::LogError()
            << "[someipd] Rejected a gateway configuration that is no mw_someip_config";
        return false;
    }
    std::shared_ptr<mw_someip_config::Root const> const config{
        buffer, mw_someip_config::GetRoot(buffer->data())};

    for (auto const* service_type_config : *config->service_types()) {
        // The Verifier only checks fields marked required. someipd uses these two as well.
        if ((service_type_config->service_type_name() == nullptr) ||
            (service_type_config->events() == nullptr)) {
            score::mw::log::LogError()
                << "[someipd] Rejected a gateway configuration: service 0x"
                << score::mw::log::LogHex16{service_type_config->service_id()}
                << " has no service_type_name or no events";
            return false;
        }
        if (service_ids_.count(service_type_config->service_id()) != 0U) {
            score::mw::log::LogError()
                << "[someipd] Rejected a gateway configuration: service 0x"
                << score::mw::log::LogHex16{service_type_config->service_id()}
                << " was added before";
            return false;
        }
    }

    if (!add_bridged_services(bridged_service_configs(*config, Daemon::someipd))) {
        return false;
    }

    NetworkServices added{};
    for (auto const* service_type_config : *config->service_types()) {
        std::shared_ptr<mw_someip_config::ServiceType const> const service_type{
            config, service_type_config};
        if ((service_type_config->local_service_instances() != nullptr) &&
            !service_type_config->local_service_instances()->empty()) {
            // TODO: handle multiple instances. Bridged services and LocalNetworkService
            // currently use service_type_name as the SOCom Service_instance (one connector
            // per service type); Routing::SetupOfferings still offers all instances on SOME/IP.
            auto const* const service_instance_config =
                *service_type_config->local_service_instances()->begin();
            score::mw::log::LogInfo()
                << "[someipd] Creating LocalNetworkService: "
                << service_type_config->service_type_name()->string_view() << " service_id=0x"
                << score::mw::log::LogHex16{service_type_config->service_id()} << " instance_id=0x"
                << score::mw::log::LogHex16{service_instance_config->instance_id()};
            auto created =
                LocalNetworkService::Create({config, service_instance_config}, service_type,
                                            routing_.get_application(), socom_runtime_);
            if (!created.has_value()) {
                score::mw::log::LogError()
                    << "[someipd] Failed to create LocalNetworkService for "
                    << service_type_config->service_type_name()->string_view();
                return false;
            }
            added.local.push_back(std::move(created).value());
        }
        if (service_type_config->remote_service_instances() != nullptr) {
            for (auto const* service_instance_config :
                 *service_type_config->remote_service_instances()) {
                score::mw::log::LogInfo()
                    << "[someipd] Creating RemoteNetworkService: "
                    << service_type_config->service_type_name()->string_view() << " service_id=0x"
                    << score::mw::log::LogHex16{service_type_config->service_id()}
                    << " instance_id=0x"
                    << score::mw::log::LogHex16{service_instance_config->instance_id()};
                auto created =
                    RemoteNetworkService::Create({config, service_instance_config}, service_type,
                                                 routing_.get_application(), socom_runtime_);
                if (!created.has_value()) {
                    score::mw::log::LogError()
                        << "[someipd] Failed to create RemoteNetworkService for "
                        << service_type_config->service_type_name()->string_view();
                    return false;
                }
                added.remote.push_back(std::move(created).value());
            }
        }
    }

    std::vector<RemoteNetworkService*> remote{};
    for (auto const& service : added.remote) {
        remote.push_back(service.get());
    }
    routing_.AddServices(config, [remote]() {
        for (auto* const service : remote) {
            service->setup_vsomeip();
        }
    });

    for (auto const* service_type_config : *config->service_types()) {
        service_ids_.insert(service_type_config->service_id());
    }
    network_services_.push_back(std::move(added));
    return true;
}

}  // namespace score::someipd
