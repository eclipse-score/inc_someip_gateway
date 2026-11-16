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
#include <vector>

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

std::size_t skip_string(std::string_view text, std::size_t open) {
    if ((open >= text.size()) || (text[open] != '"')) {
        return open;
    }
    for (std::size_t i = open + 1; i < text.size(); ++i) {
        if ((text[i] == '\\') && ((i + 1) < text.size())) {
            ++i;
            continue;
        }
        if (text[i] == '"') {
            return i + 1;
        }
    }
    return text.size();
}

std::string_view span_from(std::string_view text, std::size_t open) {
    if (open >= text.size()) {
        return {};
    }
    char const open_ch = text[open];
    char const close_ch = (open_ch == '{') ? '}' : ']';
    int depth = 0;
    for (std::size_t i = open; i < text.size(); ++i) {
        if (text[i] == '"') {
            auto const next = skip_string(text, i);
            if (next <= i) {
                return {};
            }
            i = next - 1;
            continue;
        }
        if (text[i] == open_ch) {
            ++depth;
        } else if (text[i] == close_ch) {
            --depth;
            if (depth == 0) {
                return text.substr(open, i - open + 1);
            }
        }
    }
    return {};
}

std::size_t find_top_level_key(std::string_view object, std::string_view key) {
    std::string const token = std::string("\"") + std::string(key) + "\"";
    int depth = 0;
    for (std::size_t i = 0; i < object.size(); ++i) {
        char const ch = object[i];
        if (ch == '"') {
            if ((depth == 1) && (object.compare(i, token.size(), token) == 0)) {
                std::size_t j = i + token.size();
                while ((j < object.size()) &&
                       (std::isspace(static_cast<unsigned char>(object[j])) != 0)) {
                    ++j;
                }
                if ((j < object.size()) && (object[j] == ':')) {
                    return i;
                }
            }
            auto const next = skip_string(object, i);
            if (next <= i) {
                return std::string_view::npos;
            }
            i = next - 1;
            continue;
        }
        if ((ch == '{') || (ch == '[')) {
            ++depth;
        } else if (((ch == '}') || (ch == ']')) && (depth > 0)) {
            --depth;
        }
    }
    return std::string_view::npos;
}

std::string_view value_after_key(std::string_view text, std::size_t key_pos, std::size_t key_len) {
    std::size_t i = key_pos + key_len;
    while ((i < text.size()) && (std::isspace(static_cast<unsigned char>(text[i])) != 0)) {
        ++i;
    }
    if ((i >= text.size()) || (text[i] != ':')) {
        return {};
    }
    ++i;
    while ((i < text.size()) && (std::isspace(static_cast<unsigned char>(text[i])) != 0)) {
        ++i;
    }
    if (i >= text.size()) {
        return {};
    }
    if (text[i] == '"') {
        auto const end = skip_string(text, i);
        if (end <= i) {
            return {};
        }
        return text.substr(i, end - i);
    }
    if ((text[i] == '{') || (text[i] == '[')) {
        return span_from(text, i);
    }
    std::size_t const start = i;
    while ((i < text.size()) && (text[i] != ',') && (text[i] != '}') && (text[i] != ']') &&
           (std::isspace(static_cast<unsigned char>(text[i])) == 0)) {
        ++i;
    }
    return text.substr(start, i - start);
}

std::string_view top_level_value(std::string_view object, std::string_view key) {
    auto const key_pos = find_top_level_key(object, key);
    if (key_pos == std::string_view::npos) {
        return {};
    }
    return value_after_key(object, key_pos, key.size() + 2);
}

std::optional<std::string> scalar_text(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }
    if (value.front() == '"') {
        if ((value.size() < 2) || (value.back() != '"')) {
            return std::nullopt;
        }
        return std::string(value.substr(1, value.size() - 2));
    }
    return std::string(value);
}

