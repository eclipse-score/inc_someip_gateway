/********************************************************************************
 * Copyright (c) 2025 Contributors to the Eclipse Foundation
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

#ifndef SRC_SERIALIZER_PRE_SERIALIZED_DATA
#define SRC_SERIALIZER_PRE_SERIALIZED_DATA

#include <cstddef>

/// Service for exchanging raw SOME/IP messages.
/// Used between gatewayd and someipd for the payload communication.
namespace score::someip_gateway::serializer {

/// Type definition for pre-serialized data which is used by an application to provide
/// pre-serialized data to gatewayd. The serializer can then be skipped.
template <std::size_t MaxMessageSize>
struct PreSerializedData {
    static constexpr std::size_t kMaxMessageSize = MaxMessageSize;

    std::size_t size{};

    // Align the data to the maximum alignment requirement to ensure that it can hold any type of
    // data without violating alignment requirements. The actual size of the serialized data is
    // given by the `size` member.
    alignas(std::max_align_t) std::byte data[MaxMessageSize];
};

/// Helper function to calculate the size of the PreSerializedData struct at runtime.
constexpr std::size_t get_size_of_pre_serialized_data(std::size_t MaxMessageSize) {
    // Round up MaxMessageSize to the next multiple of alignof(PreSerializedData<0>)
    std::size_t sizeof_data =
        ((MaxMessageSize + alignof(PreSerializedData<0>) - 1) / alignof(PreSerializedData<0>)) *
        alignof(PreSerializedData<0>);
    return sizeof(PreSerializedData<0>) + sizeof_data;
}

}  // namespace score::someip_gateway::serializer

#endif  // SRC_SERIALIZER_PRE_SERIALIZED_DATA
