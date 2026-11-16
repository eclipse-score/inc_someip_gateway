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

#include <cstddef>
#include <iterator>

namespace score::gateway_ipc_binding {
namespace {

constexpr std::size_t kContainerCapacity{4U};
constexpr std::size_t kEmptySize{0U};
constexpr std::size_t kFirstIndex{0U};
constexpr std::size_t kSecondIndex{1U};
constexpr std::size_t kThirdIndex{2U};
constexpr std::size_t kFourthIndex{3U};
constexpr std::size_t kTrailingIndex{2U};
constexpr int kFirstElement{10};
constexpr int kSecondElement{20};
constexpr int kThirdElement{30};
constexpr int kFourthElement{40};
constexpr int kDifferentElement{99};

using TestContainer = Fixed_size_container<int, kContainerCapacity>;

TEST(FixedSizeContainerTest, StaticMaxSizeMatchesTemplateParameter) {
    static_assert(TestContainer::max_size == kContainerCapacity);
    EXPECT_EQ(TestContainer::max_size, kContainerCapacity);
}

TEST(FixedSizeContainerTest, ContainerWithZeroSizeReportsEmpty) {
    TestContainer empty_container{};
    empty_container.size = kEmptySize;
    EXPECT_TRUE(empty_container.empty());

    TestContainer const non_empty_container{kFirstElement};
    EXPECT_FALSE(non_empty_container.empty());
}

TEST(FixedSizeContainerTest, CArrayConstructorCopiesElementsAndSetsSize) {
    constexpr int kRawElements[] = {kFirstElement, kSecondElement};
    TestContainer const partial_container{kRawElements};
    EXPECT_FALSE(partial_container.empty());
    EXPECT_EQ(partial_container.size, std::size(kRawElements));
    EXPECT_EQ(partial_container.data[kFirstIndex], kFirstElement);
    EXPECT_EQ(partial_container.data[kSecondIndex], kSecondElement);

    constexpr int kFullElements[] = {kFirstElement, kSecondElement, kThirdElement, kFourthElement};
    static_assert(std::size(kFullElements) == kContainerCapacity);
    TestContainer const full_container{kFullElements};
    EXPECT_FALSE(full_container.empty());
    EXPECT_EQ(full_container.size, std::size(kFullElements));
    EXPECT_EQ(full_container.data[kFirstIndex], kFirstElement);
    EXPECT_EQ(full_container.data[kSecondIndex], kSecondElement);
    EXPECT_EQ(full_container.data[kThirdIndex], kThirdElement);
    EXPECT_EQ(full_container.data[kFourthIndex], kFourthElement);
}

TEST(FixedSizeContainerTest, InitializerListConstructorCopiesElementsAndMatchesArrayConstructed) {
    constexpr int kRawElements[] = {kFirstElement, kSecondElement};
    TestContainer const from_array{kRawElements};
    TestContainer const from_init_list{kFirstElement, kSecondElement};

    EXPECT_FALSE(from_init_list.empty());
    EXPECT_EQ(from_init_list.size, std::size(kRawElements));
    EXPECT_EQ(from_init_list.data[kFirstIndex], kFirstElement);
    EXPECT_EQ(from_init_list.data[kSecondIndex], kSecondElement);
    EXPECT_EQ(from_array, from_init_list);
}

TEST(FixedSizeContainerTest, EqualityComparesSizeAndActiveElementsWhileIgnoringTrailingStorage) {
    TestContainer const baseline{kFirstElement, kSecondElement};
    TestContainer const identical{kFirstElement, kSecondElement};
    TestContainer const different_size{kFirstElement, kSecondElement, kThirdElement};
    TestContainer const different_active_element{kFirstElement, kDifferentElement};

    TestContainer baseline_with_other_trailing{kFirstElement, kSecondElement};
    baseline_with_other_trailing.data[kTrailingIndex] = kThirdElement;

    TestContainer different_trailing_storage{kFirstElement, kSecondElement};
    different_trailing_storage.data[kTrailingIndex] = kDifferentElement;

    EXPECT_TRUE(baseline == identical);
    EXPECT_FALSE(baseline == different_size);
    EXPECT_FALSE(baseline == different_active_element);
    EXPECT_TRUE(baseline_with_other_trailing == different_trailing_storage);
}

}  // namespace
}  // namespace score::gateway_ipc_binding
