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

#include <string_view>
#include <utility>
#include <vector>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/mw/log/logging.h"
#include "service_binding.hpp"
#include "someipd_service_binding.hpp"

namespace score::gateway_ipc_binding::mw_com {

namespace {

/// \brief Log prefix shared by the whole binding
constexpr std::string_view kLog_tag{"[gateway_ipc_binding]"};

/// \brief One Service_binding per configured bridged service instance
using Service_bindings = std::vector<std::unique_ptr<Service_binding>>;

/// \brief Set every configured bridged service up, or none of them
/// \details Partial setup would leave a link that carries some services and silently drops
///          others, which is harder to diagnose than a startup failure. On error the bindings
///          created so far are destroyed in reverse order.
/// \param runtime SOCom runtime used to create the connectors
/// \param services Bridged service instances
/// \return The bindings, or the first error encountered
Result<Service_bindings> make_service_bindings(score::socom::Runtime& runtime,
                                               Service_configs const& services) noexcept {
    Service_bindings bindings{};
    bindings.reserve(services.size());

    for (auto const& service : services) {
        auto binding = make_service_binding(runtime, service);
        if (!binding.has_value()) {
            score::mw::log::LogError() << kLog_tag << "Failed to set up bridged service"
                                       << service.instance_specifier << ":" << binding.error();
            return MakeUnexpected<Service_bindings>(std::move(binding).error());
        }
        bindings.push_back(std::move(binding).value());
    }

    return bindings;
}

/// \brief Client flavour of the mw::com binding
/// \details Consumes SomeipdService and reports it through is_connected(). The bridged services
///          are symmetric to the server flavour and live for as long as this object does.
class Client_adapter final : public Gateway_ipc_binding_client {
   public:
    Client_adapter(std::unique_ptr<Someipd_service_consumer> someipd_service,
                   Service_bindings services, std::string identifier) noexcept
        : m_someipd_service{std::move(someipd_service)},
          m_services{std::move(services)},
          m_identifier{std::move(identifier)} {}

    bool is_connected() const noexcept override { return m_someipd_service->is_connected(); }

   private:
    std::unique_ptr<Someipd_service_consumer> m_someipd_service;
    Service_bindings m_services;
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
          m_service_configs{std::move(services)} {}

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

        // Strictly after SomeipdService has been offered, see
        // docs/mw_com_binding.rst, section "Offering order".
        auto services = make_service_bindings(m_runtime, m_service_configs);
        if (!services.has_value()) {
            return MakeUnexpected<void>(std::move(services).error());
        }

        // Only kept once everything is set up, so that a failed start() can be retried.
        m_someipd_service = std::move(someipd_service).value();
        m_services = std::move(services).value();
        return {};
    }

   private:
    score::socom::Runtime& m_runtime;
    std::string m_someipd_service_specifier;
    Service_configs m_service_configs;
    std::unique_ptr<Someipd_service_provider> m_someipd_service{};
    Service_bindings m_services{};
};

}  // namespace

std::size_t sample_size(Event_config const& event) noexcept {
    auto const addressed = kSample_prefix_size + event.header_size + event.max_payload_size;
    // Round up: a mw::com DataTypeMetaInfo::size must be an integer multiple of its alignment.
    return ((addressed + kSample_alignment) - 1U) / kSample_alignment * kSample_alignment;
}

std::unique_ptr<Gateway_ipc_binding_client> create_client(score::socom::Runtime& runtime,
                                                          std::string someipd_service_specifier,
                                                          Service_configs services,
                                                          std::string_view identifier) noexcept {
    auto someipd_service = Someipd_service_consumer::create(someipd_service_specifier);
    if (!someipd_service.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to create mw::com client binding:" << someipd_service.error();
        return nullptr;
    }

    auto service_bindings = make_service_bindings(runtime, services);
    if (!service_bindings.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to create mw::com client binding:" << service_bindings.error();
        return nullptr;
    }

    return std::make_unique<Client_adapter>(std::move(someipd_service).value(),
                                            std::move(service_bindings).value(),
                                            std::string{identifier});
}

std::unique_ptr<Gateway_ipc_binding_server> create_server(score::socom::Runtime& runtime,
                                                          std::string someipd_service_specifier,
                                                          Service_configs services) noexcept {
    if (someipd_service_specifier.empty()) {
        score::mw::log::LogError() << kLog_tag
                                   << "Failed to create mw::com server binding: empty "
                                      "SomeipdService instance specifier";
        return nullptr;
    }

    return std::make_unique<Server_adapter>(runtime, std::move(someipd_service_specifier),
                                            std::move(services));
}

}  // namespace score::gateway_ipc_binding::mw_com
