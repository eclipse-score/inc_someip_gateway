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

#include <score/json/json_parser.h>
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

template <typename T>
score::Result<T> GetField(const score::json::Object& obj, std::string_view key,
                          DutConfigErrc missing_err, DutConfigErrc type_err) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return score::MakeUnexpected(missing_err);
    }
    auto result = it->second.As<T>();
    if (!result.has_value()) {
        return score::MakeUnexpected(type_err);
    }
    return result.value();
}

score::Result<std::string> GetStringField(const score::json::Object& obj, std::string_view key,
                                           DutConfigErrc missing_err, DutConfigErrc type_err) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        return score::MakeUnexpected(missing_err);
    }
    auto result = it->second.As<std::string>();
    if (!result.has_value()) {
        return score::MakeUnexpected(type_err);
    }
    return std::string{result.value().get()};
}

score::Result<std::pair<std::array<uint16_t, kMaxEventgroups>, uint8_t>>
ParseEventgroups(const score::json::List& list) {
    std::array<uint16_t, kMaxEventgroups> groups{};
    uint8_t count = 0U;
    for (const auto& eg : list) {
        if (count >= static_cast<uint8_t>(kMaxEventgroups)) {
            score::mw::log::LogError()
                << "[dut_config] Too many eventgroups (max " << kMaxEventgroups << ")";
            return score::MakeUnexpected(DutConfigErrc::kTooManyElements, "eventgroups");
        }
        auto str_result = eg.As<std::string>();
        if (!str_result.has_value()) {
            score::mw::log::LogError() << "[dut_config] eventgroups entry is not a string";
            return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "eventgroups entry");
        }
        std::string eg_str{str_result.value().get()};
        auto id_result = ParseHexId(eg_str);
        if (!id_result.has_value()) {
            return score::Unexpected{id_result.error()};
        }
        groups[count] = id_result.value();
        ++count;
    }
    return std::make_pair(groups, count);
}

score::Result<std::pair<std::array<uint8_t, kMaxInitialValueBytes>, uint8_t>>
ParseInitialValue(const score::json::List& list) {
    std::array<uint8_t, kMaxInitialValueBytes> buf{};
    uint8_t size = 0U;
    for (const auto& byte_val : list) {
        if (size >= static_cast<uint8_t>(kMaxInitialValueBytes)) {
            score::mw::log::LogError()
                << "[dut_config] initial_value exceeds max " << kMaxInitialValueBytes << " bytes";
            return score::MakeUnexpected(DutConfigErrc::kTooManyElements, "initial_value");
        }
        auto int_result = byte_val.As<int64_t>();
        if (!int_result.has_value()) {
            score::mw::log::LogError() << "[dut_config] initial_value entry is not an integer";
            return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "initial_value entry");
        }
        const int64_t raw = int_result.value();
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

score::Result<EventConfig> ParseEvent(const score::json::Object& obj) {
    EventConfig cfg;

    auto name_result = GetStringField(obj, "name",
                                      DutConfigErrc::kMissingRequiredField,
                                      DutConfigErrc::kMissingRequiredField);
    if (!name_result.has_value()) {
        score::mw::log::LogError() << "[dut_config] event missing required 'name' field";
        return score::Unexpected{name_result.error()};
    }
    cfg.name = std::move(name_result).value();
    if (cfg.name.empty()) {
        score::mw::log::LogError() << "[dut_config] event 'name' must not be empty";
        return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "events[].name");
    }

    auto id_str = GetStringField(obj, "id",
                                  DutConfigErrc::kMissingRequiredField,
                                  DutConfigErrc::kMissingRequiredField);
    if (!id_str.has_value()) {
        score::mw::log::LogError() << "[dut_config] event '" << cfg.name << "' missing 'id'";
        return score::Unexpected{id_str.error()};
    }
    auto id_result = ParseHexId(id_str.value());
    if (!id_result.has_value()) {
        return score::Unexpected{id_result.error()};
    }
    cfg.id = id_result.value();

    {
        auto it = obj.find("eventgroups");
        if (it == obj.end()) {
            score::mw::log::LogError()
                << "[dut_config] event '" << cfg.name << "' missing 'eventgroups'";
            return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField,
                                         "events[].eventgroups");
        }
        auto list_result = it->second.As<score::json::List>();
        if (!list_result.has_value()) {
            score::mw::log::LogError()
                << "[dut_config] event '" << cfg.name << "' 'eventgroups' is not an array";
            return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField,
                                         "events[].eventgroups");
        }
        auto eg_result = ParseEventgroups(list_result.value().get());
        if (!eg_result.has_value()) {
            return score::Unexpected{eg_result.error()};
        }
        cfg.eventgroups      = eg_result.value().first;
        cfg.eventgroup_count = eg_result.value().second;
    }

    auto reliable_result = GetField<bool>(obj, "reliable",
                                          DutConfigErrc::kMissingRequiredField,
                                          DutConfigErrc::kMissingRequiredField);
    if (!reliable_result.has_value()) {
        score::mw::log::LogError()
            << "[dut_config] event '" << cfg.name << "' missing 'reliable'";
        return score::Unexpected{reliable_result.error()};
    }
    cfg.reliable = reliable_result.value();

    auto is_field_result = GetField<bool>(obj, "is_field",
                                          DutConfigErrc::kMissingRequiredField,
                                          DutConfigErrc::kMissingRequiredField);
    if (!is_field_result.has_value()) {
        score::mw::log::LogError()
            << "[dut_config] event '" << cfg.name << "' missing 'is_field'";
        return score::Unexpected{is_field_result.error()};
    }
    cfg.is_field = is_field_result.value();

    auto cycle_ms_result = GetField<uint32_t>(obj, "cycle_ms",
                                               DutConfigErrc::kMissingRequiredField,
                                               DutConfigErrc::kMissingRequiredField);
    if (!cycle_ms_result.has_value()) {
        score::mw::log::LogError()
            << "[dut_config] event '" << cfg.name << "' missing 'cycle_ms'";
        return score::Unexpected{cycle_ms_result.error()};
    }
    cfg.cycle_ms = cycle_ms_result.value();

    {
        auto it = obj.find("initial_value");
        if (it != obj.end()) {
            auto list_result = it->second.As<score::json::List>();
            if (list_result.has_value()) {
                auto iv_result = ParseInitialValue(list_result.value().get());
                if (!iv_result.has_value()) {
                    return score::Unexpected{iv_result.error()};
                }
                cfg.initial_value      = iv_result.value().first;
                cfg.initial_value_size = iv_result.value().second;
            }
        }
    }

    return cfg;
}

