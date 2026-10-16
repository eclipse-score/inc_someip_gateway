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

#include "event_transmission_benchmark_context.hpp"

#include <gtest/gtest.h>

namespace score::gateway_ipc_binding {
namespace {

TEST(Event_transmission_benchmark_context_test, transmits_events_for_each_payload_size) {
    for (std::size_t const payload_size : {64U, 256U, 1024U, 1024U * 1024U}) {
        Event_transmission_benchmark_context context{payload_size};
        for (std::size_t iteration = 0U; iteration < 10U; ++iteration) {
            auto const duration = context.send_and_measure_once();
            ASSERT_TRUE(duration.has_value()) << "Payload size: " << payload_size;
            EXPECT_GE(duration.value().count(), 0);
        }
    }
}

}  // namespace
}  // namespace score::gateway_ipc_binding

int main(int argc, char** argv) {
    score::gateway_ipc_binding::initialize_benchmark_runtime();
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
