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

#include <cstdlib>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

namespace score::someipd_tc8 {
namespace {

/// Returns a writable temp directory provided by the Bazel test runner.
std::string TestTmpDir() {
    const char* dir = std::getenv("TEST_TMPDIR");
    if (dir == nullptr) {
        return "/tmp";
    }
    return std::string(dir);
}

/// Write content to a temp file and return its path.
std::string WriteTempFile(const std::string& filename, const std::string& content) {
    const std::string path = TestTmpDir() + "/" + filename;
    std::ofstream file(path);
    file << content;
    return path;
}

const std::string kValidMinimalJson = R"({
  "service_id": "0x1234",
  "instance_id": "0x5678",
  "major_version": 1,
  "minor_version": 0,
  "events": [],
  "fields": [],
  "methods": []
})";

const std::string kValidSingleEventJson = R"({
  "service_id": "0x1234",
  "instance_id": "0x5678",
  "major_version": 1,
  "minor_version": 0,
  "events": [
    {
      "name": "TestEventUINT8",
      "id": "0x8001",
      "eventgroups": ["0x0002"],
      "reliable": false,
      "is_field": true,
      "cycle_ms": 500,
      "initial_value": [222, 173]
    }
  ],
  "fields": [],
  "methods": []
})";

TEST(LoadDutConfig, ValidMinimalJsonLoadsSuccessfully) {
    const std::string path = WriteTempFile("valid_minimal.json", kValidMinimalJson);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().service_id, 0x1234U);
    EXPECT_EQ(result.value().instance_id, 0x5678U);
    EXPECT_EQ(result.value().major_version, 1U);
    EXPECT_EQ(result.value().minor_version, 0U);
    EXPECT_TRUE(result.value().events.empty());
}

TEST(LoadDutConfig, ValidSingleEventParsesCorrectly) {
    const std::string path = WriteTempFile("valid_single_event.json", kValidSingleEventJson);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().events.size(), 1U);
    EXPECT_EQ(result.value().events[0].id, 0x8001U);
    EXPECT_EQ(result.value().events[0].name, "TestEventUINT8");
    EXPECT_EQ(result.value().events[0].cycle_ms, 500U);
    EXPECT_FALSE(result.value().events[0].reliable);
    EXPECT_TRUE(result.value().events[0].is_field);
    EXPECT_EQ(result.value().events[0].initial_value_size, 2U);
    EXPECT_EQ(result.value().events[0].initial_value[0], 222U);
    EXPECT_EQ(result.value().events[0].initial_value[1], 173U);
}

TEST(LoadDutConfig, MissingServiceIdReturnsError) {
    const std::string json = R"({
      "instance_id": "0x5678",
      "major_version": 1,
      "minor_version": 0,
      "events": [],
      "fields": [],
      "methods": []
    })";
    const std::string path = WriteTempFile("missing_service_id.json", json);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_FALSE(result.has_value());
}

TEST(LoadDutConfig, MissingInstanceIdReturnsError) {
    const std::string json = R"({
      "service_id": "0x1234",
      "major_version": 1,
      "minor_version": 0,
      "events": [],
      "fields": [],
      "methods": []
    })";
    const std::string path = WriteTempFile("missing_instance_id.json", json);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_FALSE(result.has_value());
}

TEST(LoadDutConfig, MalformedHexStringReturnsError) {
    const std::string json = R"({
      "service_id": "0xGGGG",
      "instance_id": "0x5678",
      "major_version": 1,
      "minor_version": 0,
      "events": [],
      "fields": [],
      "methods": []
    })";
    const std::string path = WriteTempFile("malformed_hex.json", json);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_FALSE(result.has_value());
}

TEST(LoadDutConfig, InitialValueOutOfRangeReturnsError) {
    const std::string json = R"({
      "service_id": "0x1234",
      "instance_id": "0x5678",
      "major_version": 1,
      "minor_version": 0,
      "events": [
        {
          "name": "TestEvent",
          "id": "0x8001",
          "eventgroups": ["0x0002"],
          "reliable": false,
          "is_field": false,
          "cycle_ms": 500,
          "initial_value": [256]
        }
      ],
      "fields": [],
      "methods": []
    })";
    const std::string path = WriteTempFile("invalid_initial_value.json", json);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_FALSE(result.has_value());
}