score::Result<FieldConfig> ParseField(const score::json::Object& obj) {
    FieldConfig cfg;

    auto name_result = GetStringField(obj, "name",
                                      DutConfigErrc::kMissingRequiredField,
                                      DutConfigErrc::kMissingRequiredField);
    if (!name_result.has_value()) {
        score::mw::log::LogError() << "[dut_config] field missing required 'name' field";
        return score::Unexpected{name_result.error()};
    }
    cfg.name = std::move(name_result).value();
    if (cfg.name.empty()) {
        score::mw::log::LogError() << "[dut_config] field 'name' must not be empty";
        return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "fields[].name");
    }

    auto nid_str = GetStringField(obj, "notify_id",
                                   DutConfigErrc::kMissingRequiredField,
                                   DutConfigErrc::kMissingRequiredField);
    if (!nid_str.has_value()) {
        score::mw::log::LogError()
            << "[dut_config] field '" << cfg.name << "' missing 'notify_id'";
        return score::Unexpected{nid_str.error()};
    }
    auto nid_result = ParseHexId(nid_str.value());
    if (!nid_result.has_value()) {
        return score::Unexpected{nid_result.error()};
    }
    cfg.notify_id = nid_result.value();

    auto gid_str = GetStringField(obj, "getter_method_id",
                                   DutConfigErrc::kMissingRequiredField,
                                   DutConfigErrc::kMissingRequiredField);
    if (!gid_str.has_value()) {
        score::mw::log::LogError()
            << "[dut_config] field '" << cfg.name << "' missing 'getter_method_id'";
        return score::Unexpected{gid_str.error()};
    }
    auto gid_result = ParseHexId(gid_str.value());
    if (!gid_result.has_value()) {
        return score::Unexpected{gid_result.error()};
    }
    cfg.getter_method_id = gid_result.value();

    {
        auto sid_str = GetStringField(obj, "setter_method_id",
                                       DutConfigErrc::kMissingRequiredField,
                                       DutConfigErrc::kInvalidValue);
        if (sid_str.has_value()) {
            auto sid_result = ParseHexId(sid_str.value());
            if (!sid_result.has_value()) {
                return score::Unexpected{sid_result.error()};
            }
            cfg.setter_method_id = sid_result.value();
        }
    }

    {
        auto it = obj.find("eventgroups");
        if (it == obj.end()) {
            score::mw::log::LogError()
                << "[dut_config] field '" << cfg.name << "' missing 'eventgroups'";
            return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField,
                                         "fields[].eventgroups");
        }
        auto list_result = it->second.As<score::json::List>();
        if (!list_result.has_value()) {
            score::mw::log::LogError()
                << "[dut_config] field '" << cfg.name << "' 'eventgroups' is not an array";
            return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField,
                                         "fields[].eventgroups");
        }
        auto eg_result = ParseEventgroups(list_result.value().get());
        if (!eg_result.has_value()) {
            return score::Unexpected{eg_result.error()};
        }
        cfg.eventgroups      = eg_result.value().first;
        cfg.eventgroup_count = eg_result.value().second;
    }

    auto reliable_result = GetField<bool>(obj, "reliable",
                                          DutConfigErrc::kMissingRequiredField,
                                          DutConfigErrc::kMissingRequiredField);
    if (!reliable_result.has_value()) {
        score::mw::log::LogError()
            << "[dut_config] field '" << cfg.name << "' missing 'reliable'";
        return score::Unexpected{reliable_result.error()};
    }
    cfg.reliable = reliable_result.value();

    {
        auto it = obj.find("initial_value");
        if (it != obj.end()) {
            auto list_result = it->second.As<score::json::List>();
            if (list_result.has_value()) {
                auto iv_result = ParseInitialValue(list_result.value().get());
                if (!iv_result.has_value()) {
                    return score::Unexpected{iv_result.error()};
                }
                cfg.initial_value      = iv_result.value().first;
                cfg.initial_value_size = iv_result.value().second;
            }
        }
    }

    return cfg;
}

