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

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "src/config/mw_someip_config_generated.h"
#include "src/serializer/pre_serialized_data.h"
#include "src/serializer/serializer.h"

namespace {

using score::someip_gateway::serializer::get_size_of_pre_serialized_data;
using score::someip_gateway::serializer::PreSerializedData;
namespace config = score::mw_someip_config;

constexpr uint32_t kTestMaxMessageSize = 256;
constexpr uint32_t kTestMethodRequestMaxSize = 128;
constexpr uint32_t kTestMethodResponseMaxSize = 64;
constexpr uint16_t kTestEventId = 1;
constexpr uint16_t kTestMethodId = 10;
const char* const kTestServiceTypeName = "test_service";
const char* const kTestEventName = "test_event";
const char* const kTestMethodName = "test_method";

/// Builds a flatbuffer config binary with a single service type containing one event and one
/// method, each with NullSerializerConfig.
std::vector<uint8_t> build_test_config() {
    flatbuffers::FlatBufferBuilder fbb;

    auto null_config = config::CreateNullSerializerConfig(fbb, kTestMaxMessageSize);
    auto method_request_config = config::CreateNullSerializerConfig(fbb, kTestMethodRequestMaxSize);
    auto method_response_config =
        config::CreateNullSerializerConfig(fbb, kTestMethodResponseMaxSize);

    auto event =
        config::CreateEvent(fbb, kTestEventId, fbb.CreateString(kTestEventName),
                            config::SerializationConfig_NullSerializerConfig, null_config.Union());

    auto method = config::CreateMethod(
        fbb, kTestMethodId, fbb.CreateString(kTestMethodName),
        config::SerializationConfig_NullSerializerConfig, method_request_config.Union(),
        config::SerializationConfig_NullSerializerConfig, method_response_config.Union());

    std::vector<flatbuffers::Offset<config::Event>> events_vec = {event};
    std::vector<flatbuffers::Offset<config::Method>> methods_vec = {method};

    auto service_type = config::CreateServiceTypeDirect(
        fbb, kTestServiceTypeName, /*service_id=*/1, /*service_version_major=*/1,
        /*service_version_minor=*/0, &events_vec, &methods_vec);

    std::vector<flatbuffers::Offset<config::ServiceType>> service_types_vec = {service_type};
    auto root = config::CreateRootDirect(fbb, &service_types_vec);
    fbb.Finish(root);

    return {fbb.GetBufferPointer(), fbb.GetBufferPointer() + fbb.GetSize()};
}

/// Writes a binary config to a temporary file and returns the path.
std::string write_config_to_file(const std::vector<uint8_t>& config_data) {
    std::string path = testing::TempDir() + "null_serializer_test_config.bin";
    std::ofstream file(path, std::ios::binary | std::ios::out);
    file.write(reinterpret_cast<const char*>(config_data.data()),
               static_cast<std::streamsize>(config_data.size()));
    file.close();
    return path;
}

class NullSerializer_test : public ::testing::Test {
   protected:
    void SetUp() override {
        auto config_data = build_test_config();
        config_path_ = write_config_to_file(config_data);
        auto result = score_com_serializer_init(config_path_.c_str(), config_path_.size());
        ASSERT_EQ(result, score_com_serializer_result_ok);
    }

    void TearDown() override {
        auto result = score_com_serializer_deinit();
        EXPECT_EQ(result, score_com_serializer_result_ok);
    }

    const score_com_serializer* get_event_serializer() {
        const score_com_serializer* serializer = nullptr;
        std::string service(kTestServiceTypeName);
        std::string element(kTestEventName);
        auto result = score_com_serializer_get(service.c_str(), service.size(),
                                               score_com_serializer_element_type_event,
                                               element.c_str(), element.size(), &serializer);
        EXPECT_EQ(result, score_com_serializer_result_ok);
        return serializer;
    }

