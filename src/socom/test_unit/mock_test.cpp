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

#include <score/socom/callback_mocks.hpp>
#include <score/socom/client_connector_mock.hpp>
#include <score/socom/payload_mock.hpp>
#include <score/socom/runtime_mock.hpp>
#include <score/socom/server_connector_mock.hpp>

#include <gtest/gtest.h>

TEST(mock_test, runtime_mock)
{
    score::socom::Runtime_mock runtime;
}

TEST(mock_test, payload_mock)
{
    score::socom::Payload_mock payload;
}

TEST(mock_test, client_connector_mock)
{
    score::socom::Client_connector_mock client_connector;
}

TEST(mock_test, server_connector_mock)
{
    score::socom::Server_connector_mock server_connector;
}
