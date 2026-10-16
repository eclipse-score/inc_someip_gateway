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

#include <gtest/gtest.h>

#include <cstddef>
#include <iterator>
#include <type_traits>
#include <vector>

#include "score/socom/payload.hpp"
#include "score/socom/vector_payload.hpp"

namespace score::socom {

static_assert(std::is_move_assignable<Payload>::value, "");
static_assert(std::is_move_constructible<Payload>::value, "");
static_assert(!std::is_copy_assignable<Payload>::value, "");
static_assert(!std::is_copy_constructible<Payload>::value, "");

template <typename SPAN, typename CONT>
void check_span(SPAN const& span, CONT const& cont) {
    ASSERT_EQ(cont.size(), span.size());
    EXPECT_TRUE(std::equal(std::begin(cont), std::end(cont), std::begin(span)));
}

void check_payload(Payload const& payload, Vector_buffer const& cont) {
    check_span(payload.data(), cont);
}

TEST(Payload, NoPayloadConstruct) { EXPECT_EQ(0, empty_payload().data().size()); }

TEST(Payload, NoPayloadMoveConstruct) {
    Payload copy{empty_payload()};

    EXPECT_EQ(empty_payload(), copy);
    EXPECT_EQ(0, copy.data().size());
}

TEST(Payload, EmptyPayloadGetSlotHandleReturnsNoSlotHandle) {
    EXPECT_EQ(kNoSlotHandle, empty_payload().get_slot_handle());
}

TEST(Payload, DefaultConstructedPayloadHasNoDataAndNoSlotHandle) {
    Payload const payload{};

    EXPECT_EQ(0U, payload.data().size());
    EXPECT_EQ(kNoSlotHandle, payload.get_slot_handle());
}

TEST(Payload, VectorPayloadConstructInitializerList) {
    auto const payload =
        make_vector_payload({std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}});
    check_payload(payload, make_vector_buffer(1U, 2U, 3U, 4U, 5U));
}

TEST(Payload, VectorPayloadConstructVectorLVR) {
    auto const temp_buffer = make_vector_buffer(1U, 2U, 3U, 4U, 5U);
    auto const payload = make_vector_payload(temp_buffer);
    check_payload(payload, temp_buffer);
}

TEST(Payload, OperatorEqual) {
    auto const payload0 = make_vector_payload(make_vector_buffer(1U, 2U, 3U, 4U, 5U));
    auto const payload0_same = make_vector_payload(make_vector_buffer(1U, 2U, 3U, 4U, 5U));
    auto const payload1 = make_vector_payload(make_vector_buffer(1U, 2U, 3U, 32U, 43U, 43U));
    auto const payload2 = make_vector_payload(make_vector_buffer(1U, 2U, 2U, 32U, 43U, 43U));
    auto const payload3 = make_vector_payload({});

    EXPECT_EQ(payload0, payload0);
    EXPECT_EQ(payload0, payload0_same);
    EXPECT_EQ(payload1, payload1);
    EXPECT_EQ(payload2, payload2);
    EXPECT_EQ(payload3, payload3);
    EXPECT_NE(payload0, payload1);
    EXPECT_NE(payload0, payload2);
    EXPECT_NE(payload0, payload3);
    EXPECT_NE(payload1, payload2);
    EXPECT_NE(payload1, payload3);
    EXPECT_NE(payload2, payload3);
}

TEST(Payload, VectorPayloadGetSlotHandleReturnsNoSlotHandle) {
    auto const payload = make_vector_payload(make_vector_buffer(1U, 2U, 3U));
    EXPECT_EQ(kNoSlotHandle, payload.get_slot_handle());
}

TEST(Payload, DestructorCallsOnPayloadDestroyed) {
    std::size_t destroyed_count = 0;
    {
        auto payload = Payload{Payload::Span{}, 0, [&destroyed_count]() { ++destroyed_count; }};
        EXPECT_EQ(0U, destroyed_count);
    }
    EXPECT_EQ(1U, destroyed_count);
}

