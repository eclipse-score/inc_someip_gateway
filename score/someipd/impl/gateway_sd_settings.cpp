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

#include "gateway_sd_settings.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "score/mw/log/logging.h"

namespace score::someipd {
namespace {

std::optional<std::string> json_value(std::string_view json, std::string_view key) {
    const std::string token = std::string("\"") + std::string(key) + "\"";
    std::size_t pos = 0;
    while (pos < json.size()) {
        const auto found = json.find(token, pos);
        if (found == std::string_view::npos) {
            return std::nullopt;
        }
        std::size_t i = found + token.size();
        while ((i < json.size()) && (std::isspace(static_cast<unsigned char>(json[i])) != 0)) {
            ++i;
        }
        if ((i >= json.size()) || (json[i] != ':')) {
            pos = found + token.size();
            continue;
        }
        ++i;
        while ((i < json.size()) && (std::isspace(static_cast<unsigned char>(json[i])) != 0)) {
            ++i;
        }
        if (i >= json.size()) {
            return std::nullopt;
        }
        if (json[i] == '"') {
            ++i;
            const auto end = json.find('"', i);
            if (end == std::string_view::npos) {
                return std::nullopt;
            }
            return std::string(json.substr(i, end - i));
        }
        const std::size_t start = i;
        if ((json[i] == '-') || (json[i] == '+')) {
            ++i;
        }
        while ((i < json.size()) && (std::isdigit(static_cast<unsigned char>(json[i])) != 0)) {
            ++i;
        }
        if (i == start) {
            pos = found + token.size();
            continue;
        }
        return std::string(json.substr(start, i - start));
    }
    return std::nullopt;
}

std::string_view json_object(std::string_view json, std::string_view key) {
    const std::string token = std::string("\"") + std::string(key) + "\"";
    const auto key_pos = json.find(token);
    if (key_pos == std::string_view::npos) {
        return {};
    }
    const auto brace = json.find('{', key_pos);
    if (brace == std::string_view::npos) {
        return {};
    }
    int depth = 0;
    for (std::size_t i = brace; i < json.size(); ++i) {
        if (json[i] == '{') {
            ++depth;
        } else if (json[i] == '}') {
            --depth;
            if (depth == 0) {
                return json.substr(brace, i - brace + 1);
            }
        }
    }
    return {};
}

int parse_int(std::string const& text, int fallback) {
    if (text.empty()) {
        return fallback;
    }
    char* end = nullptr;
    auto const value = std::strtol(text.c_str(), &end, 10);
    if ((end == text.c_str()) || (*end != '\0')) {
        return fallback;
    }
    if ((value < 0) || (value > std::numeric_limits<int>::max())) {
        return fallback;
    }
    return static_cast<int>(value);
}

void assign_port(std::uint16_t& port, std::optional<std::string> const& text) {
    if (!text.has_value()) {
        return;
    }
    auto const parsed = parse_int(*text, -1);
    if ((parsed <= 0) || (parsed > 65535)) {
        return;
    }
    port = static_cast<std::uint16_t>(parsed);
}

void assign_positive(int& field, std::optional<std::string> const& text) {
    if (!text.has_value()) {
        return;
    }
    auto const parsed = parse_int(*text, -1);
    if (parsed < 0) {
        return;
    }
    field = parsed;
}

}  // namespace

GatewaySdSettings gateway_sd_defaults() { return GatewaySdSettings{}; }

GatewaySdSettings gateway_sd_settings_from_json(std::string_view json) {
    auto settings = gateway_sd_defaults();
    if (json.empty()) {
        return settings;
    }
    if (auto const unicast = json_value(json, "unicast"); unicast.has_value()) {
        settings.unicast_address = *unicast;
    }
    assign_port(settings.service_port, json_value(json, "unreliable"));

    auto const discovery = json_object(json, "service-discovery");
    if (discovery.empty()) {
        return settings;
    }
    if (auto const multicast = json_value(discovery, "multicast"); multicast.has_value()) {
        settings.multicast_address = *multicast;
    }
    assign_port(settings.multicast_port, json_value(discovery, "port"));
    assign_positive(settings.initial_delay_min_ms, json_value(discovery, "initial_delay_min"));
    assign_positive(settings.initial_delay_max_ms, json_value(discovery, "initial_delay_max"));
    assign_positive(settings.repetition_base_ms, json_value(discovery, "repetitions_base_delay"));
    assign_positive(settings.repetitions, json_value(discovery, "repetitions_max"));
    assign_positive(settings.ttl_seconds, json_value(discovery, "ttl"));
    assign_positive(settings.cyclic_offer_ms, json_value(discovery, "cyclic_offer_delay"));
    return settings;
}

GatewaySdSettings load_gateway_sd_settings() {
    auto const* const path = std::getenv("VSOMEIP_CONFIGURATION");
    if ((path == nullptr) || (path[0] == '\0')) {
        score::mw::log::LogInfo()
            << "[someipd] VSOMEIP_CONFIGURATION is unset; using the gateway SD defaults";
        return gateway_sd_defaults();
    }
    std::ifstream input{path};
    if (!input) {
        score::mw::log::LogError()
            << "[someipd] Could not read VSOMEIP_CONFIGURATION; using the gateway SD defaults";
        return gateway_sd_defaults();
    }
    std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    return gateway_sd_settings_from_json(text);
}

}  // namespace score::someipd
