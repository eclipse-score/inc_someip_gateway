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
#include <iterator>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "provider_service_binding.hpp"
#include "score/gateway_ipc_binding/error.hpp"
#include "score/json/json_parser.h"
#include "score/mw/com/runtime.h"
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

/// \brief The entries of `services` with the given role
Service_configs with_role(Service_configs const& services, Role const role) noexcept {
    Service_configs selected{};
    std::copy_if(services.cbegin(), services.cend(), std::back_inserter(selected),
                 [role](Service_config const& service) { return service.role == role; });
    return selected;
}

/// \brief SetBridgedEventSizes argument: the size of every consumer-role event, in order
std::optional<Bridged_event_sizes> consumer_event_sizes(Service_configs const& services) noexcept {
    Bridged_event_sizes sizes{};
    for (auto const& service : with_role(services, Role::consumer)) {
        for (auto const& event : service.events) {
            if (sizes.size == sizes.max_size) {
                score::mw::log::LogError() << kLog_tag << "SetBridgedEventSizes carries at most"
                                           << sizes.max_size << "event sizes";
                return std::nullopt;
            }
            sizes.data[sizes.size] = event.max_payload_size;
            ++sizes.size;
        }
    }
    return sizes;
}

/// \brief `configs` with the sizes of a SetBridgedEventSizes call, one per event, in order
/// \return std::nullopt, logged, if the number of sizes does not match the number of events
std::optional<Service_configs> with_event_sizes(Service_configs configs,
                                                Bridged_event_sizes const& sizes) noexcept {
    std::size_t event_count{0U};
    for (auto const& config : configs) {
        event_count += config.events.size();
    }
    if ((sizes.size != event_count) || (event_count > sizes.max_size)) {
        score::mw::log::LogError() << kLog_tag << "SetBridgedEventSizes has" << sizes.size
                                   << "sizes for" << event_count << "events";
        return std::nullopt;
    }

    auto size = sizes.data.cbegin();
    for (auto& config : configs) {
        for (auto& event : config.events) {
            event.max_payload_size = *size;
            ++size;
        }
    }
    return configs;
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
///          up, so that a peer seeing it can rely on this side accepting further setup. The
///          provider-role services follow once the peer sends their sizes.
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

        // The provider-role services wait for their sizes, but a configuration error fails here.
        for (auto const& config : m_provider_configs) {
            auto checked = Provider_service_binding::check(config);
            if (!checked.has_value()) {
                return checked;
            }
        }

        auto someipd_service = Someipd_service_provider::create(
            m_someipd_service_specifier,
            [](Service_configuration_text const& configuration) noexcept {
                if (configuration.size > configuration.max_size) {
                    return false;
                }

                score::json::JsonParser const parser{};
                auto json = parser.FromBuffer(
                    std::string_view{configuration.data.data(), configuration.size});
                if (!json.has_value()) {
                    score::mw::log::LogError()
                        << kLog_tag
                        << "Failed to parse add-on mw::com configuration:" << json.error();
                    return false;
                }

                return score::mw::com::runtime::AddConfiguration(std::move(json).value())
                    .has_value();
            },
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
            // Destroying the skeleton waits for a running handler, then its providers can go.
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
    /// \brief SetBridgedEventSizes handler: sets up the provider-role services with these sizes
    /// \details Runs on the mw::com message passing thread. LoLa 0.5.0 only fails calls there that
    ///          wait for a reply, which GenericSkeleton::Create() and OfferService() do not. A
    ///          repeated equal call is accepted, a different one rejected.
    bool on_event_sizes(Bridged_event_sizes const& peer_sizes) noexcept {
        // A copy, because the peer can still write to its shared memory.
        Bridged_event_sizes const sizes = peer_sizes;
        std::lock_guard const lock{m_providers_mutex};
        if (m_provider_sizes.has_value()) {
            return m_provider_sizes.value() == sizes;
        }

        auto configs = with_event_sizes(m_provider_configs, sizes);
        if (!configs.has_value()) {
            return false;
        }
        auto providers = make_service_bindings(m_runtime, configs.value());
        if (!providers.has_value()) {
            return false;
        }
        m_providers = std::move(providers).value();
        m_provider_sizes = sizes;
        return true;
    }

    score::socom::Runtime& m_runtime;
    std::string m_someipd_service_specifier;
    Service_configs m_consumer_configs;
    Service_configs m_provider_configs;
    Service_bindings m_services{};
    /// \brief Guards the provider-role state, which the SetBridgedEventSizes handler sets
    std::mutex m_providers_mutex;
    std::optional<Bridged_event_sizes> m_provider_sizes{};
    Service_bindings m_providers{};
    /// \brief Declared last, so that it is destroyed first: its handler uses the members above
    std::unique_ptr<Someipd_service_provider> m_someipd_service{};
};

}  // namespace

std::size_t sample_size(Event_config const& event) noexcept {
    auto const addressed = kSample_prefix_size + event.max_payload_size;
    // Round up: a mw::com DataTypeMetaInfo::size must be an integer multiple of its alignment.
    return ((addressed + kSample_alignment) - 1U) / kSample_alignment * kSample_alignment;
}

std::unique_ptr<Gateway_ipc_binding_client> create_client(
    score::socom::Runtime& runtime, std::string someipd_service_specifier, Service_configs services,
    std::string_view identifier, std::optional<std::string_view> mw_com_config_json) noexcept {
    auto const bridged_event_sizes = consumer_event_sizes(services);
    if (!bridged_event_sizes.has_value()) {
        return nullptr;
    }

    auto someipd_service = Someipd_service_consumer::create(
        someipd_service_specifier, bridged_event_sizes.value(), mw_com_config_json);
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
