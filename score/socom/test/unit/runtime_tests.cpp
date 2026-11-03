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

#include <set>
#include <vector>

#include "score/socom/connector_factory.hpp"
#include "score/socom/runtime.hpp"
#include "score/socom/single_connection_test_fixture.hpp"
#include "score/socom/socom_mocks.hpp"
#include "score/socom/utilities.hpp"

using namespace std::chrono_literals;

namespace score::socom {

TEST(RuntimeFactoryTest, DefaultConstructorWorks) {
    Runtime::Uptr const rt = create_runtime();
    EXPECT_NE(nullptr, rt);
}

class RuntimeTest : public SingleConnectionTest {
   protected:
    std::vector<Service_instance> const input_find_result{
        Service_instance{"first instance", Literal_tag{}},
        Service_instance{"second instance", Literal_tag{}}, connector_factory.get_instance()};
    Service_instance const expected_find_result{connector_factory.get_instance()};
};

class RuntimeLegacySubscribeFindServiceTest : public RuntimeTest {};

// NOLINTNEXTLINE(readability-redundant-string-init)
MATCHER_P(Multi_set_equal, expected, "") {
    std::multiset<Service_instance> const expected_set{expected.cbegin(), expected.cend()};
    std::multiset<Service_instance> const input_set{arg.cbegin(), arg.cend()};
    return (input_set == expected_set);
}

TEST_F(RuntimeTest, ConstructDuplicateReturnsDuplicateServiceError) {
    Server_connector_callbacks_mock callbacks;
    auto const scd = connector_factory.create_server_connector_with_result(callbacks);
    ASSERT_TRUE(scd.has_value());

    Server_connector_callbacks_mock callbacks_2;
    auto const scd_2 = connector_factory.create_server_connector_with_result(callbacks_2);
    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    EXPECT_EQ(duplicate_service, scd_2);
}

TEST_F(RuntimeTest, ConstructDuplicateMultipleTimesReturnsDuplicateServiceError) {
    Server_connector_callbacks_mock callbacks;
    auto const scd = connector_factory.create_server_connector_with_result(callbacks);
    ASSERT_TRUE(scd.has_value());

    Server_connector_callbacks_mock callbacks_2;
    auto const scd_2 = connector_factory.create_server_connector_with_result(callbacks_2);

    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    ASSERT_EQ(duplicate_service, scd_2);

    Server_connector_callbacks_mock callbacks_3;
    auto const scd_3 = connector_factory.create_server_connector_with_result(callbacks_3);

    EXPECT_EQ(duplicate_service, scd_3);
}

TEST_F(RuntimeTest, CreatingServerConnectorDeletingItAndRecreatingReturnsValidServerConnector) {
    Server_connector_callbacks_mock callbacks;
    {
        auto const scd = connector_factory.create_server_connector_with_result(callbacks);
        ASSERT_TRUE(scd.has_value());
        Server_connector_callbacks_mock callbacks_2;
        score::Result<Disabled_server_connector::Uptr> const duplicate_service =
            score::MakeUnexpected(Construction_error::duplicate_service);
        EXPECT_EQ(duplicate_service,
                  connector_factory.create_server_connector_with_result(callbacks_2));
    }

    {
        auto const scd = connector_factory.create_server_connector_with_result(callbacks);
        ASSERT_TRUE(scd.has_value());
    }
}

TEST_F(RuntimeTest, ConnectorDestroyedAfterRuntimeDoesNotCrash) {
    Disabled_server_connector::Uptr connector{nullptr};
    {
        Connector_factory factory{connector_factory};
        Server_connector_callbacks_mock callbacks;
        connector = factory.create_server_connector(callbacks);
    }
}

}  // namespace score::socom
