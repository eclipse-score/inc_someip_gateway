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

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
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

Service_configs with_role(Service_configs const& services, Role const role) noexcept {
    Service_configs selected{};
    std::copy_if(services.cbegin(), services.cend(), std::back_inserter(selected),
                 [role](Service_config const& service) { return service.role == role; });
    return selected;
}

/// \brief Builds the SetBridgedEventSizes argument from the consumer events
/// \pre validate() MUST have passed. Every size then fits std::uint32_t.
std::optional<Bridged_event_sizes> consumer_event_sizes(Service_configs const& services) noexcept {
    Bridged_event_sizes sizes{};
    for (auto const& service : with_role(services, Role::consumer)) {
        for (auto const& event : service.events) {
            if (sizes.size == sizes.max_size) {
                score::mw::log::LogError() << kLog_tag << "SetBridgedEventSizes carries at most"
                                           << sizes.max_size << "event sizes";
                return std::nullopt;
            }
            sizes.data.at(sizes.size) = {service.service_id, static_cast<std::uint16_t>(event.id),
                                         static_cast<std::uint32_t>(event.max_payload_size)};
            ++sizes.size;
        }
    }
    return sizes;
}

/// \brief Copy of `configs` with the sizes from SetBridgedEventSizes filled in
/// \return std::nullopt unless there is exactly one size per event
std::optional<Service_configs> with_event_sizes(Service_configs configs,
                                                Bridged_event_sizes const& sizes) noexcept {
    std::map<std::pair<std::uint16_t, Mw_com_event_id>, Event_config*> unsized{};
    for (auto& config : configs) {
        for (auto& event : config.events) {
            unsized.emplace(std::make_pair(config.service_id, event.id), &event);
        }
    }
    if ((sizes.size != unsized.size()) || (sizes.size > sizes.max_size)) {
        score::mw::log::LogError() << kLog_tag << "SetBridgedEventSizes has" << sizes.size
                                   << "sizes for" << unsized.size() << "events";
        return std::nullopt;
    }

    for (std::size_t index = 0U; index < sizes.size; ++index) {
        auto const& size = sizes.data.at(index);
        auto const event =
            unsized.find(std::make_pair(size.service_id, Mw_com_event_id{size.event_id}));
        if (event == unsized.end()) {
            score::mw::log::LogError()
                << kLog_tag << "SetBridgedEventSizes has an unknown or repeated event"
                << size.event_id << "of service" << size.service_id;
            return std::nullopt;
        }
        event->second->max_payload_size = size.max_payload_size;
        unsized.erase(event);
    }
    return configs;
}

std::vector<std::size_t> event_sizes(Service_configs const& configs) noexcept {
    std::vector<std::size_t> sizes{};
    for (auto const& config : configs) {
        for (auto const& event : config.events) {
            sizes.push_back(event.max_payload_size);
        }
    }
    return sizes;
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
///          The providers are set up when the peer sends their sizes.
class Server_adapter final : public Gateway_ipc_binding_server {
   public:
    Server_adapter(score::socom::Runtime& runtime, std::string someipd_service_specifier,
                   Service_configs const& services) noexcept
        : m_runtime{runtime},
          m_someipd_service_specifier{std::move(someipd_service_specifier)},
          m_consumer_configs{with_role(services, Role::consumer)},
          m_provider_configs{with_role(services, Role::provider)} {}

    Result<void> start() noexcept override {
        if (m_someipd_service != nullptr) {
            return MakeUnexpected(Mw_com_binding_error::logic_error_already_started);
        }

        auto someipd_service = Someipd_service_provider::create(
            m_someipd_service_specifier,
            [this](Bridged_event_sizes const& sizes) noexcept { return on_event_sizes(sizes); });
        if (!someipd_service.has_value()) {
            return MakeUnexpected<void>(std::move(someipd_service).error());
        }

        auto const offered = someipd_service.value()->offer();
        if (!offered.has_value()) {
            return offered;
        }

        // Strictly after SomeipdService has been offered, see
        // docs/mw_com_binding.rst, section "Offering order".
        auto services = make_service_bindings(m_runtime, m_consumer_configs);
        if (!services.has_value()) {
            // The skeleton MUST be destroyed first. Its destructor waits for a running handler.
            someipd_service.value().reset();
            m_providers.clear();
            m_provider_sizes.reset();
            return MakeUnexpected<void>(std::move(services).error());
        }

        // Only kept once everything is set up, so that a failed start() can be retried.
        m_someipd_service = std::move(someipd_service).value();
        m_services = std::move(services).value();
        return {};
    }

   private:
    /// \brief Handles SetBridgedEventSizes. Sets up the providers with the received sizes.
    /// \details Runs on the mw::com message passing thread. MUST NOT call anything that waits
    ///          for a reply.
    bool on_event_sizes(Bridged_event_sizes const& peer_sizes) noexcept {
        // The peer can still write to this shared memory. Copy it first.
        Bridged_event_sizes const sizes = peer_sizes;
        std::lock_guard const lock{m_providers_mutex};
        auto configs = with_event_sizes(m_provider_configs, sizes);
        if (!configs.has_value()) {
            return false;
        }
        auto received = event_sizes(configs.value());
        if (m_provider_sizes.has_value()) {
            if (m_provider_sizes.value() != received) {
                score::mw::log::LogError()
                    << kLog_tag << "SetBridgedEventSizes differs from the accepted call";
                return false;
            }
            return true;
        }

        auto providers = make_service_bindings(m_runtime, configs.value());
        if (!providers.has_value()) {
            return false;
        }
        m_providers = std::move(providers).value();
        m_provider_sizes = std::move(received);
        return true;
    }

    score::socom::Runtime& m_runtime;
    std::string m_someipd_service_specifier;
    Service_configs m_consumer_configs;
    Service_configs m_provider_configs;
    Service_bindings m_services{};
    /// \brief Guards m_provider_sizes and m_providers
    std::mutex m_providers_mutex;
    std::optional<std::vector<std::size_t>> m_provider_sizes{};
    Service_bindings m_providers{};
    /// \brief Declared last, so it is destroyed first. Its handler uses the members above.
    std::unique_ptr<Someipd_service_provider> m_someipd_service{};
};

}  // namespace

std::size_t sample_size(Event_config const& event) noexcept {
    auto const addressed = kSample_prefix_size + event.max_payload_size;
    // Round up: a mw::com DataTypeMetaInfo::size must be an integer multiple of its alignment.
    return ((addressed + kSample_alignment) - 1U) / kSample_alignment * kSample_alignment;
}

std::unique_ptr<Gateway_ipc_binding_client> create_client(score::socom::Runtime& runtime,
                                                          std::string someipd_service_specifier,
                                                          Service_configs services,
                                                          std::string_view identifier) noexcept {
    auto const added = add_to_mw_com_runtime(services);
    if (!added.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to create mw::com client binding:" << added.error();
        return nullptr;
    }

    auto const bridged_event_sizes = consumer_event_sizes(services);
    if (!bridged_event_sizes.has_value()) {
        return nullptr;
    }

    auto someipd_service =
        Someipd_service_consumer::create(someipd_service_specifier, bridged_event_sizes.value());
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

    auto const added = add_to_mw_com_runtime(services);
    if (!added.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to create mw::com server binding:" << added.error();
        return nullptr;
    }

    return std::make_unique<Server_adapter>(runtime, std::move(someipd_service_specifier),
                                            std::move(services));
}

}  // namespace score::gateway_ipc_binding::mw_com
