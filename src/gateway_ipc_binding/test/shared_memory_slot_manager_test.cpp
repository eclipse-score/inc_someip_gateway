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
#include <score/memory/shared/shared_memory_factory.h>
#include <unistd.h>

#include <cstring>
#include <score/gateway_ipc_binding/error.hpp>
#include <score/gateway_ipc_binding/fixed_size_container.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/gateway_ipc_binding/shared_memory_slot_manager.hpp>
#include <string>
#include <thread>
#include <vector>

#include "gtest/gtest.h"

namespace score::gateway_ipc_binding {

template <typename Target_type>
Fixed_string<Target_type::max_size> fixed_string_from_string_asserted(
    std::string_view value) noexcept {
    auto result = fixed_string_from_string<Target_type>(value);
    assert(result && "String exceeds maximum size for fixed string");
    return *result;
}

class Shared_memory_slot_manager_test : public ::testing::Test {
   protected:
    static constexpr std::size_t DEFAULT_NUMBER_OF_SLOTS = 256;  // 256 slots
    static constexpr std::size_t DEFAULT_SLOT_SIZE = 4096;       // 4 KB

    std::string get_unique_path() {
        static int counter = 0;
        return "/test_shm_slot_manager_" + std::to_string(getpid()) + "_" +
               std::to_string(++counter);
    }

    socom::Service_interface_identifier const interface{
        "com.test.interface", socom::Literal_tag{}, {1, 0}};

    socom::Service_instance const instance{"instance1", socom::Literal_tag{}};
    Shared_memory_metadata const shared_memory_metadata{
        fixed_string_from_string_asserted<Shared_memory_path>(get_unique_path()), DEFAULT_SLOT_SIZE,
        DEFAULT_NUMBER_OF_SLOTS};

    socom::Service_instance const instance_other_size{"instance_other_size", socom::Literal_tag{}};
    Shared_memory_metadata const shared_memory_metadata_other_size{
        fixed_string_from_string_asserted<Shared_memory_path>(get_unique_path()), 100, 4};

    socom::Service_instance const instance_zero_slot_size{"instance_zero_slot_size",
                                                          socom::Literal_tag{}};
    Shared_memory_metadata const shared_memory_metadata_zero_slot_size{
        fixed_string_from_string_asserted<Shared_memory_path>(get_unique_path()), 0, 100};

    socom::Service_instance const instance_zero_slot_count{"instance_zero_slot_count",
                                                           socom::Literal_tag{}};
    Shared_memory_metadata const shared_memory_metadata_zero_slot_count{
        fixed_string_from_string_asserted<Shared_memory_path>(get_unique_path()), 100, 0};

    Shared_memory_manager_factory::Shared_memory_configuration const config{
        {interface,
         {
             {instance, shared_memory_metadata},
             {instance_other_size, shared_memory_metadata_other_size},
             {instance_zero_slot_size, shared_memory_metadata_zero_slot_size},
             {instance_zero_slot_count, shared_memory_metadata_zero_slot_count},
         }}};

    Shared_memory_manager_factory::Uptr memory_manager_factory =
        Shared_memory_manager_factory::create(config);

    Result<std::unique_ptr<Shared_memory_slot_manager>> manager_result =
        memory_manager_factory->create(interface, instance);

    Shared_memory_slot_manager& manager = **manager_result;

