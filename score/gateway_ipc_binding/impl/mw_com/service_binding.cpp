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
constexpr std::string_view kBridged_service_prefix{"bridged_"};

bool has_bridged_service_prefix(std::string_view const value) noexcept {
    return (value.size() >= kBridged_service_prefix.size()) &&
           (value.compare(0U, kBridged_service_prefix.size(), kBridged_service_prefix) == 0);
}

Result<void> invalid_config_error(std::string_view const instance_specifier,
                                  std::string_view const reason) noexcept {
    score::mw::log::LogError() << kLog_tag << "Invalid configuration of bridged service"
                               << instance_specifier << ":" << reason;
    return MakeUnexpected(Mw_com_binding_error::logic_error_invalid_service_configuration);
}

// score::json::Number::As<T>() rejects signed integers and wider unsigned types even when the
// value fits, which aborts in mw::com's config parser. Match mw::com's target integer types.

/// \brief Synthetic mw::com service version (distinct from the SOME/IP interface version)
score::json::Object mw_com_service_version() noexcept {
    score::json::Object version{};
    version["major"] = std::uint32_t{1};
    version["minor"] = std::uint32_t{0};
    return version;
}

/// \brief serviceTypes entry. Named after instance_specifier because AddConfiguration() aborts on
///        duplicate serviceTypeName across calls.
score::json::Object service_type_entry(Service_config const& config) noexcept {
    score::json::List events_json{};
    events_json.reserve(config.events.size());
    for (auto const& event : config.events) {
        score::json::Object event_json{};
        event_json["eventName"] = event.name;
        event_json["eventId"] = static_cast<std::uint16_t>(event.id);
        events_json.emplace_back(std::move(event_json));
    }
    score::json::Object shm_binding_json{};
    shm_binding_json["binding"] = std::string{"SHM"};
    shm_binding_json["serviceId"] = std::uint16_t{config.service_id};
    shm_binding_json["events"] = std::move(events_json);
    score::json::List bindings_json{};
    bindings_json.emplace_back(std::move(shm_binding_json));

    score::json::Object service_type_json{};
    service_type_json["serviceTypeName"] = config.instance_specifier;
    service_type_json["version"] = mw_com_service_version();
    service_type_json["bindings"] = std::move(bindings_json);
    return service_type_json;
}

score::json::Object service_instance_entry(Service_config const& config) noexcept {
    score::json::List events_json{};
    events_json.reserve(config.events.size());
    for (auto const& event : config.events) {
        score::json::Object event_json{};
        event_json["eventName"] = event.name;
        event_json["numberOfSampleSlots"] = static_cast<std::uint16_t>(kNumber_of_sample_slots);
        // Only the peer daemon subscribes.
        event_json["maxSubscribers"] = std::uint8_t{1U};
        events_json.emplace_back(std::move(event_json));
    }
    score::json::Object shm_instance_json{};
    shm_instance_json["instanceId"] = std::uint16_t{config.instance_id};
    shm_instance_json["asil-level"] = std::string{"QM"};
    shm_instance_json["binding"] = std::string{"SHM"};
    shm_instance_json["events"] = std::move(events_json);
    score::json::List instances_json{};
    instances_json.emplace_back(std::move(shm_instance_json));

    score::json::Object service_instance_json{};
    service_instance_json["instanceSpecifier"] = config.instance_specifier;
    service_instance_json["serviceTypeName"] = config.instance_specifier;
    service_instance_json["version"] = mw_com_service_version();
    service_instance_json["instances"] = std::move(instances_json);
    return service_instance_json;
}

/// \brief mw::com configuration JSON for `services`, with one serviceType per instance
score::json::Object mw_com_deployment(Service_configs const& services) noexcept {
    score::json::List service_types_json{};
    service_types_json.reserve(services.size());
    score::json::List service_instances_json{};
    service_instances_json.reserve(services.size());
    for (auto const& config : services) {
        service_types_json.emplace_back(service_type_entry(config));
        service_instances_json.emplace_back(service_instance_entry(config));
    }

    score::json::Object config_json{};
    config_json["serviceTypes"] = std::move(service_types_json);
    config_json["serviceInstances"] = std::move(service_instances_json);
    return config_json;
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
    if (!has_bridged_service_prefix(config.instance_specifier)) {
        return invalid_config_error(config.instance_specifier,
                                    "instance specifier does not start with bridged_");
    }
    if (config.events.empty()) {
        return invalid_config_error(config.instance_specifier, "no events are configured");
    }

    std::set<std::string_view> seen_event_names{};
    std::set<Mw_com_event_id> seen_event_ids{};
    for (auto const& event : config.events) {
        if (event.name.empty()) {
            return invalid_config_error(config.instance_specifier, "an event has no name");
        }
        if (!seen_event_names.insert(event.name).second) {
            return invalid_config_error(config.instance_specifier,
                                        "an event name is used more than once");
        }
        if (!seen_event_ids.insert(event.id).second) {
            return invalid_config_error(config.instance_specifier,
                                        "an event ID is used more than once");
        }
        if (event.max_payload_size > kMax_payload_length) {
            return invalid_config_error(
                config.instance_specifier,
                "an event payload is larger than the length prefix can express");
        }
    }

    // Subscribe(0) would succeed but never hand a sample to the application.
    if ((config.role == Role::consumer) && (config.max_sample_count == 0U)) {
        return invalid_config_error(config.instance_specifier, "max_sample_count is zero");
    }
    if (config.max_sample_count > kNumber_of_sample_slots) {
        return invalid_config_error(config.instance_specifier,
                                    "max_sample_count exceeds the sample slots");
    }

    return {};
}

Result<void> add_to_mw_com_runtime(Service_configs const& services) noexcept {
    Service_configs undeployed_services{};
    std::set<std::string_view> seen_specifiers{};
    std::set<std::uint16_t> seen_service_ids{};
    for (auto const& config : services) {
        auto valid = validate(config);
        if (!valid.has_value()) {
            return valid;
        }
        if (!seen_specifiers.insert(config.instance_specifier).second) {
            return invalid_config_error(config.instance_specifier,
                                        "the instance specifier is used twice");
        }
        if (!seen_service_ids.insert(config.service_id).second) {
            return invalid_config_error(config.instance_specifier, "the service ID is used twice");
        }
        auto specifier = make_instance_specifier(config.instance_specifier);
        if (!specifier.has_value()) {
            return MakeUnexpected<void>(std::move(specifier).error());
        }
        auto const resolved_ids = score::mw::com::runtime::ResolveInstanceIDs(specifier.value());
        if (!resolved_ids.has_value() || resolved_ids.value().empty()) {
            undeployed_services.push_back(config);
        }
    }

    if (!undeployed_services.empty()) {
        auto added = score::mw::com::runtime::AddConfiguration(
            score::json::Any{mw_com_deployment(undeployed_services)});
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
