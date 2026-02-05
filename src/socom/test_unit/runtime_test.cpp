/********************************************************************************
 * Copyright (c) 2025 Contributors to the Eclipse Foundation
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

#include <score/socom/runtime.hpp>

TEST(runtime_test, test_bridge_identity_can_be_created_and_compared) {
    int const a = 42;
    int const b = 7;

    auto const under_test_1 = score::socom::Bridge_identity::make(a);
    auto const under_test_2 = score::socom::Bridge_identity::make(b);

    EXPECT_TRUE(under_test_1 == under_test_1);
    EXPECT_TRUE(under_test_1 != under_test_2);
}