    void SetUp() override { ASSERT_TRUE(manager_result); }
};

using Shared_memory_slot_manager_death_test = Shared_memory_slot_manager_test;

// Test: Construction with valid parameters
TEST_F(Shared_memory_slot_manager_test, construction_with_valid_parameters) {
    EXPECT_EQ(manager.get_slot_count(), DEFAULT_NUMBER_OF_SLOTS);
    EXPECT_EQ(manager.get_slot_size(), DEFAULT_SLOT_SIZE);
    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

// Test: Construction with various sizes
TEST_F(Shared_memory_slot_manager_test, construction_with_other_size) {
    auto manager_result1 = memory_manager_factory->create(interface, instance_other_size);
    ASSERT_TRUE(manager_result1);
    auto& manager1 = *manager_result1;
    EXPECT_EQ(manager1->get_slot_count(), shared_memory_metadata_other_size.slot_count);
    EXPECT_EQ(manager1->get_slot_size(), shared_memory_metadata_other_size.slot_size);
}

// Test: Construction with slot_size == 0 throws
TEST_F(Shared_memory_slot_manager_test, construction_with_zero_slot_size_throws) {
    EXPECT_EQ(memory_manager_factory->create(interface, instance_zero_slot_size),
              MakeUnexpected(Shared_memory_manager_error::logic_error_invalid_slot_size));
}

// Test: Construction with number_of_slots == 0 throws
TEST_F(Shared_memory_slot_manager_test, construction_with_zero_number_of_slots_throws) {
    EXPECT_EQ(memory_manager_factory->create(interface, instance_zero_slot_count),
              MakeUnexpected(Shared_memory_manager_error::logic_error_invalid_slot_count));
}

// Test: Single slot allocation and release
TEST_F(Shared_memory_slot_manager_test, single_slot_allocation_and_release) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto& guard = *guard_opt;
    auto memory = guard.get_memory();
    EXPECT_NE(memory.data(), nullptr);
    auto handle_result = guard.get_handle();
    ASSERT_TRUE(handle_result);
    auto handle = *handle_result;
    EXPECT_EQ(handle, 0);
    EXPECT_EQ(memory.size(), DEFAULT_SLOT_SIZE);
    EXPECT_EQ(manager.get_allocated_slot_count(), 1);
    EXPECT_EQ(manager.get_reference_count(handle), 1);

    auto rel_result = manager.release_slot(handle);
    EXPECT_TRUE(rel_result);
    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
    EXPECT_EQ(manager.get_reference_count(handle), 0);
}

// Test: Write and read data in slot
TEST_F(Shared_memory_slot_manager_test, write_and_read_data_in_slot) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto& guard = *guard_opt;

    std::string const test_data = "Hello, Shared Memory!";
    auto memory = guard.get_memory();
    std::memcpy(memory.data(), test_data.c_str(), test_data.size() + 1);

    char* read_data = reinterpret_cast<char*>(memory.data());
    EXPECT_STREQ(read_data, test_data.c_str());

    auto handle_result = guard.get_handle();
    ASSERT_TRUE(handle_result);
    auto rel_result = manager.release_slot(*handle_result);
    EXPECT_TRUE(rel_result);
}

