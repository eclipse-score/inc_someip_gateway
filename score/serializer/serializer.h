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
#ifndef SRC_SERIALIZER_SERIALIZER
#define SRC_SERIALIZER_SERIALIZER

/// @file
/// Interface to the serializer "plugin".

// This extern "C" block ensures that the function names are not mangled by the C++ compiler,
// making the library ABI-compatible.
#if defined(__cplusplus)
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

/// Result type for the serializer functions
enum [[nodiscard]] score_com_serializer_result {
    score_com_serializer_result_ok = 0,
    score_com_serializer_result_general_failure = 1,
    score_com_serializer_result_serializer_nonexistent = 2,
    score_com_serializer_result_serialization_failure = 3,
    score_com_serializer_result_deserialization_failure = 4
};

/// Serializer is an opaque struct that represents a serializer instance. The actual definition is
/// hidden in the implementation to allow for flexibility in how serializers are implemented. Users
/// of the serializer plugin will interact with pointers to this struct, but they won't know its
/// internal structure.
struct score_com_serializer;

/// Serializes the given object into the provided buffer.
/// @param serializer Pointer to the serializer
/// @param[out] buffer Pointer to the buffer where the serialized data will be stored.
/// @param buffer_size Size of the provided buffer in bytes.
/// @param object Pointer to the object that needs to be serialized.
/// @param[out] written_bytes Pointer to a variable where the function will write the number of
/// bytes actually written to the buffer.
score_com_serializer_result score_com_serializer_serialize(
    const struct score_com_serializer* serializer, uint8_t* buffer, size_t buffer_size,
    const void* object, size_t* written_bytes);

/// Deserializes data from the given buffer into the provided object.
/// @param serializer Pointer to the serializer
/// @param buffer Pointer to the buffer containing the serialized data.
/// @param buffer_size Size of the provided buffer in bytes.
/// @param[out] object Pointer to the object where the deserialized data will be stored.
score_com_serializer_result score_com_serializer_deserialize(
    const struct score_com_serializer* serializer, const uint8_t* buffer, size_t buffer_size,
    void* object);

/// Retrieves the maximum serialized size from the serializer.
/// @param serializer Pointer to the serializer
/// @retval 0 in case of error (e.g. nullptr passed)
size_t score_com_serializer_get_max_serialized_size(const struct score_com_serializer* serializer);

/// Retrieves the sizeof() of the application data type that the serializer handles.
/// @param serializer Pointer to the serializer
/// @retval 0 if not specified by the serializer. Deserialization might not work.
size_t score_com_serializer_get_sizeof_type(const struct score_com_serializer* serializer);

/// Retrieves the alignof() of the application data type that the serializer handles.
/// @param serializer Pointer to the serializer
/// @retval 0 if not specified by the serializer. Deserialization might not work.
size_t score_com_serializer_get_alignof_type(const struct score_com_serializer* serializer);

enum score_com_serializer_element_type {
    score_com_serializer_element_type_event = 0,
    score_com_serializer_element_type_field = 1,
    score_com_serializer_element_type_method_call = 2,
    score_com_serializer_element_type_method_response = 3
};

/// Retrieves a serializer for the specified interface and component.
/// @param service_type Identifies the interface for which the serializer is requested (not
/// null-terminated).
/// @param service_type_size Size of the `service_type` string in bytes.
/// @param element_type Specifies the type of element for which the serializer is requested.
/// @param element_name Identifies the event/field/method for which the serializer is requested (not
/// null-terminated).
/// @param element_name_size Size of the `element_name` string in bytes.
/// @param[out] serializer Pointer to a score_com_serializer struct where the function will write
/// the serializer's methods and properties if found.
/// @retval score_com_serializer_result_ok if the serializer is successfully retrieved and the
/// 'serializer' struct is populated.
/// @retval score_com_serializer_result_serializer_nonexistent if no matching serializer is found.
/// @retval score_com_serializer_result_general_failure if there is a general failure during the
/// retrieval process.
score_com_serializer_result score_com_serializer_get(
    const char* service_type, size_t service_type_size,
    enum score_com_serializer_element_type element_type, const char* element_name,
    size_t element_name_size, const struct score_com_serializer** serializer);

/// Initializes the serializer plugin. This function must be called once before any calls to
/// score_com_serializer_get(). Not thread-safe.
/// @param serializer_identifier Pointer to a string that identifies the serializer plugin (not
/// null-terminated).
/// @param serializer_identifier_size Size of the serializer identifier string in bytes.
/// @retval score_com_serializer_result_ok if initialization is successful.
/// @retval score_com_serializer_result_serializer_nonexistent if the serializer plugin can't be
/// loaded for the identifier.
/// @retval score_com_serializer_result_general_failure if there is a general failure during
/// initialization.
score_com_serializer_result score_com_serializer_init(const char* serializer_identifier,
                                                      size_t serializer_identifier_size);

/// Cleans up resources allocated by score_com_serializer_init.
score_com_serializer_result score_com_serializer_deinit();

#if defined(__cplusplus)
}  // end extern "C"
#endif

#endif  // SRC_SERIALIZER_SERIALIZER
