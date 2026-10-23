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

#include <unistd.h>

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>

#include "event_transmission_benchmark_context.hpp"
#include "score/span.hpp"

using namespace std::chrono_literals;

int main(int argc, char** argv) {
    score::cpp::span<char*> const arguments{argv, static_cast<std::size_t>(argc)};
    std::size_t total_iterations = 1000000U;
    if (argc > 2) {
        std::cerr << "Usage: " << arguments[0] << " [--iterations=N]" << std::endl;
        return EXIT_FAILURE;
    }
    if (argc == 2) {
        std::string_view const argument{arguments[1]};
        constexpr std::string_view prefix{"--iterations="};
        if (argument.substr(0U, prefix.size()) != prefix) {
            std::cerr << "Expected --iterations=N" << std::endl;
            return EXIT_FAILURE;
        }
        auto const value = argument.substr(prefix.size());
        auto const parsed =
            std::from_chars(value.data(), value.data() + value.size(), total_iterations);
        if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
            total_iterations == 0U) {
            std::cerr << "Iterations must be a positive integer" << std::endl;
            return EXIT_FAILURE;
        }
    }
    score::gateway_ipc_binding::initialize_benchmark_runtime();
    score::gateway_ipc_binding::Event_transmission_benchmark_context context(1024 * 1024);

    auto const start = std::chrono::steady_clock::now();
    std::chrono::nanoseconds duration{};

    std::size_t const progress_interval = total_iterations < 10U ? 1U : total_iterations / 10U;

    for (std::size_t iteration = 0; iteration < total_iterations; ++iteration) {
        if ((iteration + 1) % progress_interval == 0) {
            std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count()
                      << " ms: Completed " << (iteration + 1) << " / " << total_iterations
                      << " iterations." << std::endl;
        }
        auto const current_duration = context.send_and_measure_once();
        if (!current_duration) {
            std::cerr << "Benchmark failed at iteration " << iteration << ": "
                      << current_duration.error() << std::endl;
            return EXIT_FAILURE;
        }
        duration += current_duration.value();
    }

    std::cout << "Benchmark completed successfully in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(duration).count()
              << " milliseconds." << std::endl;

    auto const end = std::chrono::steady_clock::now();
    std::cout << "Total execution time: "
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()
              << " milliseconds." << std::endl;

    return EXIT_SUCCESS;
}
