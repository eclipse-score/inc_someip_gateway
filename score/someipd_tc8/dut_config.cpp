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

#include "dut_config.h"

#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>
#include "score/mw/log/logging.h"
#include "score/result/error_domain.h"

namespace score::someipd_tc8 {

class DutConfigErrorDomain final : public score::result::ErrorDomain {
   public:
    std::string_view MessageFor(
        const score::result::ErrorCode& code) const noexcept override {
        switch (static_cast<DutConfigErrc>(code)) {
            case DutConfigErrc::kMissingRequiredField:
                return "A required JSON field is absent.";
            case DutConfigErrc::kInvalidHexId:
                return "A hex ID string could not be parsed or overflows uint16_t.";
            case DutConfigErrc::kInvalidValue:
                return "A field value is out of the allowed range.";
            case DutConfigErrc::kTooManyElements:
                return "An array exceeds the allowed maximum count.";
            case DutConfigErrc::kFileOpenError:
                return "The config file could not be opened.";
            case DutConfigErrc::kJsonParseError:
                return "The file content is not valid JSON.";
            default:
                return "Unknown DutConfig error.";
        }
    }
};

score::result::Error MakeError(const DutConfigErrc code, const std::string_view message) {
    static constexpr DutConfigErrorDomain kDomain;
    return {static_cast<score::result::ErrorCode>(code), kDomain, message};
}

namespace {

/// Parse a hex string (e.g. "0x1234") to uint16_t.
/// Returns an error if the string is malformed or the value overflows uint16_t.
score::Result<uint16_t> ParseHexId(const std::string& s) {
    std::size_t pos = 0U;
    unsigned long value = 0UL;
    try {
        value = std::stoul(s, &pos, 16);
    } catch (const std::invalid_argument&) {
        score::mw::log::LogError() << "[dut_config] Invalid hex string: " << s;
        return score::MakeUnexpected(DutConfigErrc::kInvalidHexId, s);
    } catch (const std::out_of_range&) {
        score::mw::log::LogError() << "[dut_config] Hex value out of range: " << s;
        return score::MakeUnexpected(DutConfigErrc::kInvalidHexId, s);
    }
    if (pos != s.size()) {
        // Trailing non-hex characters (e.g. "0xGGGG")
        score::mw::log::LogError() << "[dut_config] Malformed hex string: " << s;
        return score::MakeUnexpected(DutConfigErrc::kInvalidHexId, s);
    }
    if (value > 0xFFFFU) {
        score::mw::log::LogError() << "[dut_config] Hex value overflows uint16_t: " << s;
        return score::MakeUnexpected(DutConfigErrc::kInvalidHexId, s);
    }
    return static_cast<uint16_t>(value);
}

/// Parse an array of hex eventgroup ID strings into a fixed-size array.
score::Result<std::pair<std::array<uint16_t, kMaxEventgroups>, uint8_t>>
ParseEventgroups(const nlohmann::json& j) {
    std::array<uint16_t, kMaxEventgroups> groups{};
    uint8_t count = 0U;
    for (const auto& eg : j) {
        if (count >= static_cast<uint8_t>(kMaxEventgroups)) {
            score::mw::log::LogError()
                << "[dut_config] Too many eventgroups (max " << kMaxEventgroups << ")";
            return score::MakeUnexpected(DutConfigErrc::kTooManyElements, "eventgroups");
        }
        if (!eg.is_string()) {
            score::mw::log::LogError()
                << "[dut_config] eventgroups entry is not a string";
            return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "eventgroups entry");
        }
        const std::string eg_str = eg.get<std::string>();
        auto id_result = ParseHexId(eg_str);
        if (!id_result.has_value()) {
            return score::Unexpected{id_result.error()};
        }
        groups[count] = id_result.value();
        ++count;
    }
    return std::make_pair(groups, count);
}

/// Parse the optional "initial_value" array (array of integers 0 to 255).
score::Result<std::pair<std::array<uint8_t, kMaxInitialValueBytes>, uint8_t>>
ParseInitialValue(const nlohmann::json& j) {
    std::array<uint8_t, kMaxInitialValueBytes> buf{};
    uint8_t size = 0U;
    for (const auto& byte_val : j) {
        if (size >= static_cast<uint8_t>(kMaxInitialValueBytes)) {
            score::mw::log::LogError()
                << "[dut_config] initial_value exceeds max " << kMaxInitialValueBytes << " bytes";
            return score::MakeUnexpected(DutConfigErrc::kTooManyElements, "initial_value");
        }
        if (!byte_val.is_number_integer()) {
            score::mw::log::LogError()
                << "[dut_config] initial_value entry is not an integer";
            return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "initial_value entry");
        }
        const int raw = byte_val.get<int>();
        if (raw < 0 || raw > 255) {
            score::mw::log::LogError()
                << "[dut_config] initial_value byte out of range [0,255]: " << raw;
            return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "initial_value byte");
        }
        buf[size] = static_cast<uint8_t>(raw);
        ++size;
    }
    return std::make_pair(buf, size);
}