// Test: Allocate all slots
TEST_F(Shared_memory_slot_manager_test, allocate_all_slots) {
    auto manager_result = memory_manager_factory->create(interface, instance_other_size);
    ASSERT_TRUE(manager_result);
    auto& manager = **manager_result;

    std::size_t const slot_count = manager.get_slot_count();
    std::vector<Shared_memory_slot_guard> guards;

    for (std::size_t i = 0; i < slot_count; ++i) {
        auto guard_opt = manager.allocate_slot();
        ASSERT_TRUE(guard_opt);
        guards.push_back(std::move(*guard_opt));
    }

    EXPECT_EQ(manager.get_allocated_slot_count(), slot_count);

    // Next allocation should fail
    auto failed_guard = manager.allocate_slot();
    EXPECT_FALSE(failed_guard);

    // Release all slots by destroying guards
    guards.clear();

    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

// Test: Slot reuse after release
TEST_F(Shared_memory_slot_manager_test, slot_reuse_after_release) {
    auto guard1_opt = manager.allocate_slot();
    ASSERT_TRUE(guard1_opt);
    auto handle_result = guard1_opt->get_handle();
    ASSERT_TRUE(handle_result);
    auto handle1 = *handle_result;

    auto rel_result = manager.release_slot(handle1);
    EXPECT_TRUE(rel_result);

    auto guard2_opt = manager.allocate_slot();
    ASSERT_TRUE(guard2_opt);
    auto h2_result = guard2_opt->get_handle();
    ASSERT_TRUE(h2_result);
    EXPECT_EQ(*h2_result, handle1);  // Should reuse the same slot
    auto mem2 = guard2_opt->get_memory();
    EXPECT_EQ(mem2.size(), DEFAULT_SLOT_SIZE);
}

// Test: Multi-consumer reference counting
TEST_F(Shared_memory_slot_manager_test, multi_consumer_reference_counting) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto& guard = *guard_opt;
    auto handle_result = guard.get_handle();
    ASSERT_TRUE(handle_result);
    auto handle = *handle_result;
    EXPECT_EQ(manager.get_reference_count(handle), 1);

    // Add two more consumers
    auto add_result1 = manager.add_consumer(handle);
    EXPECT_TRUE(add_result1);
    EXPECT_EQ(manager.get_reference_count(handle), 2);

    auto add_result2 = manager.add_consumer(handle);
    EXPECT_TRUE(add_result2);
    EXPECT_EQ(manager.get_reference_count(handle), 3);

    // Release once - still allocated
    auto rel_result1 = manager.release_slot(handle);
    EXPECT_TRUE(rel_result1);
    EXPECT_EQ(manager.get_reference_count(handle), 2);
    EXPECT_EQ(manager.get_allocated_slot_count(), 1);

    // Release second time - still allocated
    auto rel_result2 = manager.release_slot(handle);
    EXPECT_TRUE(rel_result2);
    EXPECT_EQ(manager.get_reference_count(handle), 1);
    EXPECT_EQ(manager.get_allocated_slot_count(), 1);

    // Release third time - now freed
    auto rel_result3 = manager.release_slot(handle);
    EXPECT_TRUE(rel_result3);
    EXPECT_EQ(manager.get_reference_count(handle), 0);
    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

// Test: Invalid handle operations
TEST_F(Shared_memory_slot_manager_test, invalid_handle_operations) {
    Slot_handle invalid_handle = manager.get_slot_count() + 10;

    auto add_result = manager.add_consumer(invalid_handle);
    EXPECT_FALSE(add_result);
    auto rel_result = manager.release_slot(invalid_handle);
    EXPECT_FALSE(rel_result);
    EXPECT_EQ(manager.get_reference_count(invalid_handle), 0);
}

// Test: Cannot add consumer to unallocated slot
TEST_F(Shared_memory_slot_manager_test, cannot_add_consumer_to_unallocated_slot) {
    // Try to add consumer to slot 0 which is not allocated
    auto add_result = manager.add_consumer(0);
    EXPECT_FALSE(add_result);
}

// Test: get_memory with valid handle and allocated slot
TEST_F(Shared_memory_slot_manager_test, get_memory_with_valid_allocated_slot) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto handle_result = guard_opt->get_handle();
    ASSERT_TRUE(handle_result);
    auto handle = *handle_result;

    // Get memory via manager
    auto mem_result = manager.get_memory(handle);
    ASSERT_TRUE(mem_result);
    auto memory = *mem_result;
    EXPECT_NE(memory.data(), nullptr);
    EXPECT_EQ(memory.size(), DEFAULT_SLOT_SIZE);

    // Verify it's the same memory as from guard
    auto guard_memory = guard_opt->get_memory();
    EXPECT_EQ(memory.data(), guard_memory.data());
    EXPECT_EQ(memory.size(), guard_memory.size());

    auto rel_result = manager.release_slot(handle);
    EXPECT_TRUE(rel_result);
}

// Test: get_memory with valid handle and data access
TEST_F(Shared_memory_slot_manager_test, get_memory_write_read_via_manager) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto handle_result = guard_opt->get_handle();
    ASSERT_TRUE(handle_result);
    auto handle = *handle_result;

    std::string const test_data = "Test data via get_memory";

    // Write via manager.get_memory()
    auto mem_result = manager.get_memory(handle);
    ASSERT_TRUE(mem_result);
    auto memory = *mem_result;
    std::memcpy(memory.data(), test_data.c_str(), test_data.size() + 1);

    // Read back via guard
    auto guard_memory = guard_opt->get_memory();
    char* read_data = reinterpret_cast<char*>(guard_memory.data());
    EXPECT_STREQ(read_data, test_data.c_str());

    auto rel_result = manager.release_slot(handle);
    EXPECT_TRUE(rel_result);
}

