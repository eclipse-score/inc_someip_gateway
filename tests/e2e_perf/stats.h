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
#ifndef TESTS_E2E_PERF_STATS_H
#define TESTS_E2E_PERF_STATS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ostream>
#include <vector>

namespace perf_service {

struct LatencyStats {
    std::size_t count{0};
    double min_ns{0.0};
    double max_ns{0.0};
    double mean_ns{0.0};
    double stddev_ns{0.0};
    double p50_ns{0.0};
    double p90_ns{0.0};
    double p99_ns{0.0};
};

/// Linear interpolation between closest ranks. `sorted` must be sorted ascending and non-empty.
inline double Percentile(const std::vector<std::uint64_t>& sorted, double percentile) {
    if (sorted.empty()) {
        return 0.0;
    }
    const double rank = (percentile / 100.0) * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(rank));
    const auto upper = static_cast<std::size_t>(std::ceil(rank));
    if (lower == upper) {
        return static_cast<double>(sorted[lower]);
    }
    const double weight = rank - static_cast<double>(lower);
    return static_cast<double>(sorted[lower]) * (1.0 - weight) +
           static_cast<double>(sorted[upper]) * weight;
}

inline LatencyStats ComputeLatencyStats(std::vector<std::uint64_t> samples) {
    LatencyStats stats{};
    if (samples.empty()) {
        return stats;
    }
    std::sort(samples.begin(), samples.end());

    stats.count = samples.size();
    stats.min_ns = static_cast<double>(samples.front());
    stats.max_ns = static_cast<double>(samples.back());

    double sum{0.0};
    for (const auto sample : samples) {
        sum += static_cast<double>(sample);
    }
    stats.mean_ns = sum / static_cast<double>(samples.size());

    double variance{0.0};
    for (const auto sample : samples) {
        const double delta = static_cast<double>(sample) - stats.mean_ns;
        variance += delta * delta;
    }
    stats.stddev_ns = std::sqrt(variance / static_cast<double>(samples.size()));

    stats.p50_ns = Percentile(samples, 50.0);
    stats.p90_ns = Percentile(samples, 90.0);
    stats.p99_ns = Percentile(samples, 99.0);
    return stats;
}

inline void WriteLatencyStatsJson(std::ostream& out, const LatencyStats& stats) {
    out << "{\"count\": " << stats.count << ", \"min_ns\": " << stats.min_ns
        << ", \"max_ns\": " << stats.max_ns << ", \"mean_ns\": " << stats.mean_ns
        << ", \"stddev_ns\": " << stats.stddev_ns << ", \"p50_ns\": " << stats.p50_ns
        << ", \"p90_ns\": " << stats.p90_ns << ", \"p99_ns\": " << stats.p99_ns << "}";
}

}  // namespace perf_service

#endif  // TESTS_E2E_PERF_STATS_H
