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

#include <set>
#include <string_view>
#include <utility>

#include "consumer_service_binding.hpp"
#include "provider_service_binding.hpp"
#include "sample_layout.hpp"
#include "score/gateway_ipc_binding/error.hpp"
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
    for (auto const& event : config.events) {
        if (event.name.empty()) {
            return invalid(config.instance_specifier, "an event has no name");
        }
        if (!names.insert(event.name).second) {
            return invalid(config.instance_specifier, "an event name is used more than once");
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