score::Result<MethodConfig> ParseMethod(const score::json::Object& obj) {
    MethodConfig cfg;

    auto name_result = GetStringField(obj, "name",
                                      DutConfigErrc::kMissingRequiredField,
                                      DutConfigErrc::kMissingRequiredField);
    if (!name_result.has_value()) {
        score::mw::log::LogError() << "[dut_config] method missing required 'name' field";
        return score::Unexpected{name_result.error()};
    }
    cfg.name = std::move(name_result).value();
    if (cfg.name.empty()) {
        score::mw::log::LogError() << "[dut_config] method 'name' must not be empty";
        return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "methods[].name");
    }

    auto id_str = GetStringField(obj, "id",
                                  DutConfigErrc::kMissingRequiredField,
                                  DutConfigErrc::kMissingRequiredField);
    if (!id_str.has_value()) {
        score::mw::log::LogError() << "[dut_config] method '" << cfg.name << "' missing 'id'";
        return score::Unexpected{id_str.error()};
    }
    auto id_result = ParseHexId(id_str.value());
    if (!id_result.has_value()) {
        return score::Unexpected{id_result.error()};
    }
    cfg.id = id_result.value();

    auto fnf_result = GetField<bool>(obj, "fire_and_forget",
                                     DutConfigErrc::kMissingRequiredField,
                                     DutConfigErrc::kMissingRequiredField);
    if (!fnf_result.has_value()) {
        score::mw::log::LogError()
            << "[dut_config] method '" << cfg.name << "' missing 'fire_and_forget'";
        return score::Unexpected{fnf_result.error()};
    }
    cfg.fire_and_forget = fnf_result.value();

    return cfg;
}

}  // namespace

