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

#include "gtest/gtest.h"
#include "score/socom/service_interface.hpp"
#include "score/socom/service_interface_configuration.hpp"

using namespace ::testing;

namespace socom = ::score::socom;

using socom::Num_of_events;
using socom::Num_of_methods;
using socom::Server_service_interface_configuration;
using socom::Service_instance;
using socom::Service_interface;
using socom::Service_interface_configuration;
using socom::to_num_of_events;
using socom::to_num_of_methods;

namespace {

class ServiceInterfaceConfigurationTest : public Test {
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

constexpr std::string_view ServiceInterfaceConfigurationTest::service_interface_id;
constexpr std::string_view ServiceInterfaceConfigurationTest::service_interface_id_2;

TEST_F(ServiceInterfaceConfigurationTest, ConfigurationEqual) {
    ASSERT_TRUE(interface_config_1 == interface_config_1);
    ASSERT_FALSE(interface_config_1 == interface_config_2);
    ASSERT_FALSE(interface_config_3 == interface_config_1);
    ASSERT_FALSE(interface_config_4 == interface_config_1);
}

TEST(ServiceInterfaceConfigurationTestInvalid, Invalid) {
    Service_interface_configuration const expected_config{
        Service_interface{std::string_view{""}, Service_interface::Version{0U, 0U}},
        Num_of_methods{}, Num_of_events{}};

    auto const interface_config_invalid = Server_service_interface_configuration::invalid();

    ASSERT_TRUE(interface_config_invalid == expected_config);
}

}  // namespace
