/********************************************************************************
 * Copyright (c) 2026 Contributors to the Eclipse Foundation
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

#include <future>

#include "test_constants.hpp"
#include "test_fixtures.hpp"

using testing::Values;
using namespace std::chrono_literals;

namespace score::gateway_ipc_binding {

using Gateway_ipc_binding_bidirectional_sync_state_at_connect_integration_test =
    Gateway_ipc_binding_bidirectional_test<Gateway_ipc_binding_unconnected_integration_test>;

INSTANTIATE_TEST_SUITE_P(, Gateway_ipc_binding_bidirectional_sync_state_at_connect_integration_test,
                         Values(Direction::Client_to_server, Direction::Server_to_client),
                         readable_test_names);

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_at_connect_integration_test,
       client_creates_client_connector_and_connects_to_server) {
    Client_connector_with_callbacks client;
    client.create_connector(get_client_runtime(), socom_server_config, instance);

    // start IPC server and IPC communication
    this->start_and_wait_for_client_connection();

    client.expect_client_connected(socom_server_config);
    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);

    EXPECT_EQ(client.client_connected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    server.connector.reset();
    EXPECT_EQ(client.client_disconnected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_at_connect_integration_test,
       client_creates_server_connector_and_connects_to_server) {
    Server_connector_with_callbacks server(get_client_runtime(), socom_server_config, instance);

    // start IPC server and IPC communication
    this->start_and_wait_for_client_connection();

    Client_connector_with_callbacks client(get_client_runtime(), socom_server_config, instance);

    server.connector.reset();
    EXPECT_EQ(client.client_disconnected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_at_connect_integration_test,
       ipc_server_destruction_with_server_connector) {
    this->start_and_wait_for_client_connection();

    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);

    // kill the IPC server
    this->server.reset();
    EXPECT_FALSE(this->client->is_connected());
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_at_connect_integration_test,
       ipc_server_destruction_with_client_connector) {
    this->start_and_wait_for_client_connection();

    Client_connector_with_callbacks client;
    client.create_connector(get_client_runtime(), socom_server_config, instance);

    // kill the IPC server
    this->server.reset();
    EXPECT_FALSE(this->client->is_connected());
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_at_connect_integration_test,
       ipc_client_destruction_with_server_connector) {
    this->start_and_wait_for_client_connection();

    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);

    // kill the IPC client
    this->client.reset();
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_at_connect_integration_test,
       ipc_client_destruction_with_client_connector) {
    this->start_and_wait_for_client_connection();

    Client_connector_with_callbacks client;
    client.create_connector(get_client_runtime(), socom_server_config, instance);

    // kill the IPC client
    this->client.reset();
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_at_connect_integration_test,
       ipc_server_destruction_with_connected_service) {
    this->start_and_wait_for_client_connection();

    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);
    Client_connector_with_callbacks client(get_client_runtime(), socom_server_config, instance);

    // kill and restart the IPC server
    this->server.reset();
    EXPECT_FALSE(this->client->is_connected());
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_at_connect_integration_test,
       ipc_client_destruction_with_connected_service) {
    this->start_and_wait_for_client_connection();

    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);
    Client_connector_with_callbacks client(get_client_runtime(), socom_server_config, instance);

    // kill and restart the IPC client
    this->client.reset();
}

}  // namespace score::gateway_ipc_binding