TEST(Payload, DestructorCallsOnPayloadDestroyedForMoveAssignedAndAssignee) {
    std::size_t destroyed_count = 0;
    {
        auto payload1 = Payload{Payload::Span{}, 0, [&destroyed_count]() { ++destroyed_count; }};
        auto payload2 = Payload{Payload::Span{}, 0, [&destroyed_count]() { ++destroyed_count; }};
        EXPECT_EQ(0U, destroyed_count);

        payload1 = std::move(payload2);

        // original payload1 destroyed, but payload1 now owns the payload2 destructor callback, so
        // only one call expected
        EXPECT_EQ(1U, destroyed_count);
    }
    EXPECT_EQ(2U, destroyed_count);
}

TEST(Payload, DestructorDoesNotCallOnPayloadDestroyedForMoveConstructed) {
    std::size_t destroyed_count = 0;
    {
        auto payload1 = Payload{Payload::Span{}, 0, [&destroyed_count]() { ++destroyed_count; }};
        EXPECT_EQ(0U, destroyed_count);

        auto payload = std::move(payload1);
        EXPECT_EQ(0U, destroyed_count);
    }
    EXPECT_EQ(1U, destroyed_count);
}

TEST(Payload, WritablePayloadSetSizeReducesDataSize) {
    auto payload = make_writable_vector_payload(100);
    EXPECT_EQ(100U, payload.data().size());

    bool result = payload.shrink(50);
    EXPECT_TRUE(result);
    EXPECT_EQ(50U, payload.data().size());
}

TEST(Payload, WritablePayloadSetSizeReturnsFalseWhenIncreasingSize) {
    auto payload = make_writable_vector_payload(50);
    EXPECT_EQ(50U, payload.data().size());

    bool result = payload.shrink(100);
    EXPECT_FALSE(result);
    EXPECT_EQ(50U, payload.data().size());
}

TEST(Payload, WritablePayloadSetSizeWithZero) {
    auto payload = make_writable_vector_payload(100);
    EXPECT_EQ(100U, payload.data().size());

    bool result = payload.shrink(0);
    EXPECT_TRUE(result);
    EXPECT_EQ(0U, payload.data().size());
}

TEST(Payload, WritablePayloadSetSizeToSameSizeSucceeds) {
    auto payload = make_writable_vector_payload(100);
    EXPECT_EQ(100U, payload.data().size());

    bool result = payload.shrink(100);
    EXPECT_TRUE(result);
    EXPECT_EQ(100U, payload.data().size());
}

TEST(Payload, WritablePayloadSetSizeMultipleTimes) {
    auto payload = make_writable_vector_payload(100);

    EXPECT_TRUE(payload.shrink(80));
    EXPECT_EQ(80U, payload.data().size());

    EXPECT_TRUE(payload.shrink(50));
    EXPECT_EQ(50U, payload.data().size());

    EXPECT_TRUE(payload.shrink(25));
    EXPECT_EQ(25U, payload.data().size());

    // Try to increase - should fail
    EXPECT_FALSE(payload.shrink(30));
    EXPECT_EQ(25U, payload.data().size());
}

TEST(Payload, WritablePayloadWdataAndDataShareTheBuffer) {
    auto payload = make_writable_vector_payload(100);

    EXPECT_TRUE(payload.shrink(50));
    EXPECT_EQ(payload.data().data(), payload.wdata().data());
    EXPECT_EQ(50U, payload.wdata().size());
}

TEST(Payload, WritablePayloadMoveAssignedFromWritablePayloadHasWritableData) {
    auto payload = make_writable_vector_payload(32);

    payload = make_writable_vector_payload(16);

    EXPECT_EQ(payload.data().data(), payload.wdata().data());
    EXPECT_EQ(16U, payload.wdata().size());
}

TEST(Payload, WritablePayloadMoveAssignedFromReadOnlyPayloadHasNoWritableData) {
    auto payload = make_writable_vector_payload(32);

    static_cast<Payload&>(payload) = make_vector_payload(make_vector_buffer(1U, 2U, 3U));

    EXPECT_EQ(3U, payload.data().size());
    EXPECT_EQ(0U, payload.wdata().size());
}

}  // namespace score::socom
