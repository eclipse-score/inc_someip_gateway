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
#include <limits>
#include <utility>

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"

namespace score::gateway_ipc_binding::mw_com {
namespace {

static_assert(sizeof(std::size_t) == sizeof(std::uint64_t));
static_assert(noexcept(sample_size(std::declval<Event_config const&>())));
static_assert(noexcept(addressed_sample_size(std::declval<Event_config const&>())));
static_assert(noexcept(sample_meta_info(std::declval<Event_config const&>())));
static_assert(noexcept(write_payload_length(std::declval<std::byte*>(), std::size_t{0U})));
static_assert(noexcept(read_payload_length(std::declval<std::byte const*>())));

constexpr Mw_com_event_id kTestEventId{1U};
constexpr std::size_t kExpectedAlignment = 8U;
constexpr std::size_t kSentinelBufferSize = 16U;
constexpr std::size_t kAlignmentCaseCount = 7U;
constexpr std::size_t kOverflowCaseCount = 7U;

constexpr std::byte kSentinelAa{0xAAU};
constexpr std::byte kSentinel55{0x55U};
constexpr std::byte kZeroReservedByte{0x00U};
constexpr std::byte kMaxByte{0xFFU};

constexpr std::size_t kZeroPayloadLength = 0U;
constexpr std::size_t kSignBitPayloadLength = 0x807F0100U;
constexpr std::size_t kUint32MaxPayloadLength = 0xFFFFFFFFULL;

struct SizingCase {
    std::size_t max_payload_size;
    std::size_t expected_addressed_size;
    std::size_t expected_sample_size;
};

TEST(SampleLayoutTest, AlignmentAndAddressedSizeBoundaries) {
    constexpr std::array<SizingCase, kAlignmentCaseCount> kCases{{
        {0U, 8U, 8U},
        {1U, 9U, 16U},
        {7U, 15U, 16U},
        {8U, 16U, 16U},
        {9U, 17U, 24U},
        {15U, 23U, 24U},
        {16U, 24U, 24U},
    }};

    ASSERT_EQ(kCases.size(), kAlignmentCaseCount);
    for (SizingCase const& tc : kCases) {
        SCOPED_TRACE(tc.max_payload_size);
        Event_config const event{"event", kTestEventId, tc.max_payload_size};

        EXPECT_EQ(addressed_sample_size(event), tc.expected_addressed_size);
        EXPECT_EQ(sample_size(event), tc.expected_sample_size);

        auto const meta = sample_meta_info(event);
        EXPECT_EQ(meta.size, tc.expected_sample_size);
        EXPECT_EQ(meta.alignment, kExpectedAlignment);
    }
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST(SampleLayoutTest, DISABLED_OverflowBoundariesSaturateToSizeMax) {
    constexpr std::array<SizingCase, kOverflowCaseCount> kOverflowCases{{
        // kMax_payload_length (UINT32_MAX) fits comfortably in 64-bit size_t without saturation.
        {kMax_payload_length, 0x100000007ULL, 0x100000008ULL},
        // Largest max_payload_size whose aligned sample_size fits in 64-bit size_t (SIZE_MAX - 15).
        {0xFFFFFFFFFFFFFFF0ULL, 0xFFFFFFFFFFFFFFF8ULL, 0xFFFFFFFFFFFFFFF8ULL},
        // First max_payload_size where rounding up overflows 64-bit size_t (SIZE_MAX - 14).
        {0xFFFFFFFFFFFFFFF1ULL, 0xFFFFFFFFFFFFFFF9ULL, 0xFFFFFFFFFFFFFFFFULL},
        // One below exact SIZE_MAX sum for addressed_sample_size (SIZE_MAX - 9).
        {0xFFFFFFFFFFFFFFF6ULL, 0xFFFFFFFFFFFFFFFEULL, 0xFFFFFFFFFFFFFFFFULL},
        // Exact SIZE_MAX sum for addressed_sample_size (SIZE_MAX - 8).
        {0xFFFFFFFFFFFFFFF7ULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
        // First max_payload_size where addressed_sample_size addition overflows (SIZE_MAX - 7).
        {0xFFFFFFFFFFFFFFF8ULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
        // Maximum size_t input (SIZE_MAX).
        {std::numeric_limits<std::size_t>::max(), 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL},
    }};

    ASSERT_EQ(kOverflowCases.size(), kOverflowCaseCount);
    for (SizingCase const& tc : kOverflowCases) {
        SCOPED_TRACE(tc.max_payload_size);
        Event_config const event{"event", kTestEventId, tc.max_payload_size};

        EXPECT_EQ(addressed_sample_size(event), tc.expected_addressed_size);
        EXPECT_EQ(sample_size(event), tc.expected_sample_size);

        auto const meta = sample_meta_info(event);
        EXPECT_EQ(meta.size, tc.expected_sample_size);
        EXPECT_EQ(meta.alignment, kExpectedAlignment);
    }
}

TEST(SampleLayoutTest, PrefixFramingWritesHostLengthZeroesReservedAndPreservesPayload) {
    alignas(kExpectedAlignment) std::array<std::byte, kSentinelBufferSize> buffer{};

    // Sign-bit multi-byte length (0x807F0100) over 0xAA-filled recycled slot buffer.
    buffer.fill(kSentinelAa);
    write_payload_length(buffer.data(), kSignBitPayloadLength);
    EXPECT_EQ(read_payload_length(buffer.data()), kSignBitPayloadLength);

    constexpr std::array<std::byte, kSentinelBufferSize> kExpectedSignBitBuffer{
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        std::byte{0x80U},  std::byte{0x7FU},  std::byte{0x01U},  std::byte{0x00U},
#else
        std::byte{0x00U},  std::byte{0x01U},  std::byte{0x7FU},  std::byte{0x80U},
#endif
        kZeroReservedByte, kZeroReservedByte, kZeroReservedByte, kZeroReservedByte,
        kSentinelAa,       kSentinelAa,       kSentinelAa,       kSentinelAa,
        kSentinelAa,       kSentinelAa,       kSentinelAa,       kSentinelAa,
    };
    EXPECT_EQ(buffer, kExpectedSignBitBuffer);

    // Non-zero bytes in reserved [4..7] must be ignored by read_payload_length.
    buffer[4] = kMaxByte;
    buffer[5] = std::byte{0x80U};
    buffer[6] = std::byte{0x7FU};
    buffer[7] = std::byte{0x01U};
    EXPECT_EQ(read_payload_length(buffer.data()), kSignBitPayloadLength);

    // Lower boundary length = 0 over 0xAA-filled buffer.
    buffer.fill(kSentinelAa);
    write_payload_length(buffer.data(), kZeroPayloadLength);
    EXPECT_EQ(read_payload_length(buffer.data()), kZeroPayloadLength);

    constexpr std::array<std::byte, kSentinelBufferSize> kExpectedZeroLengthBuffer{
        kZeroReservedByte, kZeroReservedByte, kZeroReservedByte, kZeroReservedByte,
        kZeroReservedByte, kZeroReservedByte, kZeroReservedByte, kZeroReservedByte,
        kSentinelAa,       kSentinelAa,       kSentinelAa,       kSentinelAa,
        kSentinelAa,       kSentinelAa,       kSentinelAa,       kSentinelAa,
    };
    EXPECT_EQ(buffer, kExpectedZeroLengthBuffer);

    // Upper boundary length = kMax_payload_length (0xFFFFFFFF) over 0x55-filled buffer.
    buffer.fill(kSentinel55);
    write_payload_length(buffer.data(), kMax_payload_length);
    EXPECT_EQ(read_payload_length(buffer.data()), kUint32MaxPayloadLength);

    constexpr std::array<std::byte, kSentinelBufferSize> kExpectedMaxLengthBuffer{
        kMaxByte,          kMaxByte,          kMaxByte,          kMaxByte,
        kZeroReservedByte, kZeroReservedByte, kZeroReservedByte, kZeroReservedByte,
        kSentinel55,       kSentinel55,       kSentinel55,       kSentinel55,
        kSentinel55,       kSentinel55,       kSentinel55,       kSentinel55,
    };
    EXPECT_EQ(buffer, kExpectedMaxLengthBuffer);
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
