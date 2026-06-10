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

/// @file
/// This file provides a "serializer" which actually doesn't serialize and just copies the memory.

#include <csignal>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>

#include "src/config/mw_someip_config_generated.h"
#include "src/serializer/pre_serialized_data.h"
#include "src/serializer/serializer.h"

using score::someip_gateway::serializer::get_size_of_pre_serialized_data;
using score::someip_gateway::serializer::PreSerializedData;

// We don't know the actual size of the PreSerializedData struct at compile time because it depends
// on the max_message_size specified in the config, so we use a view with zero-sized array and
// calculate the size at runtime.
using PreSerializedDataView = PreSerializedData<0>;

// score_com_serializer is an opaque handle that directly points to
// score::mw_someip_config::NullSerializerConfig in the flatbuffer config.
struct score_com_serializer {};

namespace {

/// Convert from opaque score_com_serializer pointer to NullSerializerConfig
const score::mw_someip_config::NullSerializerConfig* to_null_config(
    const struct score_com_serializer* serializer) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<const score::mw_someip_config::NullSerializerConfig*>(serializer);
}

std::shared_ptr<const score::mw_someip_config::Root>& get_config() {
    static std::shared_ptr<const score::mw_someip_config::Root> config;
    return config;
}

};  // anonymous namespace

score_com_serializer_result score_com_serializer_serialize(const struct score_com_serializer*,
                                                           uint8_t* buffer, size_t buffer_size,
                                                           const void* object,
                                                           size_t* written_bytes) {
    if (buffer == nullptr || object == nullptr) {
        return score_com_serializer_result_general_failure;
    }
    const auto* pre_serialized_data = static_cast<const PreSerializedDataView*>(object);
    std::size_t message_size = pre_serialized_data->size;
    if (message_size > buffer_size) {
        return score_com_serializer_result_serialization_failure;
    }
    std::memcpy(buffer, pre_serialized_data->data, message_size);
    if (written_bytes != nullptr) {
        *written_bytes = message_size;
    }
    return score_com_serializer_result_ok;
}

score_com_serializer_result score_com_serializer_deserialize(
    const struct score_com_serializer* serializer, const uint8_t* buffer, size_t buffer_size,
    void* object) {
    if (serializer == nullptr || buffer == nullptr || object == nullptr) {
        return score_com_serializer_result_general_failure;
    }
    auto* pre_serialized_data = static_cast<PreSerializedDataView*>(object);
    if (buffer_size > to_null_config(serializer)->max_message_size()) {
        return score_com_serializer_result_deserialization_failure;
    }
    std::memcpy(pre_serialized_data->data, buffer, buffer_size);
    pre_serialized_data->size = buffer_size;
    return score_com_serializer_result_ok;
}

std::size_t score_com_serializer_get_max_serialized_size(
    const struct score_com_serializer* serializer) {
    if (serializer == nullptr) {
        return 0;
    }
    return to_null_config(serializer)->max_message_size();
}

std::size_t score_com_serializer_get_sizeof_type(const struct score_com_serializer* serializer) {
    if (serializer == nullptr) {
        return 0;
    }
    return get_size_of_pre_serialized_data(to_null_config(serializer)->max_message_size());
}

std::size_t score_com_serializer_get_alignof_type(const struct score_com_serializer*) {
    return alignof(PreSerializedDataView);
}

score_com_serializer_result score_com_serializer_init(const char* serializer_identifier,
                                                      size_t serializer_identifier_size) {
    std::string_view serializer_id(serializer_identifier, serializer_identifier_size);

    // Read config data
    // TODO: Use memory mapped file instead of copying into buffer
    std::ifstream config_file;
    config_file.open(std::string(serializer_id), std::ios::binary | std::ios::in);

    if (!config_file.is_open()) {
        std::cerr << "Error: Could not open config file " << serializer_id << std::endl;
        return score_com_serializer_result_serializer_nonexistent;
    }

    config_file.seekg(0, std::ios::end);
    std::streampos length = config_file.tellg();

    if (length <= 0) {
        std::cerr << "Error: Invalid config file size: " << length << std::endl;
        config_file.close();
        return score_com_serializer_result_serializer_nonexistent;
    }

    config_file.seekg(0, std::ios::beg);
    auto config_buffer = std::shared_ptr<char>(new char[length]);
    config_file.read(config_buffer.get(), length);
    config_file.close();

    get_config() = std::shared_ptr<const score::mw_someip_config::Root>(
        config_buffer, score::mw_someip_config::GetRoot(config_buffer.get()));

    return score_com_serializer_result_ok;
}

score_com_serializer_result score_com_serializer_deinit() {
    get_config().reset();
    return score_com_serializer_result_ok;
}

namespace {

const score::mw_someip_config::NullSerializerConfig* lookup_serialization_config(
    std::string_view service_type_name, score_com_serializer_element_type element_type,
    std::string_view element_name) {
    const auto config = get_config();
    if (config == nullptr || config->service_types() == nullptr) {
        return nullptr;
    }

    for (const auto* service_type : *config->service_types()) {
        if (service_type->service_type_name() == nullptr ||
            service_type->service_type_name()->string_view() != service_type_name) {
            continue;
        }

        if (element_type == score_com_serializer_element_type_event) {
            if (service_type->events() == nullptr) {
                return nullptr;
            }
            for (const auto* event : *service_type->events()) {
                if (event->event_name() != nullptr &&
                    event->event_name()->string_view() == element_name) {
                    return event->serialization_config_as_NullSerializerConfig();
                }
            }
        } else if (element_type == score_com_serializer_element_type_method_call) {
            if (service_type->methods() == nullptr) {
                return nullptr;
            }
            for (const auto* method : *service_type->methods()) {
                if (method->method_name() != nullptr &&
                    method->method_name()->string_view() == element_name) {
                    return method->request_serialization_config_as_NullSerializerConfig();
                }
            }
        } else if (element_type == score_com_serializer_element_type_method_response) {
            if (service_type->methods() == nullptr) {
                return nullptr;
            }
            for (const auto* method : *service_type->methods()) {
                if (method->method_name() != nullptr &&
                    method->method_name()->string_view() == element_name) {
                    return method->response_serialization_config_as_NullSerializerConfig();
                }
            }
        }

        return nullptr;
    }

    return nullptr;
}

}  // namespace

score_com_serializer_result score_com_serializer_get(
    const char* service_type, size_t service_type_size,
    enum score_com_serializer_element_type element_type, const char* element_name,
    size_t element_name_size, const struct score_com_serializer** serializer) {
    if (serializer == nullptr) {
        return score_com_serializer_result_general_failure;
    }

    std::string_view service_type_name(service_type, service_type_size);
    std::string_view element_name_view(element_name, element_name_size);

    const auto* serializer_config =
        lookup_serialization_config(service_type_name, element_type, element_name_view);
    if (serializer_config == nullptr) {
        std::cerr << "Error: No serialization config found for service_type=" << service_type_name
                  << " element=" << element_name_view << std::endl;
        return score_com_serializer_result_serializer_nonexistent;
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    *serializer = reinterpret_cast<const struct score_com_serializer*>(serializer_config);

    return score_com_serializer_result_ok;
}
