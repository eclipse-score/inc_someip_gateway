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

#include <benchmark/benchmark.h>
#include <unistd.h>

#include <chrono>
#include <cstddef>
#include <string>

#include "event_transmission_benchmark_context.hpp"

namespace score::gateway_ipc_binding {
namespace {

void benchmark_event_transmission_client_to_server(benchmark::State& state) {
    Event_transmission_benchmark_context context(static_cast<std::uint32_t>(state.range(0)));

    for (auto _ : state) {
        auto const duration = context.send_and_measure_once();
        if (!duration) {
            state.SkipWithError("Failed to send or receive benchmark event: " +
                                std::string{duration.error().Message()});
            return;
        }
        state.SetIterationTime(std::chrono::duration<double>(duration.value()).count());
    }

    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                            static_cast<std::int64_t>(state.range(0)));
}

BENCHMARK(benchmark_event_transmission_client_to_server)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(1024 * 1024)
    ->UseManualTime();

}  // namespace
}  // namespace score::gateway_ipc_binding
