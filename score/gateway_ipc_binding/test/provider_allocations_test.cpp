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

#include <atomic>
#include <limits>
#include <optional>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/gateway_ipc_binding/impl/mw_com/provider_service_binding.hpp"
#include "score/socom/runtime.hpp"

namespace score::gateway_ipc_binding::mw_com {

class Provider_allocations_test : public ::testing::Test {
   protected:
    using Allocations = Provider_service_binding::Allocations;

    static void set_available(Provider_service_binding& binding, bool const available) {
        binding.on_service_state_change(available ? socom::Service_state::available
                                                  : socom::Service_state::not_available);
    }

    static Result<socom::Writable_payload> allocate(Provider_service_binding& binding,
                                                    socom::Event_id const event_id) {
        return binding.on_event_payload_allocate(event_id);
    }

    static void send(Provider_service_binding& binding, socom::Payload payload) {
        binding.on_event_update(0U, std::move(payload));
    }

    static std::optional<score::mw::com::SampleAllocateePtr<void>> take(
        Provider_service_binding& binding, std::size_t const handle) {
        return binding.m_allocations->take(handle);
    }

    static Service_config provider_config(std::vector<Event_config> events = {{"event_a", 128U}},
                                          std::size_t const max_sample_count = 4U) {
        return Service_config{socom::Service_interface_identifier{
                                  std::string_view{"bridged_/test/ipc/BridgedService"},
                                  socom::Service_interface_identifier::Version{1U, 0U}},
                              socom::Service_instance{std::string_view{"1"}},
                              "bridged_ipc/bridged",
                              Role::provider,
                              std::move(events),
                              max_sample_count};
    }
};

TEST_F(Provider_allocations_test, empty_pool_rejects_samples_and_handles) {
    Allocations allocations;
    allocations.initialize(0U);
    EXPECT_FALSE(allocations.insert({}).has_value());
    EXPECT_FALSE(allocations.take(0U).has_value());
    allocations.clear();
}

TEST_F(Provider_allocations_test, exhaustion_and_reuse_preserve_other_samples) {
    Allocations allocations;
    allocations.initialize(2U);
    auto const first = allocations.insert({});
    auto const second = allocations.insert({});
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(first.value(), second.value());
    EXPECT_FALSE(allocations.insert({}).has_value());
    EXPECT_FALSE(allocations.take(std::numeric_limits<std::size_t>::max()).has_value());

    EXPECT_TRUE(allocations.take(first.value()).has_value());
    auto const replacement = allocations.insert({});
    ASSERT_TRUE(replacement.has_value());
    EXPECT_NE(replacement.value(), first.value());
    EXPECT_FALSE(allocations.take(first.value()).has_value());
    EXPECT_FALSE(allocations.insert({}).has_value());
    EXPECT_TRUE(allocations.take(second.value()).has_value());
    EXPECT_TRUE(allocations.take(replacement.value()).has_value());
}

TEST_F(Provider_allocations_test, clear_and_reinitialize_do_not_revive_old_handles) {
    Allocations allocations;
    allocations.initialize(2U);
    auto const first = allocations.insert({});
    auto const second = allocations.insert({});
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    allocations.clear();
    allocations.clear();
    allocations.initialize(2U);

    auto const replacement_first = allocations.insert({});
    auto const replacement_second = allocations.insert({});
    ASSERT_TRUE(replacement_first.has_value());
    ASSERT_TRUE(replacement_second.has_value());
    EXPECT_FALSE(allocations.take(first.value()).has_value());
    EXPECT_FALSE(allocations.take(second.value()).has_value());
    EXPECT_FALSE(allocations.insert({}).has_value());
    EXPECT_TRUE(allocations.take(replacement_first.value()).has_value());
    EXPECT_TRUE(allocations.take(replacement_second.value()).has_value());
}

TEST_F(Provider_allocations_test, repeatedly_recycles_one_entry) {
    Allocations allocations;
    allocations.initialize(1U);
    std::optional<std::size_t> previous;
    for (std::size_t iteration = 0U; iteration < 10000U; ++iteration) {
        auto const handle = allocations.insert({});
        ASSERT_TRUE(handle.has_value());
        if (previous.has_value()) {
            EXPECT_FALSE(allocations.take(previous.value()).has_value());
        }
        EXPECT_TRUE(allocations.take(handle.value()).has_value());
        previous = handle;
    }
}

TEST_F(Provider_allocations_test, concurrently_recycles_entries) {
    Allocations allocations;
    allocations.initialize(4U);
    std::atomic<bool> succeeded{true};
    auto const recycle = [&]() {
        for (std::size_t iteration = 0U; iteration < 1000U; ++iteration) {
            auto const handle = allocations.insert({});
            if (!handle.has_value() || !allocations.take(handle.value()).has_value() ||
                allocations.take(handle.value()).has_value()) {
                succeeded = false;
                return;
            }
        }
    };
    std::thread first{recycle};
    std::thread second{recycle};
    std::thread third{recycle};
    std::thread fourth{recycle};
    first.join();
    second.join();
    third.join();
    fourth.join();
    EXPECT_TRUE(succeeded.load());
}

TEST_F(Provider_allocations_test, capacity_uses_all_provider_event_slots_not_subscription_budget) {
    auto runtime = socom::create_runtime();
    auto config = provider_config({{"event_a", 128U}, {"event_b", 128U}}, 1U);
    auto binding = Provider_service_binding::create(*runtime, config);
    ASSERT_TRUE(binding.has_value());
    set_available(*binding.value(), true);

    std::vector<socom::Writable_payload> payloads;
    for (socom::Event_id const event_id : {0U, 1U}) {
        for (std::size_t slot = 0U; slot < 4U; ++slot) {
            auto payload = allocate(*binding.value(), event_id);
            ASSERT_TRUE(payload.has_value()) << "Event " << event_id << ", slot " << slot;
            payloads.push_back(std::move(payload).value());
        }
        EXPECT_EQ(allocate(*binding.value(), event_id),
                  MakeUnexpected(Shared_memory_manager_error::runtime_error_no_available_slots));
    }
    payloads.clear();
    EXPECT_TRUE(allocate(*binding.value(), 0U).has_value());
    EXPECT_TRUE(allocate(*binding.value(), 1U).has_value());
}

TEST_F(Provider_allocations_test, late_payload_destruction_does_not_release_reused_entry) {
    auto runtime = socom::create_runtime();
    auto config = provider_config();
    auto binding = Provider_service_binding::create(*runtime, config);
    ASSERT_TRUE(binding.has_value());
    set_available(*binding.value(), true);
    auto old_payload = allocate(*binding.value(), 0U);
    ASSERT_TRUE(old_payload.has_value());
    auto const old_handle = old_payload->get_slot_handle();
    send(*binding.value(), socom::Payload{old_payload->data(), old_handle, {}});

    auto replacement = allocate(*binding.value(), 0U);
    ASSERT_TRUE(replacement.has_value());
    auto const replacement_handle = replacement->get_slot_handle();
    EXPECT_NE(old_handle, replacement_handle);
    old_payload = MakeUnexpected(Shared_memory_manager_error::runtime_error_no_available_slots);
    auto sample = take(*binding.value(), replacement_handle);
    EXPECT_TRUE(sample.has_value());
}

TEST_F(Provider_allocations_test,
       old_payload_is_safe_after_skeleton_recreation_and_binding_destruction) {
    auto runtime = socom::create_runtime();
    auto config = provider_config();
    auto binding = Provider_service_binding::create(*runtime, config);
    ASSERT_TRUE(binding.has_value());
    set_available(*binding.value(), true);
    auto old_payload = allocate(*binding.value(), 0U);
    ASSERT_TRUE(old_payload.has_value());
    auto const old_handle = old_payload->get_slot_handle();
    set_available(*binding.value(), false);
    set_available(*binding.value(), true);

    auto replacement = allocate(*binding.value(), 0U);
    ASSERT_TRUE(replacement.has_value());
    auto const replacement_handle = replacement->get_slot_handle();
    EXPECT_NE(old_handle, replacement_handle);
    old_payload = MakeUnexpected(Shared_memory_manager_error::runtime_error_no_available_slots);
    auto sample = take(*binding.value(), replacement_handle);
    EXPECT_TRUE(sample.has_value());
    sample.reset();

    auto unsent = allocate(*binding.value(), 0U);
    ASSERT_TRUE(unsent.has_value());
    binding.value().reset();
    unsent = MakeUnexpected(Shared_memory_manager_error::runtime_error_no_available_slots);
}

}  // namespace score::gateway_ipc_binding::mw_com
