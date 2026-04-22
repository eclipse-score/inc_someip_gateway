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

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>

#include "event_transmission_benchmark_context.hpp"

using namespace std::chrono_literals;

int main() {
    score::gateway_ipc_binding::Event_transmission_benchmark_context context(1024 * 1024);

    auto const start = std::chrono::steady_clock::now();
    std::chrono::nanoseconds duration{};

    std::size_t const total_iterations = 1000000;

    for (std::size_t i = 0; i < total_iterations; ++i) {
        if ((i + 1) % (total_iterations / 10) == 0) {
            std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count()
                      << " ms: Completed " << (i + 1) << " / " << total_iterations << " iterations."
                      << std::endl;
        }
        auto const current_duration = context.send_and_measure_once();
        if (!current_duration) {
            std::cerr << "Benchmark failed at iteration " << i << ": " << current_duration.error()
                      << std::endl;
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