score::Result<DutConfig> LoadDutConfig(const std::string& path) {
    {
        std::ifstream probe(path);
        if (!probe.is_open()) {
            score::mw::log::LogError() << "[dut_config] Cannot open config file: " << path;
            return score::MakeUnexpected(DutConfigErrc::kFileOpenError, path);
        }
    }

    score::Result<score::json::Any> parse_result =
        score::json::JsonParser{}.FromFile(path);
    if (!parse_result.has_value()) {
        score::mw::log::LogError() << "[dut_config] JSON parse error in " << path;
        return score::MakeUnexpected(DutConfigErrc::kJsonParseError, path);
    }

    auto obj_result = parse_result.value().As<score::json::Object>();
    if (!obj_result.has_value()) {
        score::mw::log::LogError() << "[dut_config] JSON root is not an object in " << path;
        return score::MakeUnexpected(DutConfigErrc::kJsonParseError, path);
    }
    const score::json::Object& j = obj_result.value().get();

    DutConfig config;

    auto sid_str = GetStringField(j, "service_id",
                                   DutConfigErrc::kMissingRequiredField,
                                   DutConfigErrc::kMissingRequiredField);
    if (!sid_str.has_value()) {
        score::mw::log::LogError() << "[dut_config] Missing required field 'service_id'";
        return score::Unexpected{sid_str.error()};
    }
    auto sid_result = ParseHexId(sid_str.value());
    if (!sid_result.has_value()) {
        return score::Unexpected{sid_result.error()};
    }
    config.service_id = sid_result.value();

    auto iid_str = GetStringField(j, "instance_id",
                                   DutConfigErrc::kMissingRequiredField,
                                   DutConfigErrc::kMissingRequiredField);
    if (!iid_str.has_value()) {
        score::mw::log::LogError() << "[dut_config] Missing required field 'instance_id'";
        return score::Unexpected{iid_str.error()};
    }
    auto iid_result = ParseHexId(iid_str.value());
    if (!iid_result.has_value()) {
        return score::Unexpected{iid_result.error()};
    }
    config.instance_id = iid_result.value();

    auto mv_result = GetField<uint64_t>(j, "major_version",
                                        DutConfigErrc::kMissingRequiredField,
                                        DutConfigErrc::kMissingRequiredField);
    if (!mv_result.has_value()) {
        score::mw::log::LogError() << "[dut_config] Missing required field 'major_version'";
        return score::Unexpected{mv_result.error()};
    }
    {
        const uint64_t mv = mv_result.value();
        if (mv > 255U) {
            score::mw::log::LogError()
                << "[dut_config] 'major_version' " << mv << " exceeds uint8_t range";
            return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "major_version");
        }
        config.major_version = static_cast<uint8_t>(mv);
    }

    auto minor_result = GetField<uint32_t>(j, "minor_version",
                                            DutConfigErrc::kMissingRequiredField,
                                            DutConfigErrc::kMissingRequiredField);
    if (!minor_result.has_value()) {
        score::mw::log::LogError() << "[dut_config] Missing required field 'minor_version'";
        return score::Unexpected{minor_result.error()};
    }
    config.minor_version = minor_result.value();

    {
        auto it = j.find("events");
        if (it == j.end()) {
            score::mw::log::LogError()
                << "[dut_config] Missing required field 'events' (array)";
            return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "events");
        }
        auto events_list = it->second.As<score::json::List>();
        if (!events_list.has_value()) {
            score::mw::log::LogError()
                << "[dut_config] Missing required field 'events' (array)";
            return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "events");
        }
        const score::json::List& events = events_list.value().get();
        if (events.size() > kMaxEvents) {
            score::mw::log::LogError()
                << "[dut_config] 'events' array has " << events.size()
                << " entries, max is " << kMaxEvents;
            return score::MakeUnexpected(DutConfigErrc::kTooManyElements, "events");
        }
        for (const auto& ev_any : events) {
            auto ev_obj = ev_any.As<score::json::Object>();
            if (!ev_obj.has_value()) {
                return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "events[] entry");
            }
            auto ev_result = ParseEvent(ev_obj.value().get());
            if (!ev_result.has_value()) {
                return score::Unexpected{ev_result.error()};
            }
            config.events.push_back(std::move(ev_result).value());
        }
    }

    {
        auto it = j.find("fields");
        if (it == j.end()) {
            score::mw::log::LogError()
                << "[dut_config] Missing required field 'fields' (array)";
            return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "fields");
        }
        auto fields_list = it->second.As<score::json::List>();
        if (!fields_list.has_value()) {
            score::mw::log::LogError()
                << "[dut_config] Missing required field 'fields' (array)";
            return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "fields");
        }
        const score::json::List& fields = fields_list.value().get();
        if (fields.size() > kMaxFields) {
            score::mw::log::LogError()
                << "[dut_config] 'fields' array has " << fields.size()
                << " entries, max is " << kMaxFields;
            return score::MakeUnexpected(DutConfigErrc::kTooManyElements, "fields");
        }
        for (const auto& fld_any : fields) {
            auto fld_obj = fld_any.As<score::json::Object>();
            if (!fld_obj.has_value()) {
                return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "fields[] entry");
            }
            auto fld_result = ParseField(fld_obj.value().get());
            if (!fld_result.has_value()) {
                return score::Unexpected{fld_result.error()};
            }
            config.fields.push_back(std::move(fld_result).value());
        }
    }

    {
        auto it = j.find("methods");
        if (it == j.end()) {
            score::mw::log::LogError()
                << "[dut_config] Missing required field 'methods' (array)";
            return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "methods");
        }
        auto methods_list = it->second.As<score::json::List>();
        if (!methods_list.has_value()) {
            score::mw::log::LogError()
                << "[dut_config] Missing required field 'methods' (array)";
            return score::MakeUnexpected(DutConfigErrc::kMissingRequiredField, "methods");
        }
        const score::json::List& methods = methods_list.value().get();
        if (methods.size() > kMaxMethods) {
            score::mw::log::LogError()
                << "[dut_config] 'methods' array has " << methods.size()
                << " entries, max is " << kMaxMethods;
            return score::MakeUnexpected(DutConfigErrc::kTooManyElements, "methods");
        }
        for (const auto& mth_any : methods) {
            auto mth_obj = mth_any.As<score::json::Object>();
            if (!mth_obj.has_value()) {
                return score::MakeUnexpected(DutConfigErrc::kInvalidValue, "methods[] entry");
            }
            auto mth_result = ParseMethod(mth_obj.value().get());
            if (!mth_result.has_value()) {
                return score::Unexpected{mth_result.error()};
            }
            config.methods.push_back(std::move(mth_result).value());
        }
    }

    return config;
}

}  // namespace score::someipd_tc8
