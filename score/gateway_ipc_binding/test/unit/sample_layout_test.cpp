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

#include "score/gateway_ipc_binding/impl/sample_layout.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"

namespace score::gateway_ipc_binding::mw_com {
namespace {

constexpr Mw_com_event_id kZeroEventId{1U};
constexpr Mw_com_event_id kUnalignedEventId{2U};
constexpr std::size_t kZeroPayloadSize{0U};
constexpr std::size_t kUnalignedPayloadSize{10U};
constexpr std::size_t kExpectedUnalignedAddressedSize{kSample_prefix_size + kUnalignedPayloadSize};
constexpr std::size_t kExpectedAlignedSampleBlocks{3U};
constexpr std::size_t kExpectedAlignedSampleSize{kExpectedAlignedSampleBlocks * kSample_alignment};
constexpr std::size_t kRepresentativePayloadLength{12U};
constexpr std::size_t kPayloadLengthFieldSize{sizeof(std::uint32_t)};
constexpr std::byte kNonZeroFillByte{0xFFU};
constexpr std::byte kExpectedReservedByte{0x00U};

TEST(SampleLayoutTest, ZeroPayloadSampleSizeEqualsPrefixSize) {
    Event_config const zero_event{"zero", kZeroEventId, kZeroPayloadSize};
    EXPECT_EQ(sample_size(zero_event), kSample_prefix_size);
    EXPECT_EQ(addressed_sample_size(zero_event), kSample_prefix_size);
}

TEST(SampleLayoutTest, UnalignedPayloadSampleSizeRoundsUpToAlignment) {
    Event_config const unaligned_event{"unaligned", kUnalignedEventId, kUnalignedPayloadSize};
    EXPECT_EQ(addressed_sample_size(unaligned_event), kExpectedUnalignedAddressedSize);
    EXPECT_EQ(sample_size(unaligned_event), kExpectedAlignedSampleSize);
}

TEST(SampleLayoutTest, SampleMetaInfoUsesAlignedSampleSizeAndAlignment) {
    Event_config const unaligned_event{"unaligned", kUnalignedEventId, kUnalignedPayloadSize};
    auto const meta = sample_meta_info(unaligned_event);
    EXPECT_EQ(meta.size, kExpectedAlignedSampleSize);
    EXPECT_EQ(meta.alignment, kSample_alignment);
}

TEST(SampleLayoutTest, PayloadLengthRoundTripsAndZeroesAllReservedPrefixBytes) {
    alignas(kSample_alignment) std::array<std::byte, kSample_prefix_size> buffer{};
    constexpr std::array kPayloadLengths{
        kZeroPayloadSize,
        kRepresentativePayloadLength,
        kMax_payload_length,
    };

    for (std::size_t const payload_length : kPayloadLengths) {
        buffer.fill(kNonZeroFillByte);
        write_payload_length(buffer.data(), payload_length);
        EXPECT_EQ(read_payload_length(buffer.data()), payload_length);
        for (std::size_t i = kPayloadLengthFieldSize; i < kSample_prefix_size; ++i) {
            EXPECT_EQ(buffer[i], kExpectedReservedByte);
        }
    }
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