/// Parse a single EventConfig object.
score::Result<EventConfig> ParseEvent(const nlohmann::json& j) {
    EventConfig cfg;

    if (!j.contains("name") || !j["name"].is_string()) {
        score::mw::log::LogError() << "[dut_config] event missing required 'name' field";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "events[].name");
    }
    cfg.name = j["name"].get<std::string>();
    if (cfg.name.empty()) {
        score::mw::log::LogError() << "[dut_config] event 'name' must not be empty";
        return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "events[].name");
    }

    if (!j.contains("id") || !j["id"].is_string()) {
        score::mw::log::LogError() << "[dut_config] event '" << cfg.name << "' missing 'id'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "events[].id");
    }
    auto id_result = ParseHexId(j["id"].get<std::string>());
    if (!id_result.has_value()) {
        return score::Unexpected{id_result.error()};
    }
    cfg.id = id_result.value();

    if (!j.contains("eventgroups") || !j["eventgroups"].is_array()) {
        score::mw::log::LogError()
            << "[dut_config] event '" << cfg.name << "' missing 'eventgroups'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField,
                                     "events[].eventgroups");
    }
    auto eg_result = ParseEventgroups(j["eventgroups"]);
    if (!eg_result.has_value()) {
        return score::Unexpected{eg_result.error()};
    }
    cfg.eventgroups      = eg_result.value().first;
    cfg.eventgroup_count = eg_result.value().second;

    if (!j.contains("reliable") || !j["reliable"].is_boolean()) {
        score::mw::log::LogError()
            << "[dut_config] event '" << cfg.name << "' missing 'reliable'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "events[].reliable");
    }
    cfg.reliable = j["reliable"].get<bool>();

    if (!j.contains("is_field") || !j["is_field"].is_boolean()) {
        score::mw::log::LogError()
            << "[dut_config] event '" << cfg.name << "' missing 'is_field'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "events[].is_field");
    }
    cfg.is_field = j["is_field"].get<bool>();

    if (!j.contains("cycle_ms") || !j["cycle_ms"].is_number_unsigned()) {
        score::mw::log::LogError()
            << "[dut_config] event '" << cfg.name << "' missing 'cycle_ms'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "events[].cycle_ms");
    }
    cfg.cycle_ms = j["cycle_ms"].get<uint32_t>();

    if (j.contains("initial_value") && j["initial_value"].is_array()) {
        auto iv_result = ParseInitialValue(j["initial_value"]);
        if (!iv_result.has_value()) {
            return score::Unexpected{iv_result.error()};
        }
        cfg.initial_value      = iv_result.value().first;
        cfg.initial_value_size = iv_result.value().second;
    }

    return cfg;
}

