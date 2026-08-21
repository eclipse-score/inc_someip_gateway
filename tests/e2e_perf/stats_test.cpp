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

#include "stats.h"

#include <gtest/gtest.h>

namespace perf_service {
namespace {

TEST(PercentileTest, EmptyInputReturnsZero) { EXPECT_DOUBLE_EQ(Percentile({}, 50.0), 0.0); }

TEST(PercentileTest, SingleElementReturnsThatElement) {
    EXPECT_DOUBLE_EQ(Percentile({42}, 0.0), 42.0);
    EXPECT_DOUBLE_EQ(Percentile({42}, 99.0), 42.0);
}

TEST(PercentileTest, InterpolatesBetweenRanks) {
    const std::vector<std::uint64_t> samples{0, 10, 20, 30};
    EXPECT_DOUBLE_EQ(Percentile(samples, 0.0), 0.0);
    EXPECT_DOUBLE_EQ(Percentile(samples, 50.0), 15.0);
    EXPECT_DOUBLE_EQ(Percentile(samples, 100.0), 30.0);
}

TEST(ComputeLatencyStatsTest, EmptyInputYieldsZeroedStats) {
    const auto stats = ComputeLatencyStats({});
    EXPECT_EQ(stats.count, 0U);
    EXPECT_DOUBLE_EQ(stats.mean_ns, 0.0);
    EXPECT_DOUBLE_EQ(stats.p99_ns, 0.0);
}

TEST(ComputeLatencyStatsTest, SortsUnorderedInput) {
    const auto stats = ComputeLatencyStats({30, 10, 20, 0});
    EXPECT_EQ(stats.count, 4U);
    EXPECT_DOUBLE_EQ(stats.min_ns, 0.0);
    EXPECT_DOUBLE_EQ(stats.max_ns, 30.0);
    EXPECT_DOUBLE_EQ(stats.mean_ns, 15.0);
    EXPECT_DOUBLE_EQ(stats.p50_ns, 15.0);
}

TEST(ComputeLatencyStatsTest, ComputesStandardDeviation) {
    const auto stats = ComputeLatencyStats({2, 4, 4, 4, 5, 5, 7, 9});
    EXPECT_DOUBLE_EQ(stats.mean_ns, 5.0);
    EXPECT_DOUBLE_EQ(stats.stddev_ns, 2.0);
}

TEST(WriteLatencyStatsJsonTest, EmitsAllFields) {
    std::ostringstream out{};
    WriteLatencyStatsJson(out, ComputeLatencyStats({1, 2, 3}));
    const auto json = out.str();
    for (const auto* key :
         {"count", "min_ns", "max_ns", "mean_ns", "stddev_ns", "p50_ns", "p90_ns", "p99_ns"}) {
        EXPECT_NE(json.find(key), std::string::npos) << "missing key " << key << " in " << json;
    }
}

}  // namespace
}  // namespace perf_service