std::optional<std::uint16_t> parse_u16(std::string_view value) {
    auto const text = scalar_text(value);
    if (!text.has_value() || text->empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    auto const parsed = std::strtoul(text->c_str(), &end, 0);
    if ((end == text->c_str()) || (*end != '\0') || (parsed > 65535UL)) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(parsed);
}

std::optional<bool> parse_bool(std::string_view value) {
    auto const text = scalar_text(value);
    if (!text.has_value()) {
        return std::nullopt;
    }
    if (*text == "true") {
        return true;
    }
    if (*text == "false") {
        return false;
    }
    return std::nullopt;
}

template <typename Fn>
void for_each_object(std::string_view array, Fn&& visit) {
    for (std::size_t i = 0; i < array.size();) {
        if (array[i] == '"') {
            i = skip_string(array, i);
            continue;
        }
        if (array[i] == '{') {
            auto const object = span_from(array, i);
            if (object.empty()) {
                return;
            }
            visit(object);
            i += object.size();
            continue;
        }
        ++i;
    }
}

void parse_events(std::string_view service, GatewayServiceSettings& parsed) {
    auto const events = top_level_value(service, "events");
    if (events.empty() || (events.front() != '[')) {
        return;
    }
    for_each_object(events, [&](std::string_view object) {
        auto const event_id = parse_u16(top_level_value(object, "event"));
        if (!event_id.has_value()) {
            return;
        }
        GatewayEventSettings event;
        event.event_id = *event_id;
        if (auto const field = parse_bool(top_level_value(object, "is_field")); field.has_value()) {
            event.has_is_field = true;
            event.is_field = *field;
        }
        if (auto const reliable = parse_bool(top_level_value(object, "is_reliable"));
            reliable.has_value()) {
            event.has_is_reliable = true;
            event.is_reliable = *reliable;
        }
        parsed.events.push_back(event);
    });
}

void parse_eventgroups(std::string_view service, GatewayServiceSettings& parsed) {
    auto const groups = top_level_value(service, "eventgroups");
    if (groups.empty() || (groups.front() != '[')) {
        return;
    }
    for_each_object(groups, [&](std::string_view object) {
        auto const group_id = parse_u16(top_level_value(object, "eventgroup"));
        if (!group_id.has_value()) {
            return;
        }
        GatewayEventgroupSettings group;
        group.eventgroup_id = *group_id;
        auto const multicast = top_level_value(object, "multicast");
        if (!multicast.empty() && (multicast.front() == '{')) {
            if (auto const address = scalar_text(top_level_value(multicast, "address"));
                address.has_value()) {
                group.multicast_address = *address;
            }
            if (auto const port = parse_u16(top_level_value(multicast, "port")); port.has_value()) {
                group.multicast_port = *port;
            }
        }
        if (auto const threshold = parse_u16(top_level_value(object, "threshold"));
            threshold.has_value()) {
            group.has_threshold = true;
            group.threshold = *threshold;
        }
        parsed.eventgroups.push_back(group);
    });
}

void parse_services(std::string_view json, GatewaySdSettings& settings) {
    auto const services = top_level_value(json, "services");
    if (services.empty() || (services.front() != '[')) {
        return;
    }
    for_each_object(services, [&](std::string_view object) {
        auto const service_id = parse_u16(top_level_value(object, "service"));
        auto const instance_id = parse_u16(top_level_value(object, "instance"));
        if (!service_id.has_value() || !instance_id.has_value()) {
            return;
        }
        GatewayServiceSettings service;
        service.service_id = *service_id;
        service.instance_id = *instance_id;
        if (auto const port = parse_u16(top_level_value(object, "unreliable")); port.has_value()) {
            service.unreliable_port = *port;
        }
        if (auto const port = parse_u16(top_level_value(object, "reliable")); port.has_value()) {
            service.reliable_port = *port;
        }
        parse_events(object, service);
        parse_eventgroups(object, service);
        settings.services.push_back(std::move(service));
    });
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
    parse_services(json, settings);

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
