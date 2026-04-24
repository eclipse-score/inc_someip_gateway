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

#include <cassert>
#include <cstddef>
#include <memory>
#include <string>

#include "../src/shared_memory_managers.hpp"

namespace score::gateway_ipc_binding {
namespace {

class Read_only_shared_memory_slot_manager_benchmark final
    : public Read_only_shared_memory_slot_manager {
   public:
    socom::Payload::Uptr get_payload(
        Shared_memory_handle,
        [[maybe_unused]] On_payload_destruction_callback callback) const noexcept override {
        return nullptr;
    }
};

class Shared_memory_manager_factory_benchmark final : public Shared_memory_manager_factory {
   public:
    Result<Shared_memory_slot_manager::Uptr> create(
        [[maybe_unused]] score::socom::Service_interface_identifier const& interface,
        [[maybe_unused]] score::socom::Service_instance const& instance) noexcept override {
        assert(false && "Not used by this benchmark");
        return {};
    }

    Result<Read_only_shared_memory_slot_manager::Uptr> open(
        [[maybe_unused]] Shared_memory_metadata const& metadata) noexcept override {
        return Read_only_shared_memory_slot_manager::Uptr{
            std::make_unique<Read_only_shared_memory_slot_manager_benchmark>()};
    }
};

void benchmark_cached_read_only_lookup(benchmark::State& state) {
    auto factory = std::make_shared<Shared_memory_manager_factory_benchmark>();
    Read_only_memory_managers managers{factory};

    Shared_memory_metadata metadata{};
    std::string const path(static_cast<std::size_t>(state.range(0)), 'a');
    auto result = fixed_string_from_string<Shared_memory_path>(path);
    assert(result && "Path should fit into fixed-size metadata path");
    metadata.path = *result;
    metadata.slot_size = 1024U;
    metadata.slot_count = 8U;

    auto& warmup = managers.get_read_only_shared_memory_slot_manager(metadata);
    benchmark::DoNotOptimize(&warmup);

    for (auto _ : state) {
        auto& manager = managers.get_read_only_shared_memory_slot_manager(metadata);
        benchmark::DoNotOptimize(&manager);
    }
}

BENCHMARK(benchmark_cached_read_only_lookup)->Arg(32)->Arg(128)->Arg(256);

}  // namespace
}  // namespace score::gateway_ipc_binding