TEST(LoadDutConfig, EmptyEventsArrayLoadsSuccessfully) {
    const std::string path = WriteTempFile("empty_events.json", kValidMinimalJson);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result.value().events.empty());
}

TEST(LoadDutConfig, TooManyEventsReturnsError) {
    std::string json = R"({
      "service_id": "0x1234",
      "instance_id": "0x5678",
      "major_version": 1,
      "minor_version": 0,
      "events": [)";
    for (std::size_t i = 0U; i <= kMaxEvents; ++i) {
        if (i > 0U) {
            json += ",";
        }
        json += R"({"name":"ev","id":"0x8001","eventgroups":["0x0002"],)"
                R"("reliable":false,"is_field":false,"cycle_ms":500})";
    }
    json += R"(], "fields": [], "methods": [] })";

    const std::string path = WriteTempFile("too_many_events.json", json);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_FALSE(result.has_value());
}

TEST(LoadDutConfig, FieldWithoutSetterIsReadOnly) {
    const std::string json = R"({
      "service_id": "0x1234",
      "instance_id": "0x5678",
      "major_version": 1,
      "minor_version": 0,
      "events": [],
      "fields": [
        {
          "name": "InterfaceVersion",
          "notify_id": "0x8005",
          "getter_method_id": "0x25",
          "eventgroups": ["0x0002"],
          "reliable": false,
          "initial_value": [1, 0]
        }
      ],
      "methods": []
    })";
    const std::string path = WriteTempFile("readonly_field.json", json);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value().fields.size(), 1U);
    EXPECT_EQ(result.value().fields[0].getter_method_id, 0x25U);
    EXPECT_FALSE(result.value().fields[0].setter_method_id.has_value());
}

TEST(LoadDutConfig, HexValueOverflowUint16ReturnsError) {
    const std::string json = R"({
      "service_id": "0x10000",
      "instance_id": "0x5678",
      "major_version": 1,
      "minor_version": 0,
      "events": [],
      "fields": [],
      "methods": []
    })";
    const std::string path = WriteTempFile("hex_overflow.json", json);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_FALSE(result.has_value());
}

TEST(LoadDutConfig, WrongTypeInEventgroupsReturnsError) {
    const std::string json = R"({
      "service_id": "0x1234",
      "instance_id": "0x5678",
      "major_version": 1,
      "minor_version": 0,
      "events": [
        {
          "name": "TestEvent",
          "id": "0x8001",
          "eventgroups": [2],
          "reliable": false,
          "is_field": false,
          "cycle_ms": 500
        }
      ],
      "fields": [],
      "methods": []
    })";
    const std::string path = WriteTempFile("wrong_type_eventgroups.json", json);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_FALSE(result.has_value());
}

TEST(LoadDutConfig, WrongTypeInInitialValueReturnsError) {
    const std::string json = R"({
      "service_id": "0x1234",
      "instance_id": "0x5678",
      "major_version": 1,
      "minor_version": 0,
      "events": [
        {
          "name": "TestEvent",
          "id": "0x8001",
          "eventgroups": ["0x0002"],
          "reliable": false,
          "is_field": false,
          "cycle_ms": 500,
          "initial_value": ["not_an_int"]
        }
      ],
      "fields": [],
      "methods": []
    })";
    const std::string path = WriteTempFile("wrong_type_initial_value.json", json);
    score::Result<DutConfig> result = LoadDutConfig(path);

    ASSERT_FALSE(result.has_value());
}

TEST(LoadDutConfig, NonExistentFileReturnsError) {
    score::Result<DutConfig> result = LoadDutConfig("/nonexistent/path/to/config.json");
    ASSERT_FALSE(result.has_value());
}

TEST(LoadDutConfig, MalformedJsonReturnsError) {
    const std::string path = WriteTempFile("malformed.json", "{ not: valid json ,,, }");
    score::Result<DutConfig> result = LoadDutConfig(path);
    ASSERT_FALSE(result.has_value());
}

}  // namespace
}  // namespace score::someipd_tc8