    std::string config_path_;
};

// --- Init / Deinit ---

TEST_F(NullSerializer_test, init_with_nonexistent_file_fails) {
    ASSERT_EQ(score_com_serializer_deinit(), score_com_serializer_result_ok);
    const std::string bad_path = "/nonexistent/path.bin";
    auto result = score_com_serializer_init(bad_path.c_str(), bad_path.size());
    EXPECT_EQ(result, score_com_serializer_result_serializer_nonexistent);
}

TEST_F(NullSerializer_test, init_with_empty_file_fails) {
    ASSERT_EQ(score_com_serializer_deinit(), score_com_serializer_result_ok);
    std::string path = testing::TempDir() + "empty_config.bin";
    std::ofstream file(path, std::ios::binary | std::ios::out);
    file.close();
    auto result = score_com_serializer_init(path.c_str(), path.size());
    EXPECT_EQ(result, score_com_serializer_result_serializer_nonexistent);
}

// --- score_com_serializer_get ---

TEST_F(NullSerializer_test, get_event_serializer_succeeds) {
    const score_com_serializer* serializer = nullptr;
    std::string service(kTestServiceTypeName);
    std::string element(kTestEventName);
    auto result = score_com_serializer_get(service.c_str(), service.size(),
                                           score_com_serializer_element_type_event, element.c_str(),
                                           element.size(), &serializer);
    EXPECT_EQ(result, score_com_serializer_result_ok);
    EXPECT_NE(serializer, nullptr);
}

TEST_F(NullSerializer_test, get_method_call_serializer_succeeds) {
    const score_com_serializer* serializer = nullptr;
    std::string service(kTestServiceTypeName);
    std::string element(kTestMethodName);
    auto result = score_com_serializer_get(service.c_str(), service.size(),
                                           score_com_serializer_element_type_method_call,
                                           element.c_str(), element.size(), &serializer);
    EXPECT_EQ(result, score_com_serializer_result_ok);
    EXPECT_NE(serializer, nullptr);
}

TEST_F(NullSerializer_test, get_method_response_serializer_succeeds) {
    const score_com_serializer* serializer = nullptr;
    std::string service(kTestServiceTypeName);
    std::string element(kTestMethodName);
    auto result = score_com_serializer_get(service.c_str(), service.size(),
                                           score_com_serializer_element_type_method_response,
                                           element.c_str(), element.size(), &serializer);
    EXPECT_EQ(result, score_com_serializer_result_ok);
    EXPECT_NE(serializer, nullptr);
}

TEST_F(NullSerializer_test, get_with_unknown_service_type_fails) {
    const score_com_serializer* serializer = nullptr;
    std::string service("nonexistent_service");
    std::string element(kTestEventName);
    auto result = score_com_serializer_get(service.c_str(), service.size(),
                                           score_com_serializer_element_type_event, element.c_str(),
                                           element.size(), &serializer);
    EXPECT_EQ(result, score_com_serializer_result_serializer_nonexistent);
}

TEST_F(NullSerializer_test, get_with_unknown_element_name_fails) {
    const score_com_serializer* serializer = nullptr;
    std::string service(kTestServiceTypeName);
    std::string element("nonexistent_event");
    auto result = score_com_serializer_get(service.c_str(), service.size(),
                                           score_com_serializer_element_type_event, element.c_str(),
                                           element.size(), &serializer);
    EXPECT_EQ(result, score_com_serializer_result_serializer_nonexistent);
}

TEST_F(NullSerializer_test, get_with_null_serializer_output_fails) {
    std::string service(kTestServiceTypeName);
    std::string element(kTestEventName);
    auto result = score_com_serializer_get(service.c_str(), service.size(),
                                           score_com_serializer_element_type_event, element.c_str(),
                                           element.size(), nullptr);
    EXPECT_EQ(result, score_com_serializer_result_general_failure);
}

// --- max_serialized_size ---

TEST_F(NullSerializer_test, get_max_serialized_size_returns_configured_value_for_event) {
    const auto* serializer = get_event_serializer();
    EXPECT_EQ(score_com_serializer_get_max_serialized_size(serializer), kTestMaxMessageSize);
}

TEST_F(NullSerializer_test, get_max_serialized_size_returns_configured_value_for_method_call) {
    const score_com_serializer* serializer = nullptr;
    std::string service(kTestServiceTypeName);
    std::string element(kTestMethodName);
    ASSERT_EQ(score_com_serializer_get(service.c_str(), service.size(),
                                       score_com_serializer_element_type_method_call,
                                       element.c_str(), element.size(), &serializer),
              score_com_serializer_result_ok);
    EXPECT_EQ(score_com_serializer_get_max_serialized_size(serializer), kTestMethodRequestMaxSize);
}

TEST_F(NullSerializer_test, get_max_serialized_size_returns_configured_value_for_method_response) {
    const score_com_serializer* serializer = nullptr;
    std::string service(kTestServiceTypeName);
    std::string element(kTestMethodName);
    ASSERT_EQ(score_com_serializer_get(service.c_str(), service.size(),
                                       score_com_serializer_element_type_method_response,
                                       element.c_str(), element.size(), &serializer),
              score_com_serializer_result_ok);
    EXPECT_EQ(score_com_serializer_get_max_serialized_size(serializer), kTestMethodResponseMaxSize);
}

TEST_F(NullSerializer_test, get_max_serialized_size_returns_zero_for_null) {
    EXPECT_EQ(score_com_serializer_get_max_serialized_size(nullptr), 0U);
}

// --- sizeof_object / alignof_object ---

TEST_F(NullSerializer_test, get_sizeof_object_returns_expected_size) {
    const auto* serializer = get_event_serializer();
    EXPECT_EQ(score_com_serializer_get_sizeof_type(serializer),
              get_size_of_pre_serialized_data(kTestMaxMessageSize));
}

TEST_F(NullSerializer_test, get_sizeof_object_returns_zero_for_null) {
    EXPECT_EQ(score_com_serializer_get_sizeof_type(nullptr), 0U);
}

TEST_F(NullSerializer_test, get_alignof_object_returns_preserialized_alignment) {
    const auto* serializer = get_event_serializer();
    EXPECT_EQ(score_com_serializer_get_alignof_type(serializer), alignof(PreSerializedData<0>));
}

// --- Serialize ---

TEST_F(NullSerializer_test, serialize_copies_data_to_buffer) {
    const auto* serializer = get_event_serializer();

    PreSerializedData<kTestMaxMessageSize> object{};
    const uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
    object.size = sizeof(test_data);
    std::memcpy(object.data, test_data, sizeof(test_data));

    std::vector<uint8_t> buffer(kTestMaxMessageSize);
    std::size_t written_bytes = 0;
    auto result = score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), &object,
                                                 &written_bytes);

    EXPECT_EQ(result, score_com_serializer_result_ok);
    EXPECT_EQ(written_bytes, sizeof(test_data));
    EXPECT_EQ(std::memcmp(buffer.data(), test_data, sizeof(test_data)), 0);
}

