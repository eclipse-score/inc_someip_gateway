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

#ifndef SCORE_SOMEIPD_TC8_DUT_CONFIG_H
#define SCORE_SOMEIPD_TC8_DUT_CONFIG_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <score/optional.hpp>
#include "score/result/result.h"

namespace score::someipd_tc8 {

enum class DutConfigErrc : score::result::ErrorCode {
    kMissingRequiredField,  ///< A required JSON field is absent.
    kInvalidHexId,          ///< A hex ID string could not be parsed or overflows uint16_t.
    kInvalidValue,          ///< A field value is out of the allowed range.
    kTooManyElements,       ///< An array exceeds the allowed maximum count.
    kFileOpenError,         ///< The config file could not be opened.
    kJsonParseError,        ///< The file content is not valid JSON.
};

score::result::Error MakeError(DutConfigErrc code, std::string_view message = "");

static constexpr std::size_t kMaxEventgroups       = 3U;
static constexpr std::size_t kMaxInitialValueBytes = 64U;
static constexpr std::size_t kMaxEvents            = 16U;
static constexpr std::size_t kMaxFields            = 8U;
static constexpr std::size_t kMaxMethods           = 64U;

struct EventConfig {
    std::string   name;
    uint16_t      id{0U};
    std::array<uint16_t, kMaxEventgroups> eventgroups{};
    uint8_t       eventgroup_count{0U};
    bool          reliable{false};   // true: RT_RELIABLE (TCP), false: RT_UNRELIABLE (UDP)
    bool          is_field{false};   // true: ET_FIELD, false: ET_EVENT
    uint32_t      cycle_ms{0U};      // 0: seed only, nonzero: periodic notify interval in ms
    std::array<uint8_t, kMaxInitialValueBytes> initial_value{};
    uint8_t       initial_value_size{0U};
};

struct FieldConfig {
    std::string   name;
    uint16_t      notify_id{0U};
    uint16_t      getter_method_id{0U};
    score::cpp::optional<uint16_t> setter_method_id;  // absent means read-only field
    std::array<uint16_t, kMaxEventgroups> eventgroups{};
    uint8_t       eventgroup_count{0U};
    bool          reliable{false};
    std::array<uint8_t, kMaxInitialValueBytes> initial_value{};
    uint8_t       initial_value_size{0U};
};

struct MethodConfig {
    std::string name;
    uint16_t    id{0U};
    bool        fire_and_forget{false};
};

struct DutConfig {
    uint16_t service_id{0U};
    uint16_t instance_id{0U};
    uint8_t  major_version{0U};
    uint32_t minor_version{0U};
    std::vector<EventConfig>  events;
    std::vector<FieldConfig>  fields;
    std::vector<MethodConfig> methods;
};

/// Parse a tc8_dut_config.json file and validate all required fields.
/// Returns a populated DutConfig on success, or a descriptive error.
score::Result<DutConfig> LoadDutConfig(const std::string& path);

}  // namespace score::someipd_tc8

#endif  // SCORE_SOMEIPD_TC8_DUT_CONFIG_H
