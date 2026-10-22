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
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "score/concurrency/thread_pool.h"
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

/// \brief Builds the AddServiceConfiguration argument
/// \pre validate() MUST have passed so every event.max_payload_size fits in std::uint32_t.
std::unique_ptr<Service_configuration> service_configuration(
    Service_configs const& services, score::cpp::span<std::byte const> gateway_config) noexcept {
    if (gateway_config.size() > kMax_service_configuration_size) {
        score::mw::log::LogError()
            << kLog_tag << "The gateway configuration has" << gateway_config.size()
            << "bytes. AddServiceConfiguration carries at most" << kMax_service_configuration_size;
        return nullptr;
    }

    // Too large for the stack.
    auto configuration = std::make_unique<Service_configuration>();
    std::copy(gateway_config.begin(), gateway_config.end(),
              configuration->gateway_config.data.begin());
    configuration->gateway_config.size = gateway_config.size();

    auto& event_sizes = configuration->event_sizes;
    for (auto const& service : services) {
        if (service.role != Role::consumer) {
            continue;
        }
        for (auto const& event : service.events) {
            if (event_sizes.size == event_sizes.max_size) {
                score::mw::log::LogError() << kLog_tag << "AddServiceConfiguration carries at most"
                                           << event_sizes.max_size << "event sizes";
                return nullptr;
            }
            event_sizes.data.at(event_sizes.size) = {
                service.service_id, static_cast<std::uint16_t>(event.id),
                static_cast<std::uint32_t>(event.max_payload_size)};
            ++event_sizes.size;
        }
    }
    return configuration;
}