// Test: get_memory with invalid handle
TEST_F(Shared_memory_slot_manager_test, get_memory_with_invalid_handle) {
    Slot_handle invalid_handle = manager.get_slot_count() + 10;

    auto mem_result = manager.get_memory(invalid_handle);
    EXPECT_FALSE(mem_result);
}

// Test: get_memory with unallocated slot
TEST_F(Shared_memory_slot_manager_test, get_memory_with_unallocated_slot) {
    // Try to get memory from slot 0 which is not allocated
    auto mem_result = manager.get_memory(0);
    EXPECT_FALSE(mem_result);
}

// Test: get_memory after slot release
TEST_F(Shared_memory_slot_manager_test, get_memory_after_slot_release) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto handle_result = guard_opt->get_handle();
    ASSERT_TRUE(handle_result);
    auto handle = *handle_result;

    // First, memory should be available
    auto mem_result = manager.get_memory(handle);
    ASSERT_TRUE(mem_result);
    EXPECT_NE((*mem_result).data(), nullptr);

    // Release the slot
    auto rel_result = manager.release_slot(handle);
    EXPECT_TRUE(rel_result);

    // Now memory should not be available
    auto mem_result2 = manager.get_memory(handle);
    EXPECT_FALSE(mem_result2);
}

// Test: Thread safety - concurrent allocations
TEST_F(Shared_memory_slot_manager_test, thread_safety_concurrent_allocations) {
    constexpr int num_threads = 10;
    constexpr int allocations_per_thread = 10;

    std::vector<std::thread> threads;
    std::vector<std::vector<Shared_memory_slot_guard>> thread_guards(num_threads);

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([this, &thread_guards, i]() {
            for (int j = 0; j < allocations_per_thread; ++j) {
                auto guard_opt = manager.allocate_slot();
                if (guard_opt) {
                    thread_guards[i].push_back(std::move(*guard_opt));
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    // Count total successful allocations
    std::size_t total_allocated = 0;
    for (auto const& guards : thread_guards) {
        total_allocated += guards.size();
    }

    EXPECT_EQ(manager.get_allocated_slot_count(), total_allocated);

    // Release all by clearing vectors
    for (auto& guards : thread_guards) {
        guards.clear();
    }

    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

// Test: Thread safety - concurrent add_consumer and release
TEST_F(Shared_memory_slot_manager_test, thread_safety_concurrent_add_consumer_and_release) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto handle_result = guard_opt->get_handle();
    ASSERT_TRUE(handle_result);
    auto handle = *handle_result;

    constexpr int num_consumers = 100;

    // Add consumers concurrently
    std::vector<std::thread> add_threads;
    for (int i = 0; i < num_consumers; ++i) {
        add_threads.emplace_back([this, handle]() { EXPECT_TRUE(manager.add_consumer(handle)); });
    }

    for (auto& thread : add_threads) {
        thread.join();
    }

    EXPECT_EQ(manager.get_reference_count(handle), num_consumers + 1);  // +1 for initial allocation

    // Release concurrently
    std::vector<std::thread> release_threads;
    for (int i = 0; i < num_consumers + 1; ++i) {
        release_threads.emplace_back(
            [this, handle]() { EXPECT_TRUE(manager.release_slot(handle)); });
    }

    for (auto& thread : release_threads) {
        thread.join();
    }

    EXPECT_EQ(manager.get_reference_count(handle), 0);
}

// Test: RAII guard - automatic release
TEST_F(Shared_memory_slot_manager_test, raii_guard_automatic_release) {
    {
        auto guard_opt = manager.allocate_slot();
        ASSERT_TRUE(guard_opt);
        ASSERT_TRUE(guard_opt->has_slot());
        EXPECT_EQ(manager.get_allocated_slot_count(), 1);
    }

    // Guard destroyed, slot should be released
    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

// Test: RAII guard - allocation failure
TEST_F(Shared_memory_slot_manager_test, raii_guard_allocation_failure) {
    auto manager_result = memory_manager_factory->create(interface, instance_other_size);
    ASSERT_TRUE(manager_result);
    auto& manager = **manager_result;

    // Allocate all available slots
    auto guard1_opt = manager.allocate_slot();
    auto guard2_opt = manager.allocate_slot();
    auto guard3_opt = manager.allocate_slot();
    auto guard4_opt = manager.allocate_slot();
    ASSERT_TRUE(guard1_opt);
    ASSERT_TRUE(guard2_opt);
    ASSERT_TRUE(guard3_opt);
    ASSERT_TRUE(guard4_opt);
    ASSERT_TRUE(guard1_opt->has_slot());
    ASSERT_TRUE(guard2_opt->has_slot());
    ASSERT_TRUE(guard3_opt->has_slot());
    ASSERT_TRUE(guard4_opt->has_slot());
    EXPECT_EQ(manager.get_allocated_slot_count(), 4);

    // Next allocation should fail
    auto guard5_opt = manager.allocate_slot();
    EXPECT_FALSE(guard5_opt);
    EXPECT_EQ(manager.get_allocated_slot_count(), 4);
}

// Test: RAII guard - share slot
TEST_F(Shared_memory_slot_manager_test, raii_guard_share_slot) {
    Slot_handle handle;
    {
        auto guard_opt = manager.allocate_slot();
        ASSERT_TRUE(guard_opt);
        auto& guard = *guard_opt;
        ASSERT_TRUE(guard.has_slot());
        auto h_result = guard.get_handle();
        ASSERT_TRUE(h_result);
        handle = *h_result;

        // share() returns a new guard managing a reference to the same slot
        auto shared_guard = guard.share();
        EXPECT_TRUE(shared_guard.has_slot());
        auto sh_result = shared_guard.get_handle();
        ASSERT_TRUE(sh_result);
        EXPECT_EQ(*sh_result, handle);
        EXPECT_EQ(manager.get_reference_count(handle), 2);

        // shared_guard will be destroyed at end of block, releasing its reference
    }

    // guard and shared_guard released, reference count should be 0
    EXPECT_EQ(manager.get_reference_count(handle), 0);
    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

// Test: RAII guard - keep shared slot separate
TEST_F(Shared_memory_slot_manager_test, raii_guard_keep_shared_slot_separate) {
    Slot_handle handle;
    auto shared_guard_opt = manager.allocate_slot();
    ASSERT_TRUE(shared_guard_opt);
    auto shared_guard = std::move(*shared_guard_opt);

    {
        auto guard_opt = manager.allocate_slot();
        ASSERT_TRUE(guard_opt);
        auto& guard = *guard_opt;
        ASSERT_TRUE(guard.has_slot());
        auto h_result = guard.get_handle();
        ASSERT_TRUE(h_result);
        handle = *h_result;

        // share() returns a new guard managing a reference to the same slot
        shared_guard = guard.share();
        EXPECT_TRUE(shared_guard.has_slot());
        auto sh_result = shared_guard.get_handle();
        ASSERT_TRUE(sh_result);
        EXPECT_EQ(*sh_result, handle);
        EXPECT_EQ(manager.get_reference_count(handle), 2);
    }

    // Original guard destroyed, but shared_guard keeps the reference alive
    EXPECT_EQ(manager.get_reference_count(handle), 1);
    EXPECT_EQ(manager.get_allocated_slot_count(), 1);

    // Release the shared guard
    shared_guard.reset();
    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

// Test: RAII guard - allocate and automatic release
TEST_F(Shared_memory_slot_manager_test, raii_guard_with_pre_allocated_slot) {
    Slot_handle handle;
    {
        auto guard_opt = manager.allocate_slot();
        ASSERT_TRUE(guard_opt);
        auto& guard = *guard_opt;
        EXPECT_TRUE(guard.has_slot());
        auto h_result = guard.get_handle();
        ASSERT_TRUE(h_result);
        handle = *h_result;
        EXPECT_EQ(manager.get_allocated_slot_count(), 1);
    }

    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
    EXPECT_EQ(manager.get_reference_count(handle), 0);
}

// Test: RAII guard - move constructor
TEST_F(Shared_memory_slot_manager_test, raii_guard_move_constructor) {
    auto guard1_opt = manager.allocate_slot();
    ASSERT_TRUE(guard1_opt);
    auto guard1 = std::move(*guard1_opt);
    ASSERT_TRUE(guard1.has_slot());
    auto h_result = guard1.get_handle();
    ASSERT_TRUE(h_result);
    auto handle = *h_result;

    Shared_memory_slot_guard guard2(std::move(guard1));

    EXPECT_FALSE(guard1.has_slot());
    EXPECT_TRUE(guard2.has_slot());
    auto h2_result = guard2.get_handle();
    ASSERT_TRUE(h2_result);
    EXPECT_EQ(*h2_result, handle);
    EXPECT_EQ(manager.get_allocated_slot_count(), 1);

    guard2.reset();
    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

// Test: RAII guard - move assignment
TEST_F(Shared_memory_slot_manager_test, raii_guard_move_assignment) {
    auto guard1_opt = manager.allocate_slot();
    ASSERT_TRUE(guard1_opt);
    auto guard1 = std::move(*guard1_opt);
    ASSERT_TRUE(guard1.has_slot());
    auto h1_result = guard1.get_handle();
    ASSERT_TRUE(h1_result);
    auto handle1 = *h1_result;

    auto guard2_opt = manager.allocate_slot();
    ASSERT_TRUE(guard2_opt);
    auto guard2 = std::move(*guard2_opt);
    ASSERT_TRUE(guard2.has_slot());

    EXPECT_EQ(manager.get_allocated_slot_count(), 2);

    // Move assignment - guard2's slot should be released, guard1's slot transferred
    guard2 = std::move(guard1);

    EXPECT_FALSE(guard1.has_slot());
    EXPECT_TRUE(guard2.has_slot());
    auto h2_result = guard2.get_handle();
    ASSERT_TRUE(h2_result);
    EXPECT_EQ(*h2_result, handle1);
    EXPECT_EQ(manager.get_allocated_slot_count(), 1);
}

// Test: RAII guard - early release
TEST_F(Shared_memory_slot_manager_test, raii_guard_early_release) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto guard = std::move(*guard_opt);
    ASSERT_TRUE(guard.has_slot());

    EXPECT_EQ(manager.get_allocated_slot_count(), 1);

    guard.reset();
    EXPECT_FALSE(guard.has_slot());
    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

// Test: Multiple slots with different offsets
TEST_F(Shared_memory_slot_manager_test, multiple_slots_with_different_offsets) {
    std::size_t const slot_size = shared_memory_metadata_other_size.slot_size;
    auto manager_result = memory_manager_factory->create(interface, instance_other_size);
    ASSERT_TRUE(manager_result);
    auto& manager = **manager_result;

    auto guard0_opt = manager.allocate_slot();
    auto guard1_opt = manager.allocate_slot();
    auto guard2_opt = manager.allocate_slot();

    ASSERT_TRUE(guard0_opt);
    ASSERT_TRUE(guard1_opt);
    ASSERT_TRUE(guard2_opt);

    EXPECT_EQ(guard0_opt->get_memory().size(), slot_size);
    EXPECT_EQ(guard1_opt->get_memory().size(), slot_size);
    EXPECT_EQ(guard2_opt->get_memory().size(), slot_size);

    auto h0_result = guard0_opt->get_handle();
    ASSERT_TRUE(h0_result);
    auto rel0_result = manager.release_slot(*h0_result);
    EXPECT_TRUE(rel0_result);

    auto h1_result = guard1_opt->get_handle();
    ASSERT_TRUE(h1_result);
    auto rel1_result = manager.release_slot(*h1_result);
    EXPECT_TRUE(rel1_result);

    auto h2_result = guard2_opt->get_handle();
    ASSERT_TRUE(h2_result);
    auto rel2_result = manager.release_slot(*h2_result);
    EXPECT_TRUE(rel2_result);
}

// Test: Release guard - returns handle and stops managing slot
TEST_F(Shared_memory_slot_manager_test, release_guard_returns_handle) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto& guard = *guard_opt;

    auto h_result = guard.get_handle();
    ASSERT_TRUE(h_result);
    auto handle = *h_result;
    EXPECT_EQ(manager.get_reference_count(handle), 1);

    // Release should return the handle
    auto released_result = guard.release();
    ASSERT_TRUE(released_result);
    auto released_handle = *released_result;
    EXPECT_EQ(released_handle, handle);

    // Guard should no longer hold a valid slot
    EXPECT_FALSE(guard.has_slot());
    auto h_check_result = guard.get_handle();
    EXPECT_FALSE(h_check_result);

    // Slot should still be allocated in the manager (not released)
    EXPECT_EQ(manager.get_reference_count(handle), 1);

    // Caller must now manually manage the slot
    auto rel_result = manager.release_slot(handle);
    EXPECT_TRUE(rel_result);
    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

// Test: Release guard with no slot returns invalid handle
TEST_F(Shared_memory_slot_manager_test, release_guard_no_slot_returns_invalid_handle) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto& guard = *guard_opt;

    // First release
    auto released_result = guard.release();
    ASSERT_TRUE(released_result);
    EXPECT_FALSE(guard.has_slot());

    // Second release should return error
    auto released_result2 = guard.release();
    EXPECT_FALSE(released_result2);
}

// Test: Release transfers ownership - guard destruction doesn't release slot
TEST_F(Shared_memory_slot_manager_test, release_transfers_ownership_guard_destruction) {
    Slot_handle released_handle;
    {
        auto guard_opt = manager.allocate_slot();
        ASSERT_TRUE(guard_opt);
        auto& guard = *guard_opt;
        auto h_result = guard.get_handle();
        ASSERT_TRUE(h_result);
        auto handle = *h_result;

        auto released_result = guard.release();
        ASSERT_TRUE(released_result);
        released_handle = *released_result;
        EXPECT_EQ(released_handle, handle);

        // Guard no longer manages the slot
        EXPECT_FALSE(guard.has_slot());

        // Guard is destroyed here
    }

    // Slot should NOT be freed (guard didn't release it)
    EXPECT_EQ(manager.get_reference_count(released_handle), 1);
    EXPECT_EQ(manager.get_allocated_slot_count(), 1);

    // Caller must manually release
    auto rel_result = manager.release_slot(released_handle);
    EXPECT_TRUE(rel_result);
    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

// Test: Release followed by reset on invalid guard
TEST_F(Shared_memory_slot_manager_test, release_then_reset_on_invalid_guard) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto& guard = *guard_opt;
    auto h_result = guard.get_handle();
    ASSERT_TRUE(h_result);
    auto handle = *h_result;

    // Release the slot from guard
    auto released_result = guard.release();
    ASSERT_TRUE(released_result);
    auto released_handle = *released_result;
    EXPECT_EQ(released_handle, handle);

    // Calling reset on invalid guard should be safe
    guard.reset();

    // Slot is still allocated (not released by guard or reset)
    EXPECT_EQ(manager.get_reference_count(handle), 1);

    // Manually release
    auto rel_result = manager.release_slot(handle);
    EXPECT_TRUE(rel_result);
}

// Test: Release with multi-consumer slot
TEST_F(Shared_memory_slot_manager_test, release_with_multi_consumer_slot) {
    auto guard_opt = manager.allocate_slot();
    ASSERT_TRUE(guard_opt);
    auto& guard = *guard_opt;
    auto h_result = guard.get_handle();
    ASSERT_TRUE(h_result);
    auto handle = *h_result;

    // Add another consumer
    auto add_result = manager.add_consumer(handle);
    EXPECT_TRUE(add_result);
    EXPECT_EQ(manager.get_reference_count(handle), 2);

    // Release transfers ownership without releasing to manager
    auto released_result = guard.release();
    ASSERT_TRUE(released_result);
    auto released_handle = *released_result;
    EXPECT_EQ(released_handle, handle);

    // Reference count unchanged (guard didn't release)
    EXPECT_EQ(manager.get_reference_count(handle), 2);

    // Caller must manage the releases
    auto rel_result1 = manager.release_slot(handle);
    EXPECT_TRUE(rel_result1);
    EXPECT_EQ(manager.get_reference_count(handle), 1);

    auto rel_result2 = manager.release_slot(handle);
    EXPECT_TRUE(rel_result2);
    EXPECT_EQ(manager.get_allocated_slot_count(), 0);
}

}  // namespace score::gateway_ipc_binding
