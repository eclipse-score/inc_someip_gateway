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

#include <gtest/gtest.h>

#include <score/socom/error.hpp>
#include <type_traits>

using ::score::socom::Construction_error;
using ::score::socom::Error;
using ::score::socom::Server_connector_error;

namespace {

TEST(Error_test, data_types) {
    EXPECT_EQ(std::is_enum_v<Error>, true);
    EXPECT_EQ(static_cast<uint8_t>(Error::runtime_error_service_not_available), 0x00U);
    EXPECT_EQ(static_cast<uint8_t>(Error::runtime_error_request_rejected), 0x01U);
    EXPECT_EQ(static_cast<uint8_t>(Error::logic_error_id_out_of_range), 0x02U);
    EXPECT_EQ(static_cast<uint8_t>(Error::runtime_error_malformed_payload), 0x03U);
    EXPECT_EQ(static_cast<uint8_t>(Error::runtime_error_permission_not_allowed), 0x04U);

    EXPECT_EQ(std::is_enum_v<Server_connector_error>, true);
    EXPECT_EQ(static_cast<uint8_t>(Server_connector_error::logic_error_id_out_of_range), 0x00U);

    EXPECT_EQ(std::is_enum_v<Construction_error>, true);
    EXPECT_EQ(static_cast<uint8_t>(Construction_error::duplicate_service), 0x00U);
    EXPECT_EQ(static_cast<uint8_t>(Construction_error::callback_missing), 0x01U);
}

}  // namespace
