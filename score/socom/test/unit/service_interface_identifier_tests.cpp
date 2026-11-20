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
#include "score/socom/service_interface_definition.hpp"
#include "score/socom/service_interface_identifier.hpp"

using namespace ::testing;

namespace score::socom {

class ServiceInterfaceIdentifierTest : public Test {
   protected:
    static constexpr std::string_view service_interface_id{"service1"};
    static constexpr std::string_view service_interface_id_2{"service2"};
    Service_interface_identifier::Version const service_interface_version{1, 0};
    Service_interface_identifier const service_interface{service_interface_id,
                                                         service_interface_version};
    Service_interface_identifier const service_interface_2{service_interface_id_2,
                                                           service_interface_version};
    std::size_t num_methods{1U};
    std::size_t num_methods_2{2U};
    std::size_t num_events{2U};
    std::size_t num_events_2{3U};

    Service_interface_definition const interface_config_1{
        service_interface, to_num_of_methods(num_methods), to_num_of_events(num_events)};
    Service_interface_definition const interface_config_2{
        service_interface_2, to_num_of_methods(num_methods), to_num_of_events(num_events)};
    Service_interface_definition const interface_config_3{
        service_interface, to_num_of_methods(num_methods_2), to_num_of_events(num_events)};
    Service_interface_definition const interface_config_4{
        service_interface, to_num_of_methods(num_methods), to_num_of_events(num_events_2)};
};

using ServiceInterfaceConfigurationDeathTest = ServiceInterfaceIdentifierTest;
using score::utils::string_registry::LiteralTag;
using score::utils::string_registry::StringRegistry;

TEST_F(ServiceInterfaceIdentifierTest, ConfigurationEqual) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "equivalence-classes");
    ASSERT_TRUE(interface_config_1 == interface_config_1);
    ASSERT_FALSE(interface_config_1 == interface_config_2);
}

TEST_F(ServiceInterfaceIdentifierTest, LiteratorConstructorUsesStringView) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "design-analysis");
    StringRegistry registry;
    auto const registry_string_view = registry.Insert(service_interface_id).first;

    auto const interface =
        Service_interface_identifier{registry_string_view, service_interface_version};
    EXPECT_EQ(interface.id, registry_string_view);
    EXPECT_EQ(interface.id.Data(), registry_string_view.Data());
}

TEST_F(ServiceInterfaceIdentifierTest, StringConstructorUsesStringView) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "design-analysis");
    auto const interface =
        Service_interface_identifier{std::string(service_interface_id), service_interface_version};
    EXPECT_EQ(interface.id.StringView(), service_interface_id);

    auto const registry_string_view = service_id_registry().Insert(service_interface_id).first;
    EXPECT_EQ(interface.id, registry_string_view);
    EXPECT_EQ(interface.id.Data(), registry_string_view.Data());
}

TEST_F(ServiceInterfaceIdentifierTest, StringViewConstructorUsesStringView) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "design-analysis");
    auto const interface =
        Service_interface_identifier{service_interface_id, service_interface_version};
    EXPECT_EQ(interface.id.StringView(), service_interface_id);

    auto const registry_string_view = service_id_registry().Insert(service_interface_id).first;
    EXPECT_EQ(interface.id, registry_string_view);
    EXPECT_EQ(interface.id.Data(), registry_string_view.Data());
}

TEST_F(ServiceInterfaceIdentifierTest, StringViewLiteralConstructorUsesStringView) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "design-analysis");
    auto const interface =
        Service_interface_identifier{service_interface_id, LiteralTag{}, service_interface_version};
    EXPECT_EQ(interface.id.StringView(), service_interface_id);

    auto const registry_string_view = service_id_registry().Insert(service_interface_id).first;
    EXPECT_EQ(interface.id, registry_string_view);
    EXPECT_EQ(interface.id.Data(), registry_string_view.Data());
}

TEST_F(ServiceInterfaceConfigurationDeathTest, SameInterfaceDifferentNumMethodsAsserts) {
    RecordProperty("TestType", "fault-injection");
    RecordProperty("DerivationTechnique", "error-guessing");
    EXPECT_DEATH((void)(interface_config_1 == interface_config_3), "");
}

TEST_F(ServiceInterfaceConfigurationDeathTest, SameInterfaceDifferentNumEventsAsserts) {
    RecordProperty("TestType", "fault-injection");
    RecordProperty("DerivationTechnique", "error-guessing");
    EXPECT_DEATH((void)(interface_config_1 == interface_config_4), "");
}

class ServiceInstanceTest : public Test {
   protected:
    static constexpr std::string_view id_string{"idd"};
};

constexpr std::string_view ServiceInstanceTest::id_string;

TEST_F(ServiceInstanceTest, LiteratorConstructorUsesStringView) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "design-analysis");
    StringRegistry registry;
    auto const registry_string_view = registry.Insert(id_string).first;

    auto const instance = Service_instance{registry_string_view};
    EXPECT_EQ(instance.id, registry_string_view);
    EXPECT_EQ(instance.id.Data(), registry_string_view.Data());
}

TEST_F(ServiceInstanceTest, StringConstructorAddsStringToRegistry) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "design-analysis");
    auto const instance = Service_instance{std::string(id_string)};
    EXPECT_EQ(instance.id.StringView(), id_string);

    auto const registry_string_view = instance_id_registry().Insert(id_string).first;
    EXPECT_EQ(instance.id, registry_string_view);
    EXPECT_EQ(instance.id.Data(), registry_string_view.Data());
}

TEST_F(ServiceInstanceTest, StringViewConstructorAddsStringToRegistry) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "design-analysis");
    auto const instance = Service_instance{id_string};
    EXPECT_EQ(instance.id.StringView(), id_string);

    auto const registry_string_view = instance_id_registry().Insert(id_string).first;
    EXPECT_EQ(instance.id, registry_string_view);
    EXPECT_EQ(instance.id.Data(), registry_string_view.Data());
}

TEST_F(ServiceInstanceTest, StringViewLiteralConstructorAddsStringToRegistry) {
    RecordProperty("TestType", "interface-test");
    RecordProperty("DerivationTechnique", "design-analysis");
    auto const instance = Service_instance{id_string, LiteralTag{}};
    EXPECT_EQ(instance.id.StringView(), id_string);

    auto const registry_string_view = instance_id_registry().Insert(id_string, LiteralTag{}).first;
    EXPECT_EQ(instance.id, registry_string_view);
    EXPECT_EQ(instance.id.Data(), registry_string_view.Data());
}

}  // namespace score::socom
