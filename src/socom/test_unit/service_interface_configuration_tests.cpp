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

#include <score/socom/service_interface.hpp>
#include <score/socom/service_interface_configuration.hpp>
#include <string_view>

using ::testing::Test;

namespace socom = ::score::socom;

using socom::Service_interface;
using socom::Service_interface_configuration;
using socom::to_num_of_events;
using socom::to_num_of_methods;

namespace {

class Service_interface_configuration_test : public Test {
   protected:
    static constexpr std::string_view service_interface_id{"service1"};
    static constexpr std::string_view service_interface_id_2{"service2"};
    Service_interface::Version const service_interface_version{1, 0};
    Service_interface const service_interface{service_interface_id, service_interface_version};
    Service_interface const service_interface_2{service_interface_id_2, service_interface_version};
    std::size_t num_methods{1U};
    std::size_t num_methods_2{2U};
    std::size_t num_events{2U};
    std::size_t num_events_2{3U};

    Service_interface_configuration const interface_config_1{
        service_interface, to_num_of_methods(num_methods), to_num_of_events(num_events)};
    Service_interface_configuration const interface_config_2{
        service_interface_2, to_num_of_methods(num_methods), to_num_of_events(num_events)};
    Service_interface_configuration const interface_config_3{
        service_interface, to_num_of_methods(num_methods_2), to_num_of_events(num_events)};
    Service_interface_configuration const interface_config_4{
        service_interface, to_num_of_methods(num_methods), to_num_of_events(num_events_2)};
};

constexpr std::string_view Service_interface_configuration_test::service_interface_id;
constexpr std::string_view Service_interface_configuration_test::service_interface_id_2;

TEST_F(Service_interface_configuration_test, ConfigurationEqual) {
    /// Verify that true is returned when configs are equal otherwise false
    ASSERT_TRUE(interface_config_1 == interface_config_1);
    ASSERT_FALSE(interface_config_1 == interface_config_2);
    ASSERT_FALSE(interface_config_3 == interface_config_1);
    ASSERT_FALSE(interface_config_4 == interface_config_1);
}

}  // namespace
