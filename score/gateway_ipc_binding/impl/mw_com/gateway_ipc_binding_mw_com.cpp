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

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"

#include <utility>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/mw/log/logging.h"
#include "someipd_service_binding.hpp"

namespace score::gateway_ipc_binding::mw_com {

namespace {

/// \brief Log the bridged services that are configured but not transported yet
/// \details Bridged services are the next implementation step. Until then the configuration is
///          accepted so that callers can already be written against the final signature, but it is
///          reported so that a silently dead link is not mistaken for a working one.
void warn_about_unsupported_services(Service_configs const& services,
                                     std::string_view const context) noexcept {
    if (services.empty()) {
        return;
    }
    score::mw::log::LogWarn() << "[gateway_ipc_binding]" << context << "received" << services.size()
                              << "bridged service configuration(s). Bridged services are not "
                                 "transported yet, only SomeipdService peer liveness is.";
}

/// \brief Client flavour of the mw::com binding
/// \details Consumes SomeipdService and reports it through is_connected(). The SOCom runtime and
///          the bridged service configuration are kept for the upcoming bridged-service support.
class Client_adapter final : public Gateway_ipc_binding_client {
   public:
    Client_adapter(score::socom::Runtime& runtime,
                   std::unique_ptr<Someipd_service_consumer> someipd_service,
                   Service_configs services, std::string identifier) noexcept
        : m_runtime{runtime},
          m_someipd_service{std::move(someipd_service)},
          m_services{std::move(services)},
          m_identifier{std::move(identifier)} {}

    bool is_connected() const noexcept override { return m_someipd_service->is_connected(); }

   private:
    [[maybe_unused]] score::socom::Runtime& m_runtime;
    std::unique_ptr<Someipd_service_consumer> m_someipd_service;
    [[maybe_unused]] Service_configs m_services;
    [[maybe_unused]] std::string m_identifier;
};

/// \brief Server flavour of the mw::com binding
/// \details Provides SomeipdService. It is offered in start(), before any bridged service is set
///          up, so that a peer seeing it can rely on this side accepting further setup.
class Server_adapter final : public Gateway_ipc_binding_server {
   public:
    Server_adapter(score::socom::Runtime& runtime, std::string someipd_service_specifier,
                   Service_configs services) noexcept
        : m_runtime{runtime},
          m_someipd_service_specifier{std::move(someipd_service_specifier)},
          m_services{std::move(services)} {}

    Result<void> start() noexcept override {
        if (m_someipd_service != nullptr) {
            return MakeUnexpected(Mw_com_binding_error::logic_error_already_started);
        }

        auto someipd_service = Someipd_service_provider::create(m_someipd_service_specifier);
        if (!someipd_service.has_value()) {
            return MakeUnexpected<void>(std::move(someipd_service).error());
        }

        auto const offered = someipd_service.value()->offer();
        if (!offered.has_value()) {
            return offered;
        }

        // Only kept once it is fully set up, so that a failed start() can be retried.
        m_someipd_service = std::move(someipd_service).value();

        // Bridged services would be set up here, strictly after SomeipdService has been offered.
        return {};
    }

   private:
    [[maybe_unused]] score::socom::Runtime& m_runtime;
    std::string m_someipd_service_specifier;
    [[maybe_unused]] Service_configs m_services;
    std::unique_ptr<Someipd_service_provider> m_someipd_service{};
};

}  // namespace

std::size_t sample_size(Event_config const& event) noexcept {
    return kSample_prefix_size + event.header_size + event.max_payload_size;
}

std::unique_ptr<Gateway_ipc_binding_client> create_client(score::socom::Runtime& runtime,
                                                          std::string someipd_service_specifier,
                                                          Service_configs services,
                                                          std::string_view identifier) noexcept {
    warn_about_unsupported_services(services, "create_client()");

    auto someipd_service = Someipd_service_consumer::create(someipd_service_specifier);
    if (!someipd_service.has_value()) {
        score::mw::log::LogError()
            << "[gateway_ipc_binding] Failed to create mw::com client binding:"
            << someipd_service.error();
        return nullptr;
    }

    return std::make_unique<Client_adapter>(runtime, std::move(someipd_service).value(),
                                            std::move(services), std::string{identifier});
}

std::unique_ptr<Gateway_ipc_binding_server> create_server(score::socom::Runtime& runtime,
                                                          std::string someipd_service_specifier,
                                                          Service_configs services) noexcept {
    warn_about_unsupported_services(services, "create_server()");

    if (someipd_service_specifier.empty()) {
        score::mw::log::LogError() << "[gateway_ipc_binding] Failed to create mw::com server "
                                      "binding: empty SomeipdService instance specifier";
        return nullptr;
    }

    return std::make_unique<Server_adapter>(runtime, std::move(someipd_service_specifier),
                                            std::move(services));
}

}  // namespace score::gateway_ipc_binding::mw_com
