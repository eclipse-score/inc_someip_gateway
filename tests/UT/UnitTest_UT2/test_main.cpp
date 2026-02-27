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

// Function to be tested
int diff(int a, int b) { return a - b; }


TEST(DiffTest, HandlesNegativeNumbers) {
    EXPECT_EQ(diff(-1, -2), 1);
    EXPECT_EQ(diff(-10, -20), 10);
    //test1
}
// Main function for running tests
int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
//test123
//sddd
