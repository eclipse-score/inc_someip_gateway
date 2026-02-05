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

#include <score/socom/service_interface.hpp>

using ::testing::Test;

namespace socom = ::score::socom;
using socom::Service_interface;

namespace {

TEST(Service_interface_test, construct_with_string_view_id) {
    // Create the test data
    std::string_view const service_interface_id{"test123"};
    constexpr std::uint16_t version_major = 1;
    constexpr std::uint16_t version_minor = 0;
    Service_interface::Version const version{version_major, version_minor};

    // Construct the Service_interface with the id as string_view
    Service_interface const si{service_interface_id, version};

    // verify
    ASSERT_EQ(si.id, service_interface_id);
    ASSERT_EQ(si.version.major, version_major);
    ASSERT_EQ(si.version.minor, version_minor);
}

}  // namespace