TEST_F(NullSerializer_test, serialize_with_null_buffer_fails) {
    const auto* serializer = get_event_serializer();
    PreSerializedData<kTestMaxMessageSize> object{};
    object.size = 4;
    std::size_t written_bytes = 0;
    auto result = score_com_serializer_serialize(serializer, nullptr, 100, &object, &written_bytes);
    EXPECT_EQ(result, score_com_serializer_result_general_failure);
}

TEST_F(NullSerializer_test, serialize_with_null_object_fails) {
    const auto* serializer = get_event_serializer();
    std::vector<uint8_t> buffer(kTestMaxMessageSize);
    std::size_t written_bytes = 0;
    auto result = score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), nullptr,
                                                 &written_bytes);
    EXPECT_EQ(result, score_com_serializer_result_general_failure);
}

TEST_F(NullSerializer_test, serialize_with_insufficient_buffer_fails) {
    const auto* serializer = get_event_serializer();

    PreSerializedData<kTestMaxMessageSize> object{};
    object.size = 100;

    std::vector<uint8_t> buffer(10);  // too small
    std::size_t written_bytes = 0;
    auto result = score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), &object,
                                                 &written_bytes);
    EXPECT_EQ(result, score_com_serializer_result_serialization_failure);
}

TEST_F(NullSerializer_test, serialize_with_null_written_bytes_succeeds) {
    const auto* serializer = get_event_serializer();

    PreSerializedData<kTestMaxMessageSize> object{};
    object.size = 4;
    const uint8_t test_data[] = {0x01, 0x02, 0x03, 0x04};
    std::memcpy(object.data, test_data, sizeof(test_data));

    std::vector<uint8_t> buffer(kTestMaxMessageSize);
    auto result =
        score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), &object, nullptr);
    EXPECT_EQ(result, score_com_serializer_result_ok);
}

