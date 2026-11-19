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

#include "score/gateway_ipc_binding/fixed_size_container.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <new>
#include <type_traits>
#include <utility>

namespace score::gateway_ipc_binding {
namespace {

constexpr std::size_t kZeroCapacity = 0U;
constexpr std::size_t kSmallCapacity = 4U;
constexpr std::size_t kByteCapacity = 8U;
constexpr std::size_t kSingleCount = 1U;
constexpr std::size_t kPairCount = 2U;
constexpr std::size_t kAlmostFullCount = 3U;
constexpr std::size_t kCorruptedOverreadOffset = 5U;
constexpr std::size_t kGuardArenaSlots = 3U;

constexpr std::byte kDirtyStorageByte{0xAAU};
constexpr std::uint8_t kZeroByte = 0x00U;
constexpr std::uint8_t kLowByte = 0x10U;
constexpr std::uint8_t kMidByte = 0x22U;
constexpr std::uint8_t kMaxSignedPositiveByte = 0x7FU;
constexpr std::uint8_t kMinHighBitByte = 0x80U;
constexpr std::uint8_t kAlmostMaxByte = 0xFEU;
constexpr std::uint8_t kMaxUnsignedByte = 0xFFU;
constexpr std::uint8_t kTrailingGarbageA = 0xAAU;
constexpr std::uint8_t kTrailingGarbageB = 0x55U;

constexpr std::uint32_t kZeroWord = 0x00000000U;
constexpr std::uint32_t kWord0 = 0x01020304U;
constexpr std::uint32_t kWord1 = 0x7FFFFFFFU;
constexpr std::uint32_t kWord2 = 0x80000000U;
constexpr std::uint32_t kWord3 = 0xFFFFFFFFU;

struct TriviallyCopyableRecord {
    std::uint16_t service_id;
    std::uint32_t max_payload_size;
};

struct TrackedElement {
    std::uint32_t value;
};

int g_tracked_element_comparisons = 0;

bool operator==(TrackedElement const lhs, TrackedElement const rhs) noexcept {
    ++g_tracked_element_comparisons;
    return lhs.value == rhs.value;
}

[[maybe_unused]] bool operator!=(TrackedElement const lhs, TrackedElement const rhs) noexcept {
    return !(lhs == rhs);
}

using ByteContainer = Fixed_size_container<std::uint8_t, kSmallCapacity>;
using WordContainer = Fixed_size_container<std::uint32_t, kSmallCapacity>;
using ZeroCapContainer = Fixed_size_container<std::uint8_t, kZeroCapacity>;
using TrackedContainer = Fixed_size_container<TrackedElement, kSmallCapacity>;

static_assert(std::is_trivially_copyable_v<Fixed_size_container<std::byte, kByteCapacity>>);
static_assert(std::is_trivially_copyable_v<WordContainer>);
static_assert(std::is_trivially_copyable_v<ZeroCapContainer>);
static_assert(
    std::is_trivially_copyable_v<Fixed_size_container<TriviallyCopyableRecord, kSmallCapacity>>);
static_assert(std::is_trivially_copyable_v<TrackedContainer>);

static_assert(WordContainer::max_size == kSmallCapacity);
static_assert(sizeof(WordContainer::data) == kSmallCapacity * sizeof(std::uint32_t));
static_assert(ZeroCapContainer::max_size == kZeroCapacity);

static_assert(std::is_constructible_v<WordContainer, std::uint32_t const (&)[kSingleCount]>);
static_assert(std::is_constructible_v<WordContainer, std::uint32_t const (&)[kSmallCapacity]>);
static_assert(
    !std::is_constructible_v<WordContainer, std::uint32_t const (&)[kSmallCapacity + 1U]>);

template <typename C, typename = void>
struct HasInequalityOperator : std::false_type {};

template <typename C>
struct HasInequalityOperator<
    C, std::void_t<decltype(std::declval<C const&>() != std::declval<C const&>())>>
    : std::true_type {};

template <typename C>
void ExpectNeWhenAvailable(C const& lhs, C const& rhs, bool const expected) {
    if constexpr (HasInequalityOperator<C>::value) {
        EXPECT_EQ(lhs != rhs, expected);
    }
}

template <typename C>
void ExpectEqNoexceptWhenFixed(C const& lhs, C const& rhs) {
    EXPECT_TRUE(noexcept(lhs == rhs));
    if constexpr (HasInequalityOperator<C>::value) {
        EXPECT_TRUE(noexcept(lhs != rhs));
    }
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST(FixedSizeContainerTest, DISABLED_DefaultAndValueInitializationZeroSizeAndStorage) {
    ByteContainer unbraced;
    EXPECT_EQ(unbraced.size, kZeroCapacity);
    EXPECT_TRUE(unbraced.empty());
    EXPECT_TRUE(noexcept(unbraced.empty()));

    ByteContainer const braced{};
    EXPECT_EQ(braced.size, kZeroCapacity);
    EXPECT_TRUE(braced.empty());
    ASSERT_EQ(braced.data.size(), kSmallCapacity);
    for (std::size_t i = 0U; i < braced.data.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(braced.data[i], kZeroByte);
    }

    alignas(ByteContainer) std::array<std::byte, sizeof(ByteContainer)> dirty_storage{};
    dirty_storage.fill(kDirtyStorageByte);
    auto* const dirty_default = ::new (static_cast<void*>(dirty_storage.data())) ByteContainer;
    EXPECT_EQ(dirty_default->size, kZeroCapacity);
    EXPECT_TRUE(dirty_default->empty());
    ASSERT_EQ(dirty_default->data.size(), kSmallCapacity);
    for (std::size_t i = 0U; i < dirty_default->data.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(dirty_default->data[i], kZeroByte);
    }
    dirty_default->~ByteContainer();

    ZeroCapContainer const zero_cap{};
    EXPECT_EQ(zero_cap.size, kZeroCapacity);
    EXPECT_TRUE(zero_cap.empty());
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST(FixedSizeContainerTest, DISABLED_ArrayConstructionCopiesElementsAndZeroesTrailingSlots) {
    alignas(WordContainer) std::array<std::byte, sizeof(WordContainer)> dirty_storage{};

    dirty_storage.fill(kDirtyStorageByte);
    std::uint32_t const one_elem[kSingleCount] = {kWord2};
    auto* const single = ::new (static_cast<void*>(dirty_storage.data())) WordContainer(one_elem);
    EXPECT_EQ(single->size, kSingleCount);
    EXPECT_FALSE(single->empty());
    EXPECT_EQ(single->data[0], kWord2);
    EXPECT_EQ(single->data[1], kZeroWord);
    EXPECT_EQ(single->data[2], kZeroWord);
    EXPECT_EQ(single->data[3], kZeroWord);
    single->~WordContainer();

    dirty_storage.fill(kDirtyStorageByte);
    std::uint32_t const three_elems[kAlmostFullCount] = {kWord0, kWord1, kWord2};
    auto* const almost_full =
        ::new (static_cast<void*>(dirty_storage.data())) WordContainer(three_elems);
    EXPECT_EQ(almost_full->size, kAlmostFullCount);
    EXPECT_FALSE(almost_full->empty());
    EXPECT_EQ(almost_full->data[0], kWord0);
    EXPECT_EQ(almost_full->data[1], kWord1);
    EXPECT_EQ(almost_full->data[2], kWord2);
    EXPECT_EQ(almost_full->data[3], kZeroWord);
    almost_full->~WordContainer();

    dirty_storage.fill(kDirtyStorageByte);
    std::uint32_t const four_elems[kSmallCapacity] = {kWord0, kWord1, kWord2, kWord3};
    auto* const full = ::new (static_cast<void*>(dirty_storage.data())) WordContainer(four_elems);
    EXPECT_EQ(full->size, kSmallCapacity);
    EXPECT_FALSE(full->empty());
    EXPECT_EQ(full->data[0], kWord0);
    EXPECT_EQ(full->data[1], kWord1);
    EXPECT_EQ(full->data[2], kWord2);
    EXPECT_EQ(full->data[3], kWord3);
    full->~WordContainer();
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST(FixedSizeContainerTest, DISABLED_InitializerListConstructionAcrossCapacitySeams) {
    alignas(WordContainer) std::array<std::byte, sizeof(WordContainer)> dirty_storage{};

    dirty_storage.fill(kDirtyStorageByte);
    std::initializer_list<std::uint32_t> const empty_init{};
    auto* const empty_list =
        ::new (static_cast<void*>(dirty_storage.data())) WordContainer(empty_init);
    EXPECT_EQ(empty_list->size, kZeroCapacity);
    EXPECT_TRUE(empty_list->empty());
    ASSERT_EQ(empty_list->data.size(), kSmallCapacity);
    for (std::size_t i = 0U; i < empty_list->data.size(); ++i) {
        SCOPED_TRACE(i);
        EXPECT_EQ(empty_list->data[i], kZeroWord);
    }
    empty_list->~WordContainer();

    dirty_storage.fill(kDirtyStorageByte);
    auto* const one_list = ::new (static_cast<void*>(dirty_storage.data())) WordContainer{kWord1};
    EXPECT_EQ(one_list->size, kSingleCount);
    EXPECT_FALSE(one_list->empty());
    EXPECT_EQ(one_list->data[0], kWord1);
    EXPECT_EQ(one_list->data[1], kZeroWord);
    EXPECT_EQ(one_list->data[2], kZeroWord);
    EXPECT_EQ(one_list->data[3], kZeroWord);
    one_list->~WordContainer();

    dirty_storage.fill(kDirtyStorageByte);
    auto* const three_list =
        ::new (static_cast<void*>(dirty_storage.data())) WordContainer{kWord0, kWord1, kWord2};
    EXPECT_EQ(three_list->size, kAlmostFullCount);
    EXPECT_FALSE(three_list->empty());
    EXPECT_EQ(three_list->data[0], kWord0);
    EXPECT_EQ(three_list->data[1], kWord1);
    EXPECT_EQ(three_list->data[2], kWord2);
    EXPECT_EQ(three_list->data[3], kZeroWord);
    three_list->~WordContainer();

    WordContainer const full_list{kZeroWord, kWord1, kWord2, kWord3};
    EXPECT_EQ(full_list.size, kSmallCapacity);
    EXPECT_FALSE(full_list.empty());
    EXPECT_EQ(full_list.data[0], kZeroWord);
    EXPECT_EQ(full_list.data[1], kWord1);
    EXPECT_EQ(full_list.data[2], kWord2);
    EXPECT_EQ(full_list.data[3], kWord3);
}

// Disabled: enable once https://github.com/eclipse-score/inc_someip_gateway/issues/451 is fixed.
TEST(FixedSizeContainerTest,
     DISABLED_EqualityAndInequalityRespectActivePrefixSignBitAndCorruptedSize) {
    EXPECT_TRUE((HasInequalityOperator<ByteContainer>::value));

    ByteContainer lhs{kLowByte, kMaxSignedPositiveByte};
    ByteContainer rhs{kLowByte, kMaxSignedPositiveByte};
    ExpectEqNoexceptWhenFixed(lhs, rhs);

    // Mutate inactive trailing bytes [size..Max_size) to verify only active prefix participates.
    lhs.data[2] = kTrailingGarbageA;
    lhs.data[3] = kMaxUnsignedByte;
    rhs.data[2] = kZeroByte;
    rhs.data[3] = kTrailingGarbageB;
    EXPECT_TRUE(lhs == rhs);
    ExpectNeWhenAvailable(lhs, rhs, false);

    // Empty containers with different backing bytes compare equal.
    ByteContainer empty_lhs{};
    ByteContainer empty_rhs{};
    empty_lhs.data = {kTrailingGarbageA, kMaxUnsignedByte, kMinHighBitByte, kLowByte};
    empty_rhs.data = {kZeroByte, kTrailingGarbageB, kMaxSignedPositiveByte, kMidByte};
    EXPECT_TRUE(empty_lhs == empty_rhs);
    ExpectNeWhenAvailable(empty_lhs, empty_rhs, false);

    // Different active lengths with identical backing bytes compare unequal.
    ByteContainer short_prefix{kLowByte, kMidByte, kMaxSignedPositiveByte};
    ByteContainer long_prefix{kLowByte, kMidByte, kMaxSignedPositiveByte};
    short_prefix.size = kPairCount;
    EXPECT_FALSE(short_prefix == long_prefix);
    ExpectNeWhenAvailable(short_prefix, long_prefix, true);

    // Sign-bit seam (0x7F vs 0x80) at index 0, index size - 1, and index Max_size - 1.
    ByteContainer const first_pos{kMaxSignedPositiveByte, kLowByte, kMidByte};
    ByteContainer const first_neg{kMinHighBitByte, kLowByte, kMidByte};
    EXPECT_FALSE(first_pos == first_neg);
    ExpectNeWhenAvailable(first_pos, first_neg, true);

    ByteContainer const tail_pos{kZeroByte, kMaxUnsignedByte, kMaxSignedPositiveByte};
    ByteContainer const tail_neg{kZeroByte, kMaxUnsignedByte, kMinHighBitByte};
    EXPECT_FALSE(tail_pos == tail_neg);
    ExpectNeWhenAvailable(tail_pos, tail_neg, true);

    ByteContainer const full_a{kZeroByte, kMaxSignedPositiveByte, kMinHighBitByte, kAlmostMaxByte};
    ByteContainer const full_b{kZeroByte, kMaxSignedPositiveByte, kMinHighBitByte,
                               kMaxUnsignedByte};
    EXPECT_FALSE(full_a == full_b);
    ExpectNeWhenAvailable(full_a, full_b, true);

    // Corrupted shared-memory size > Max_size must return false without reading past Max_size - 1.
    std::array<ByteContainer, kGuardArenaSlots> corrupted_lhs_arena{};
    std::array<ByteContainer, kGuardArenaSlots> corrupted_rhs_arena{};

    corrupted_lhs_arena[0].size = kSmallCapacity + 1U;
    corrupted_rhs_arena[0].size = kSmallCapacity;
    EXPECT_FALSE(corrupted_lhs_arena[0] == corrupted_rhs_arena[0]);
    ExpectNeWhenAvailable(corrupted_lhs_arena[0], corrupted_rhs_arena[0], true);
    EXPECT_FALSE(corrupted_rhs_arena[0] == corrupted_lhs_arena[0]);
    ExpectNeWhenAvailable(corrupted_rhs_arena[0], corrupted_lhs_arena[0], true);

    corrupted_lhs_arena[0].size = kSmallCapacity + 1U;
    corrupted_rhs_arena[0].size = kSmallCapacity + 1U;
    EXPECT_FALSE(corrupted_lhs_arena[0] == corrupted_rhs_arena[0]);
    ExpectNeWhenAvailable(corrupted_lhs_arena[0], corrupted_rhs_arena[0], true);

    corrupted_lhs_arena[0].size = kSmallCapacity + kCorruptedOverreadOffset;
    corrupted_rhs_arena[0].size = kSmallCapacity + kCorruptedOverreadOffset;
    EXPECT_FALSE(corrupted_lhs_arena[0] == corrupted_rhs_arena[0]);
    ExpectNeWhenAvailable(corrupted_lhs_arena[0], corrupted_rhs_arena[0], true);

    // Verify active-prefix comparison touches only [0..size) and corrupted size > Max_size
    // touches zero elements.
    TrackedContainer tracked_prefix_lhs{TrackedElement{kWord0}, TrackedElement{kWord1}};
    TrackedContainer tracked_prefix_rhs{TrackedElement{kWord0}, TrackedElement{kWord1}};
    tracked_prefix_lhs.data[2] = TrackedElement{kWord2};
    tracked_prefix_rhs.data[2] = TrackedElement{kWord3};
    g_tracked_element_comparisons = 0;
    EXPECT_TRUE(tracked_prefix_lhs == tracked_prefix_rhs);
    EXPECT_EQ(g_tracked_element_comparisons, static_cast<int>(kPairCount));

    std::array<TrackedContainer, kGuardArenaSlots> tracked_lhs_arena{};
    std::array<TrackedContainer, kGuardArenaSlots> tracked_rhs_arena{};
    tracked_lhs_arena[0].size = kSmallCapacity + kCorruptedOverreadOffset;
    tracked_rhs_arena[0].size = kSmallCapacity + kCorruptedOverreadOffset;

    g_tracked_element_comparisons = 0;
    EXPECT_FALSE(tracked_lhs_arena[0] == tracked_rhs_arena[0]);
    EXPECT_EQ(g_tracked_element_comparisons, 0);
    ExpectNeWhenAvailable(tracked_lhs_arena[0], tracked_rhs_arena[0], true);
}

}  // namespace
}  // namespace score::gateway_ipc_binding
