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

#include "service_binding.hpp"

#include <cstdint>
#include <set>
#include <string_view>
#include <utility>

#include "consumer_service_binding.hpp"
#include "provider_service_binding.hpp"
#include "sample_layout.hpp"
#include "score/gateway_ipc_binding/error.hpp"
#include "score/json/json_parser.h"
#include "score/mw/com/runtime.h"
#include "score/mw/log/logging.h"

namespace score::gateway_ipc_binding::mw_com {

namespace {

/// \brief Log prefix shared by the whole binding
constexpr std::string_view kLog_tag{"[gateway_ipc_binding]"};

Result<void> invalid(std::string_view const instance_specifier,
                     std::string_view const reason) noexcept {
    score::mw::log::LogError() << kLog_tag << "Invalid configuration of bridged service"
                               << instance_specifier << ":" << reason;
    return MakeUnexpected(Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// Use the exact integer types mw::com expects. It rejects wider ones.

/// \brief Version of the bridged service types, not the SOME/IP one
score::json::Any bridge_version() noexcept {
    score::json::Object version{};
    version["major"] = score::json::Any{std::uint32_t{1}};
    version["minor"] = score::json::Any{std::uint32_t{0}};
    return score::json::Any{std::move(version)};
}

/// \brief serviceTypes entry, named like the instance specifier
score::json::Object service_type_entry(Service_config const& service) noexcept {
    score::json::List events{};
    for (auto const& configured : service.events) {
        score::json::Object event{};
        event["eventName"] = score::json::Any{configured.name};
        event["eventId"] = score::json::Any{static_cast<std::uint16_t>(configured.id)};
        events.emplace_back(std::move(event));
    }
    score::json::Object binding{};
    binding["binding"] = score::json::Any{std::string{"SHM"}};
    binding["serviceId"] = score::json::Any{service.service_id};
    binding["events"] = score::json::Any{std::move(events)};
    score::json::List bindings{};
    bindings.emplace_back(std::move(binding));

    score::json::Object type{};
    type["serviceTypeName"] = score::json::Any{service.instance_specifier};
    type["version"] = bridge_version();
    type["bindings"] = score::json::Any{std::move(bindings)};
    return type;
}

score::json::Object service_instance_entry(Service_config const& service) noexcept {
    score::json::List events{};
    for (auto const& configured : service.events) {
        score::json::Object event{};
        event["eventName"] = score::json::Any{configured.name};
        event["numberOfSampleSlots"] =
            score::json::Any{static_cast<std::uint16_t>(kNumber_of_sample_slots)};
        // Only the peer daemon subscribes.
        event["maxSubscribers"] = score::json::Any{std::uint8_t{1U}};
        events.emplace_back(std::move(event));
    }
    score::json::Object deployment{};
    deployment["instanceId"] = score::json::Any{service.instance_id};
    deployment["asil-level"] = score::json::Any{std::string{"QM"}};
    deployment["binding"] = score::json::Any{std::string{"SHM"}};
    deployment["events"] = score::json::Any{std::move(events)};
    score::json::List deployments{};
    deployments.emplace_back(std::move(deployment));

    score::json::Object instance{};
    instance["instanceSpecifier"] = score::json::Any{service.instance_specifier};
    instance["serviceTypeName"] = score::json::Any{service.instance_specifier};
    instance["version"] = bridge_version();
    instance["instances"] = score::json::Any{std::move(deployments)};
    return instance;
}

/// \brief mw::com configuration for `services`, one service type per instance
score::json::Object mw_com_deployment(Service_configs const& services) noexcept {
    score::json::List types{};
    score::json::List instances{};
    for (auto const& service : services) {
        types.emplace_back(service_type_entry(service));
        instances.emplace_back(service_instance_entry(service));
    }

    score::json::Object deployment{};
    deployment["serviceTypes"] = score::json::Any{std::move(types)};
    deployment["serviceInstances"] = score::json::Any{std::move(instances)};
    return deployment;
}

}  // namespace

Result<score::mw::com::InstanceSpecifier> make_instance_specifier(
    std::string const& instance_specifier) noexcept {
    auto specifier = score::mw::com::InstanceSpecifier::Create(std::string{instance_specifier});
    if (!specifier.has_value()) {
        score::mw::log::LogError() << kLog_tag << "Invalid mw::com instance specifier"
                                   << instance_specifier << ":" << specifier.error();
        return MakeUnexpected(Mw_com_binding_error::logic_error_invalid_instance_specifier);
    }
    return std::move(specifier).value();
}

Result<void> check_instance_is_deployed(score::mw::com::InstanceSpecifier const& specifier,
                                        std::string const& instance_specifier) noexcept {
    auto const identifiers = score::mw::com::runtime::ResolveInstanceIDs(specifier);
    if (!identifiers.has_value()) {
        score::mw::log::LogError() << kLog_tag << "Failed to resolve mw::com instance specifier"
                                   << instance_specifier << ":" << identifiers.error();
        return MakeUnexpected(Mw_com_binding_error::logic_error_invalid_instance_specifier);
    }
    if (identifiers.value().empty()) {
        score::mw::log::LogError() << kLog_tag << "The mw::com instance specifier"
                                   << instance_specifier << "is not part of the mw::com deployment";
        return MakeUnexpected(Mw_com_binding_error::logic_error_invalid_instance_specifier);
    }
    return {};
}

Result<void> validate(Service_config const& config) noexcept {
    if (config.events.empty()) {
        return invalid(config.instance_specifier, "no events are configured");
    }

    std::set<std::string_view> names{};
    std::set<Mw_com_event_id> ids{};
    for (auto const& event : config.events) {
        if (event.name.empty()) {
            return invalid(config.instance_specifier, "an event has no name");
        }
        if (!names.insert(event.name).second) {
            return invalid(config.instance_specifier, "an event name is used more than once");
        }
        if (!ids.insert(event.id).second) {
            return invalid(config.instance_specifier, "an event ID is used more than once");
        }
        if (event.max_payload_size > kMax_payload_length) {
            return invalid(config.instance_specifier,
                           "an event payload is larger than the length prefix can express");
        }
    }

    // Subscribe(0) would succeed but never hand a sample to the application.
    if ((config.role == Role::consumer) && (config.max_sample_count == 0U)) {
        return invalid(config.instance_specifier, "max_sample_count is zero");
    }
    if (config.max_sample_count > kNumber_of_sample_slots) {
        return invalid(config.instance_specifier, "max_sample_count exceeds the sample slots");
    }

    return {};
}

Result<void> add_to_mw_com_runtime(Service_configs const& services) noexcept {
    Service_configs unknown{};
    std::set<std::string_view> specifiers{};
    std::set<std::uint16_t> service_ids{};
    for (auto const& service : services) {
        auto valid = validate(service);
        if (!valid.has_value()) {
            return valid;
        }
        if (!specifiers.insert(service.instance_specifier).second) {
            return invalid(service.instance_specifier, "the instance specifier is used twice");
        }
        if (!service_ids.insert(service.service_id).second) {
            return invalid(service.instance_specifier, "the service ID is used twice");
        }
        auto specifier = make_instance_specifier(service.instance_specifier);
        if (!specifier.has_value()) {
            return MakeUnexpected<void>(std::move(specifier).error());
        }
        auto const known = score::mw::com::runtime::ResolveInstanceIDs(specifier.value());
        if (!known.has_value() || known.value().empty()) {
            unknown.push_back(service);
        }
    }

    if (!unknown.empty()) {
        auto added =
            score::mw::com::runtime::AddConfiguration(score::json::Any{mw_com_deployment(unknown)});
        if (!added.has_value()) {
            score::mw::log::LogError()
                << kLog_tag << "Failed to add the bridged services to mw::com:" << added.error();
            return MakeUnexpected(Mw_com_binding_error::runtime_error_service_setup_failed);
        }
    }
    return {};
}

Result<std::unique_ptr<Service_binding>> make_service_binding(
    score::socom::Runtime& runtime, Service_config const& config) noexcept {
    auto valid = validate(config);
    if (!valid.has_value()) {
        return MakeUnexpected<std::unique_ptr<Service_binding>>(std::move(valid).error());
    }

    if (config.role == Role::provider) {
        auto binding = Provider_service_binding::create(runtime, config);
        if (!binding.has_value()) {
            return MakeUnexpected<std::unique_ptr<Service_binding>>(std::move(binding).error());
        }
        return std::unique_ptr<Service_binding>{std::move(binding).value()};
    }

    auto binding = Consumer_service_binding::create(runtime, config);
    if (!binding.has_value()) {
        return MakeUnexpected<std::unique_ptr<Service_binding>>(std::move(binding).error());
    }
    return std::unique_ptr<Service_binding>{std::move(binding).value()};
}

}  // namespace score::gateway_ipc_binding::mw_com