/// Parse a single FieldConfig object.
score::Result<FieldConfig> ParseField(const nlohmann::json& j) {
    FieldConfig cfg;

    if (!j.contains("name") || !j["name"].is_string()) {
        score::mw::log::LogError() << "[dut_config] field missing required 'name' field";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "fields[].name");
    }
    cfg.name = j["name"].get<std::string>();
    if (cfg.name.empty()) {
        score::mw::log::LogError() << "[dut_config] field 'name' must not be empty";
        return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "fields[].name");
    }

    if (!j.contains("notify_id") || !j["notify_id"].is_string()) {
        score::mw::log::LogError()
            << "[dut_config] field '" << cfg.name << "' missing 'notify_id'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "fields[].notify_id");
    }
    auto nid_result = ParseHexId(j["notify_id"].get<std::string>());
    if (!nid_result.has_value()) {
        return score::Unexpected{nid_result.error()};
    }
    cfg.notify_id = nid_result.value();

    if (!j.contains("getter_method_id") || !j["getter_method_id"].is_string()) {
        score::mw::log::LogError()
            << "[dut_config] field '" << cfg.name << "' missing 'getter_method_id'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField,
                                     "fields[].getter_method_id");
    }
    auto gid_result = ParseHexId(j["getter_method_id"].get<std::string>());
    if (!gid_result.has_value()) {
        return score::Unexpected{gid_result.error()};
    }
    cfg.getter_method_id = gid_result.value();

    if (j.contains("setter_method_id") && j["setter_method_id"].is_string()) {
        auto sid_result = ParseHexId(j["setter_method_id"].get<std::string>());
        if (!sid_result.has_value()) {
            return score::Unexpected{sid_result.error()};
        }
        cfg.setter_method_id = sid_result.value();
    }

    if (!j.contains("eventgroups") || !j["eventgroups"].is_array()) {
        score::mw::log::LogError()
            << "[dut_config] field '" << cfg.name << "' missing 'eventgroups'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField,
                                     "fields[].eventgroups");
    }
    auto eg_result = ParseEventgroups(j["eventgroups"]);
    if (!eg_result.has_value()) {
        return score::Unexpected{eg_result.error()};
    }
    cfg.eventgroups      = eg_result.value().first;
    cfg.eventgroup_count = eg_result.value().second;

    if (!j.contains("reliable") || !j["reliable"].is_boolean()) {
        score::mw::log::LogError()
            << "[dut_config] field '" << cfg.name << "' missing 'reliable'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "fields[].reliable");
    }
    cfg.reliable = j["reliable"].get<bool>();

    if (j.contains("initial_value") && j["initial_value"].is_array()) {
        auto iv_result = ParseInitialValue(j["initial_value"]);
        if (!iv_result.has_value()) {
            return score::Unexpected{iv_result.error()};
        }
        cfg.initial_value      = iv_result.value().first;
        cfg.initial_value_size = iv_result.value().second;
    }

    return cfg;
}

/// Parse a single MethodConfig object.
score::Result<MethodConfig> ParseMethod(const nlohmann::json& j) {
    MethodConfig cfg;

    if (!j.contains("name") || !j["name"].is_string()) {
        score::mw::log::LogError() << "[dut_config] method missing required 'name' field";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "methods[].name");
    }
    cfg.name = j["name"].get<std::string>();
    if (cfg.name.empty()) {
        score::mw::log::LogError() << "[dut_config] method 'name' must not be empty";
        return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "methods[].name");
    }

    if (!j.contains("id") || !j["id"].is_string()) {
        score::mw::log::LogError() << "[dut_config] method '" << cfg.name << "' missing 'id'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "methods[].id");
    }
    auto id_result = ParseHexId(j["id"].get<std::string>());
    if (!id_result.has_value()) {
        return score::Unexpected{id_result.error()};
    }
    cfg.id = id_result.value();

    if (!j.contains("fire_and_forget") || !j["fire_and_forget"].is_boolean()) {
        score::mw::log::LogError()
            << "[dut_config] method '" << cfg.name << "' missing 'fire_and_forget'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField,
                                     "methods[].fire_and_forget");
    }
    cfg.fire_and_forget = j["fire_and_forget"].get<bool>();

    return cfg;
}

}  // namespace