/// \brief Copy of `configs` with the provider sizes from AddServiceConfiguration filled in
/// \return std::nullopt unless there is exactly one size per provider event
std::optional<Service_configs> with_event_sizes(Service_configs configs,
                                                Bridged_event_sizes const& event_sizes) noexcept {
    std::map<std::pair<std::uint16_t, Mw_com_event_id>, Event_config*> unsized_events{};
    for (auto& config : configs) {
        if (config.role != Role::provider) {
            continue;
        }
        for (auto& event : config.events) {
            unsized_events.emplace(std::pair{config.service_id, event.id}, &event);
        }
    }
    if ((event_sizes.size != unsized_events.size()) || (event_sizes.size > event_sizes.max_size)) {
        score::mw::log::LogError() << kLog_tag << "AddServiceConfiguration has" << event_sizes.size
                                   << "sizes for" << unsized_events.size() << "provider events";
        return std::nullopt;
    }

    for (auto const& event_size :
         score::cpp::span<Bridged_event_size const>{event_sizes.data.data(), event_sizes.size}) {
        auto const event_it =
            unsized_events.find({event_size.service_id, Mw_com_event_id{event_size.event_id}});
        if (event_it == unsized_events.end()) {
            score::mw::log::LogError()
                << kLog_tag << "AddServiceConfiguration has an unknown or repeated event"
                << event_size.event_id << "of service" << event_size.service_id;
            return std::nullopt;
        }
        event_it->second->max_payload_size = event_size.max_payload_size;
        unsized_events.erase(event_it);
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
/// \details Provides SomeipdService. Each accepted AddServiceConfiguration call adds the bridged
///          services of one gatewayd.
class Server_adapter final : public Gateway_ipc_binding_server {
   public:
    Server_adapter(score::socom::Runtime& runtime, std::string someipd_service_specifier,
                   Gateway_config_handler handler) noexcept
        : m_runtime{runtime},
          m_someipd_service_specifier{std::move(someipd_service_specifier)},
          m_handler{std::move(handler)} {}

    Result<void> start() noexcept override {
        if (m_someipd_service != nullptr) {
            return MakeUnexpected(Mw_com_binding_error::logic_error_already_started);
        }

        auto someipd_service = Someipd_service_provider::create(
            m_someipd_service_specifier,
            [this](Service_configuration const& configuration) noexcept {
                return on_service_configuration(configuration);
            });
        if (!someipd_service.has_value()) {
            return MakeUnexpected<void>(std::move(someipd_service).error());
        }

        auto const offered = someipd_service.value()->offer();
        if (!offered.has_value()) {
            return offered;
        }

        m_someipd_service = std::move(someipd_service).value();
        return {};
    }

   private:
    /// \brief Handles AddServiceConfiguration
    /// \details Runs on an mw::com message passing thread. MUST NOT call anything that waits
    ///          for a reply.
    bool on_service_configuration(Service_configuration const& gatewayd_configuration) noexcept {
        // Copy out of shared memory before validating sizes so gatewayd cannot mutate them after
        // the check. Too large for the stack.
        auto configuration = std::make_unique<Service_configuration const>(gatewayd_configuration);
        auto const& gateway_config = configuration->gateway_config;
        if ((gateway_config.size > gateway_config.max_size) ||
            (configuration->event_sizes.size > configuration->event_sizes.max_size)) {
            score::mw::log::LogError() << kLog_tag << "AddServiceConfiguration is too large";
            return false;
        }

        std::lock_guard const lock{m_mutex};
        bool const already_accepted = std::any_of(
            m_accepted_configs.cbegin(), m_accepted_configs.cend(),
            [&configuration](auto const& accepted) { return *accepted == *configuration; });
        if (already_accepted) {
            // A restarted gatewayd sends the same configuration again.
            return true;
        }

        Service_bindings new_bindings{};
        Add_bridged_services const add_bridged_services =
            [this, &configuration, &new_bindings](Service_configs const& services) noexcept {
                auto const sized = with_event_sizes(services, configuration->event_sizes);
                if (!sized.has_value() || !add_to_mw_com_runtime(sized.value()).has_value()) {
                    return false;
                }
                auto bindings = make_service_bindings(m_runtime, sized.value());
                if (!bindings.has_value()) {
                    return false;
                }
                new_bindings = std::move(bindings).value();
                return true;
            };
        if (!m_handler(
                score::cpp::span<std::byte const>{gateway_config.data.data(), gateway_config.size},
                add_bridged_services)) {
            return false;
        }

        std::vector<Service_binding*> bindings_to_start{};
        bindings_to_start.reserve(new_bindings.size());
        for (auto& binding : new_bindings) {
            bindings_to_start.push_back(binding.get());
            m_services.push_back(std::move(binding));
        }
        m_accepted_configs.push_back(std::move(configuration));
        m_discovery.Post([bindings_to_start = std::move(bindings_to_start)](
                             score::cpp::stop_token const&) noexcept {
            for (auto* const binding : bindings_to_start) {
                // A failure is logged. The service then stays unavailable.
                static_cast<void>(binding->start());
            }
        });
        return true;
    }

    score::socom::Runtime& m_runtime;
    std::string m_someipd_service_specifier;
    Gateway_config_handler m_handler;
    /// \brief Guards m_accepted_configs and m_services
    std::mutex m_mutex;
    /// \brief Every accepted AddServiceConfiguration argument
    std::vector<std::unique_ptr<Service_configuration const>> m_accepted_configs{};
    Service_bindings m_services{};
    /// \brief Starts consumer service discovery off the AddServiceConfiguration handler thread.
    ///        Declared after m_services so its destructor stops the worker thread before m_services
    ///        is destroyed.
    score::concurrency::ThreadPool m_discovery{1U, "ipc_discovery"};
    /// \brief Declared last, so it is destroyed first. Its handler uses the members above.
    std::unique_ptr<Someipd_service_provider> m_someipd_service{};
};

}  // namespace

std::size_t sample_size(Event_config const& event) noexcept {
    auto const unaligned_size = kSample_prefix_size + event.max_payload_size;
    // Round up: a mw::com DataTypeMetaInfo::size must be an integer multiple of its alignment.
    return ((unaligned_size + kSample_alignment) - 1U) / kSample_alignment * kSample_alignment;
}

std::unique_ptr<Gateway_ipc_binding_client> create_client(
    score::socom::Runtime& runtime, std::string someipd_service_specifier, Service_configs services,
    std::string_view identifier, score::cpp::span<std::byte const> gateway_config) noexcept {
    auto const added = add_to_mw_com_runtime(services);
    if (!added.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to create mw::com client binding:" << added.error();
        return nullptr;
    }

    auto const configuration = service_configuration(services, gateway_config);
    if (configuration == nullptr) {
        return nullptr;
    }

    auto service_bindings = make_service_bindings(runtime, services);
    if (!service_bindings.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to create mw::com client binding:" << service_bindings.error();
        return nullptr;
    }
    for (auto const& binding : service_bindings.value()) {
        auto const started = binding->start();
        if (!started.has_value()) {
            score::mw::log::LogError()
                << kLog_tag << "Failed to create mw::com client binding:" << started.error();
            return nullptr;
        }
    }

    auto someipd_service =
        Someipd_service_consumer::create(someipd_service_specifier, *configuration);
    if (!someipd_service.has_value()) {
        score::mw::log::LogError()
            << kLog_tag << "Failed to create mw::com client binding:" << someipd_service.error();
        return nullptr;
    }

    return std::make_unique<Client_adapter>(std::move(someipd_service).value(),
                                            std::move(service_bindings).value(),
                                            std::string{identifier});
}

std::unique_ptr<Gateway_ipc_binding_server> create_server(score::socom::Runtime& runtime,
                                                          std::string someipd_service_specifier,
                                                          Gateway_config_handler handler) noexcept {
    if (someipd_service_specifier.empty()) {
        score::mw::log::LogError() << kLog_tag
                                   << "Failed to create mw::com server binding: empty "
                                      "SomeipdService instance specifier";
        return nullptr;
    }

    return std::make_unique<Server_adapter>(runtime, std::move(someipd_service_specifier),
                                            std::move(handler));
}

}  // namespace score::gateway_ipc_binding::mw_com
