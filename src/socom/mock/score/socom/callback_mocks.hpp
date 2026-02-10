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

#ifndef SCORE_SOCOM_CALLBACK_MOCKS_HPP
#define SCORE_SOCOM_CALLBACK_MOCKS_HPP

#include <gmock/gmock.h>

#include <score/socom/client_connector.hpp>
#include <score/socom/runtime.hpp>
#include <score/socom/server_connector.hpp>

namespace score::socom {

// Runtime callbacks
using Find_result_change_callback_mock = ::testing::MockFunction<Find_result_change_callback>;

// Client_connector callbacks
using Service_state_change_callback_mock = ::testing::MockFunction<Service_state_change_callback>;
using Event_update_callback_mock = ::testing::MockFunction<Event_update_callback>;
using Event_payload_allocate_callback_mock =
    ::testing::MockFunction<Event_payload_allocate_callback>;

// Server_connector callbacks
using Event_subscription_change_callback_mock =
    ::testing::MockFunction<Event_subscription_change_callback>;
using Event_request_update_callback_mock = ::testing::MockFunction<Event_request_update_callback>;
using Method_call_credentials_callback_mock =
    ::testing::MockFunction<Method_call_credentials_callback>;
using Method_payload_allocate_callback_mock =
    ::testing::MockFunction<Method_payload_allocate_callback>;

// Method callbacks
using Method_call_credentials_callback_mock =
    ::testing::MockFunction<Method_call_credentials_callback>;
using Method_reply_callback_mock = ::testing::MockFunction<Method_reply_callback>;

// Bridge callbacks
using Subscribe_find_service_function_mock =
    ::testing::MockFunction<Subscribe_find_service_function>;
using Request_service_function_mock = ::testing::MockFunction<Request_service_function>;

}  // namespace score::socom

#endif  // SCORE_SOCOM_CLIENT_CONNECTOR_MOCK_HPP