score::Result<DutConfig> LoadDutConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        score::mw::log::LogError() << "[dut_config] Cannot open config file: " << path;
        return score::MakeUnexpected(DutConfigErrc::kFileOpenError, path);
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        score::mw::log::LogError() << "[dut_config] JSON parse error in " << path << ": "
                                   << std::string_view{e.what()};
        return score::MakeUnexpected(DutConfigErrc::kJsonParseError, path);
    }

    DutConfig config;

    if (!j.contains("service_id") || !j["service_id"].is_string()) {
        score::mw::log::LogError() << "[dut_config] Missing required field 'service_id'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "service_id");
    }
    auto sid_result = ParseHexId(j["service_id"].get<std::string>());
    if (!sid_result.has_value()) {
        return score::Unexpected{sid_result.error()};
    }
    config.service_id = sid_result.value();

    if (!j.contains("instance_id") || !j["instance_id"].is_string()) {
        score::mw::log::LogError() << "[dut_config] Missing required field 'instance_id'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "instance_id");
    }
    auto iid_result = ParseHexId(j["instance_id"].get<std::string>());
    if (!iid_result.has_value()) {
        return score::Unexpected{iid_result.error()};
    }
    config.instance_id = iid_result.value();

    if (!j.contains("major_version") || !j["major_version"].is_number_unsigned()) {
        score::mw::log::LogError() << "[dut_config] Missing required field 'major_version'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "major_version");
    }
    {
        const unsigned int mv = j["major_version"].get<unsigned int>();
        if (mv > 255U) {
            score::mw::log::LogError()
                << "[dut_config] 'major_version' " << mv << " exceeds uint8_t range";
            return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "major_version");
        }
        config.major_version = static_cast<uint8_t>(mv);
    }

    if (!j.contains("minor_version") || !j["minor_version"].is_number_unsigned()) {
        score::mw::log::LogError() << "[dut_config] Missing required field 'minor_version'";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "minor_version");
    }
    config.minor_version = j["minor_version"].get<uint32_t>();

    if (!j.contains("events") || !j["events"].is_array()) {
        score::mw::log::LogError() << "[dut_config] Missing required field 'events' (array)";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "events");
    }
    if (j["events"].size() > kMaxEvents) {
        score::mw::log::LogError()
            << "[dut_config] 'events' array has " << j["events"].size()
            << " entries, max is " << kMaxEvents;
        return score::MakeUnexpected(DutConfigErrc::kTooManyElements, "events");
    }
    for (const auto& ev_json : j["events"]) {
        auto ev_result = ParseEvent(ev_json);
        if (!ev_result.has_value()) {
            return score::Unexpected{ev_result.error()};
        }
        config.events.push_back(std::move(ev_result).value());
    }

    if (!j.contains("fields") || !j["fields"].is_array()) {
        score::mw::log::LogError() << "[dut_config] Missing required field 'fields' (array)";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "fields");
    }
    if (j["fields"].size() > kMaxFields) {
        score::mw::log::LogError()
            << "[dut_config] 'fields' array has " << j["fields"].size()
            << " entries, max is " << kMaxFields;
        return score::MakeUnexpected(DutConfigErrc::kTooManyElements, "fields");
    }
    for (const auto& fld_json : j["fields"]) {
        auto fld_result = ParseField(fld_json);
        if (!fld_result.has_value()) {
            return score::Unexpected{fld_result.error()};
        }
        config.fields.push_back(std::move(fld_result).value());
    }

    if (!j.contains("methods") || !j["methods"].is_array()) {
        score::mw::log::LogError() << "[dut_config] Missing required field 'methods' (array)";
        return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "methods");
    }
    if (j["methods"].size() > kMaxMethods) {
        score::mw::log::LogError()
            << "[dut_config] 'methods' array has " << j["methods"].size()
            << " entries, max is " << kMaxMethods;
        return score::MakeUnexpected(DutConfigErrc::kTooManyElements, "methods");
    }
    for (const auto& mth_json : j["methods"]) {
        auto mth_result = ParseMethod(mth_json);
        if (!mth_result.has_value()) {
            return score::Unexpected{mth_result.error()};
        }
        config.methods.push_back(std::move(mth_result).value());
    }

    return config;
}

}  // namespace score::someipd_tc8
