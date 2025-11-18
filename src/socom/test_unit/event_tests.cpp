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

#include <score/socom/event.hpp>

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

using ::score::socom::Event_id;
using ::score::socom::Event_mode;
using ::score::socom::Event_state;


namespace {

TEST(Event_test, data_types)
{
    EXPECT_TRUE((std::is_same_v<Event_id, std::uint16_t>));

    EXPECT_EQ(std::is_enum_v<Event_mode>, true);
    EXPECT_EQ(static_cast<uint8_t>(Event_mode::update), 0x00U);
    EXPECT_EQ(static_cast<uint8_t>(Event_mode::update_and_initial_value), 0x01U);

    EXPECT_EQ(std::is_enum_v<Event_state>, true);
    EXPECT_EQ(static_cast<uint8_t>(Event_state::unsubscribed), 0x00U);
    EXPECT_EQ(static_cast<uint8_t>(Event_state::subscribed), 0x01U);
}

} // namespace