// --- Deserialize ---

TEST_F(NullSerializer_test, deserialize_copies_buffer_to_object) {
    const auto* serializer = get_event_serializer();

    const uint8_t test_data[] = {0xAA, 0xBB, 0xCC, 0xDD};
    PreSerializedData<kTestMaxMessageSize> object{};

    auto result =
        score_com_serializer_deserialize(serializer, test_data, sizeof(test_data), &object);

    EXPECT_EQ(result, score_com_serializer_result_ok);
    EXPECT_EQ(object.size, sizeof(test_data));
    EXPECT_EQ(std::memcmp(object.data, test_data, sizeof(test_data)), 0);
}

TEST_F(NullSerializer_test, deserialize_with_oversized_buffer_fails) {
    const auto* serializer = get_event_serializer();

    std::vector<uint8_t> large_buffer(kTestMaxMessageSize + 1);
    PreSerializedData<kTestMaxMessageSize> object{};

    auto result = score_com_serializer_deserialize(serializer, large_buffer.data(),
                                                   large_buffer.size(), &object);
    EXPECT_EQ(result, score_com_serializer_result_deserialization_failure);
}

TEST_F(NullSerializer_test, deserialize_with_null_serializer_fails) {
    const uint8_t test_data[] = {0x01};
    PreSerializedData<kTestMaxMessageSize> object{};
    auto result = score_com_serializer_deserialize(nullptr, test_data, sizeof(test_data), &object);
    EXPECT_EQ(result, score_com_serializer_result_general_failure);
}

TEST_F(NullSerializer_test, deserialize_with_null_buffer_fails) {
    const auto* serializer = get_event_serializer();
    PreSerializedData<kTestMaxMessageSize> object{};
    auto result = score_com_serializer_deserialize(serializer, nullptr, 4, &object);
    EXPECT_EQ(result, score_com_serializer_result_general_failure);
}

TEST_F(NullSerializer_test, deserialize_with_null_object_fails) {
    const auto* serializer = get_event_serializer();
    const uint8_t test_data[] = {0x01};
    auto result =
        score_com_serializer_deserialize(serializer, test_data, sizeof(test_data), nullptr);
    EXPECT_EQ(result, score_com_serializer_result_general_failure);
}

// --- Round-trip ---

TEST_F(NullSerializer_test, serialize_then_deserialize_preserves_data) {
    const auto* serializer = get_event_serializer();

    PreSerializedData<kTestMaxMessageSize> original{};
    const uint8_t payload[] = {0x10, 0x20, 0x30, 0x40, 0x50};
    original.size = sizeof(payload);
    std::memcpy(original.data, payload, sizeof(payload));

    std::vector<uint8_t> buffer(kTestMaxMessageSize);
    std::size_t written_bytes = 0;
    ASSERT_EQ(score_com_serializer_serialize(serializer, buffer.data(), buffer.size(), &original,
                                             &written_bytes),
              score_com_serializer_result_ok);

    PreSerializedData<kTestMaxMessageSize> restored{};
    ASSERT_EQ(score_com_serializer_deserialize(serializer, buffer.data(), written_bytes, &restored),
              score_com_serializer_result_ok);

    EXPECT_EQ(restored.size, original.size);
    EXPECT_EQ(std::memcmp(restored.data, original.data, original.size), 0);
}

}  // namespace
