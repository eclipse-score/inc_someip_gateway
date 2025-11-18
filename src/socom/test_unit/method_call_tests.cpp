/********************************************************************************
 * Copyright (c) 2025 Elektrobit Automotive GmbH
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

#include "make_vector_buffer.hpp"

#include <score/socom/method.hpp>
#include <score/socom/vector_payload.hpp>

#include <gtest/gtest.h>

using ::score::socom::Application_error;
using ::score::socom::Application_return;
using ::score::socom::Error;
using ::score::socom::make_vector_payload;
using ::score::socom::Method_result;
using ::score::socom::Vector_buffer;
using ::score::socom::test::make_vector_buffer;


namespace {

TEST(Method_type_test, method_result_equality_and_inequality_operators)
{
    // Create Method_result objects
    auto const ar = Method_result{Application_return{}};
    auto const ae = Method_result{Application_error{}};
    auto const me = Method_result{Error::runtime_error_request_rejected};
    auto const ae_code = Method_result{Application_error{10}};
    auto const ae_payload =
        Method_result{Application_error{make_vector_payload(Vector_buffer{10})}};

    // Test for (in)equality
    EXPECT_EQ(ar, ar);
    EXPECT_EQ(ae, ae);
    EXPECT_FALSE(ae != ae);
    EXPECT_EQ(me, me);
    EXPECT_NE(ar, ae);
    EXPECT_NE(ar, me);
    EXPECT_NE(ae, me);
    EXPECT_FALSE(ae == ae_code);
    EXPECT_TRUE(ae != ae_code);
    EXPECT_FALSE(ae == ae_payload);
    EXPECT_TRUE(ae != ae_payload);

    // Set up test payloads for Application_return tests. header = 2 bytes.
    auto const vector_payload1 = make_vector_payload(2, make_vector_buffer(1U, 2U, 3U, 4U, 5U));
    auto const vector_payload1_copy =
        make_vector_payload(2, make_vector_buffer(1U, 2U, 3U, 4U, 5U));
    auto const vector_payload2 = make_vector_payload(2, make_vector_buffer(9U, 8U, 7U, 6U, 5U));
    auto const vector_payload3 = make_vector_payload(2, make_vector_buffer(1U, 2U, 7U, 6U, 5U));
    auto const vector_payload4 = make_vector_payload(2, make_vector_buffer(9U, 8U, 3U, 4U, 5U));
    auto const ar1 = Application_return{vector_payload1};
    auto const ar1_copy = Application_return{vector_payload1_copy};
    auto const ar2 = Application_return{vector_payload1};
    auto const ar3 = Application_return{vector_payload3};
    auto const ar4 = Application_return{vector_payload4};

    // Test for (in)equality
    EXPECT_TRUE(ar1 == ar2);
    EXPECT_FALSE(ar1 == ar3);
    EXPECT_FALSE(ar1 == ar4);
    EXPECT_FALSE(ar1 != ar1_copy);
}

} // namespace
