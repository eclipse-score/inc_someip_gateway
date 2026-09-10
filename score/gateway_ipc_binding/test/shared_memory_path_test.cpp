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

#include <string>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/gateway_ipc_binding/fixed_size_container.hpp"
#include "score/gateway_ipc_binding/gateway_ipc_binding.hpp"

namespace score::gateway_ipc_binding {
namespace {

std::string path_of(Result<Shared_memory_path> const& result) {
    return fixed_string_to_string(*result);
}

TEST(Shared_memory_path_test, plain_name_is_unchanged) {
    EXPECT_EQ(path_of(make_shared_memory_path("echo_response", 17185U)), "/echo_response_17185");
    EXPECT_EQ(path_of(make_counterpart_shared_memory_path("echo_response", 17185U)),
              "/counterpart_echo_response_17185");
}

TEST(Shared_memory_path_test, embedded_slashes_become_underscores) {
    EXPECT_EQ(path_of(make_shared_memory_path("bench/echo_response", 17185U)),
              "/bench_echo_response_17185");
    EXPECT_EQ(path_of(make_counterpart_shared_memory_path("bench/echo_response", 17185U)),
              "/counterpart_bench_echo_response_17185");
}

TEST(Shared_memory_path_test, namespaced_name_yields_a_single_path_component) {
    auto const path = path_of(
        make_shared_memory_path("/car_window_common/car_window_info/CarWindowInfo", 17185U));

    EXPECT_EQ(path, "/car_window_common_car_window_info_CarWindowInfo_17185");
    // Exactly one leading slash and nothing else: this is what shm_open requires.
    EXPECT_EQ(path.find('/', 1U), std::string::npos);
}

TEST(Shared_memory_path_test, leading_slash_is_dropped_not_substituted) {
    // Substituting it would give "/_name" here and "counterpart__name" below.
    EXPECT_EQ(path_of(make_shared_memory_path("/CarWindowInfo", 1U)), "/CarWindowInfo_1");
    EXPECT_EQ(path_of(make_counterpart_shared_memory_path("/CarWindowInfo", 1U)),
              "/counterpart_CarWindowInfo_1");
}

TEST(Shared_memory_path_test, repeated_leading_slashes_are_all_dropped) {
    EXPECT_EQ(path_of(make_shared_memory_path("//a/b", 1U)), "/a_b_1");
}

TEST(Shared_memory_path_test, empty_name_still_produces_a_valid_path) {
    EXPECT_EQ(path_of(make_shared_memory_path("", 1U)), "/_1");
}

TEST(Shared_memory_path_test, name_at_the_size_limit_is_accepted) {
    // "/" + name + "_" + "1" must be exactly kMax_shared_memory_path_size.
    std::string const name(kMax_shared_memory_path_size - 3U, 'a');

    auto const result = make_shared_memory_path(name, 1U);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(path_of(result).size(), kMax_shared_memory_path_size);
}

TEST(Shared_memory_path_test, name_beyond_the_size_limit_is_rejected) {
    std::string const name(kMax_shared_memory_path_size, 'a');

    auto const result = make_shared_memory_path(name, 1U);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Gateway_ipc_binding_error::fixed_size_container_too_small);
}

TEST(Shared_memory_path_test, counterpart_hits_the_limit_before_the_data_path) {
    // The counterpart prefix is 12 bytes longer, so a name can be valid for one and not
    // the other. Callers must check both results, not just the first.
    std::string const name(kMax_shared_memory_path_size - 3U, 'a');

    EXPECT_TRUE(make_shared_memory_path(name, 1U).has_value());
    EXPECT_FALSE(make_counterpart_shared_memory_path(name, 1U).has_value());
}

}  // namespace
}  // namespace score::gateway_ipc_binding
