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

#ifndef SRC_SOCOM_TEST_UNIT2_FRAMEWORK_INC_SINGLE_CONNECTION_TEST_FIXTURE
#define SRC_SOCOM_TEST_UNIT2_FRAMEWORK_INC_SINGLE_CONNECTION_TEST_FIXTURE

#include "connector_factory.hpp"
#include "gtest/gtest.h"
#include "score/socom/client_connector.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/service_interface_configuration.hpp"
#include "score/socom/vector_payload.hpp"
#include "socom_mocks.hpp"

namespace ac {

/// \brief Small payload
::score::socom::Payload::Sptr const& input_data();

/// \brief Small payload
::score::socom::Payload::Sptr const& error_data();

/// \brief SingleConnectionTest provides some constants which are almost always
///        in each test
class SingleConnectionTest : public ::testing::Test {
   public:
    ::score::socom::Service_interface const service_interface{::score::socom::Service_interface{
        "TestInterface1", ::score::socom::Service_interface::Version{1U, 2U}}};
    ::score::socom::Service_instance const service_instance{"TestInterface1"};
    std::size_t num_methods{2U};
    std::size_t num_events{3U};
    Connector_factory connector_factory{
        service_interface, ::score::socom::to_num_of_methods(num_methods),
        ::score::socom::to_num_of_events(num_events), service_instance};
    static ::score::socom::Method_id const method_id{0x01};
    ::score::socom::Method_id const min_method_id{0};
    ::score::socom::Method_id const max_method_id{
        static_cast<::score::socom::Method_id>(connector_factory.get_num_methods() - 1)};
    static ::score::socom::Event_id const event_id{0x02};
    ::score::socom::Event_id const min_event_id{0};
    ::score::socom::Event_id const max_event_id{
        static_cast<::score::socom::Event_id>(connector_factory.get_num_events() - 1)};
    ::score::socom::Payload::Sptr const real_payload =
        ::score::socom::make_vector_payload(::score::socom::make_vector_buffer(1U, 2U, 3U, 4U));
};

}  // namespace ac

#endif  // SRC_SOCOM_TEST_UNIT2_FRAMEWORK_INC_SINGLE_CONNECTION_TEST_FIXTURE
