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

using Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test =
    Gateway_ipc_binding_bidirectional_test<Gateway_ipc_binding_integration_test>;

INSTANTIATE_TEST_SUITE_P(, Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test,
                         Values(Direction::Client_to_server, Direction::Server_to_client),
                         readable_test_names);

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test,
       ipc_server_destruction_with_server_connector) {
    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);

    // kill the IPC server
    this->server.reset();
    EXPECT_TRUE(
        wait_on_connection_state(*this->client, Connection_state::Disconnected, very_long_timeout));
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test,
       ipc_server_destruction_with_client_connector) {
    Client_connector_with_callbacks client;
    client.create_connector(get_client_runtime(), socom_server_config, instance);

    // kill the IPC server
    this->server.reset();
    EXPECT_TRUE(
        wait_on_connection_state(*this->client, Connection_state::Disconnected, very_long_timeout));
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test,
       ipc_client_destruction_with_server_connector) {
    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);

    // kill the IPC client
    this->client.reset();
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test,
       ipc_client_destruction_with_client_connector) {
    Client_connector_with_callbacks client;
    client.create_connector(get_client_runtime(), socom_server_config, instance);

    // kill the IPC client
    this->client.reset();
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test,
       ipc_server_destruction_with_connected_service) {
    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);
    Client_connector_with_callbacks client(get_client_runtime(), socom_server_config, instance);

    // kill the IPC server
    this->server.reset();
    EXPECT_TRUE(
        wait_on_connection_state(*this->client, Connection_state::Disconnected, very_long_timeout));
    EXPECT_EQ(client.client_disconnected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test,
       ipc_client_destruction_with_connected_service) {
    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);
    Client_connector_with_callbacks client(get_client_runtime(), socom_server_config, instance);

    // kill the IPC client
    this->client.reset();

    EXPECT_EQ(client.client_disconnected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test,
       ipc_server_destruction_and_reconnect) {
    // kill the IPC server
    this->server.reset();
    EXPECT_TRUE(
        wait_on_connection_state(*this->client, Connection_state::Disconnected, very_long_timeout));

    // restart the IPC server
    this->server = create_ipc_server(*runtime_server);
    start_and_wait_for_client_connection();
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test,
       ipc_client_destruction_and_reconnect) {
    // kill the IPC client
    this->client.reset();

    // restart the IPC client
    this->client =
        create_ipc_client(*runtime_client, client_shm_config, {}, server_shared_memory_configs);

    EXPECT_TRUE(
        wait_on_connection_state(*this->client, Connection_state::Connected, very_long_timeout));
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test,
       ipc_server_destruction_with_connected_service_and_reconnect) {
    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);
    Client_connector_with_callbacks client(get_client_runtime(), socom_server_config, instance);

    // kill the IPC server
    this->server.reset();
    EXPECT_TRUE(
        wait_on_connection_state(*this->client, Connection_state::Disconnected, very_long_timeout));
    EXPECT_EQ(client.client_disconnected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    // restart the IPC server
    client.expect_client_connected(socom_server_config);
    this->server = create_ipc_server(*runtime_server);
    start_and_wait_for_client_connection();

    EXPECT_EQ(client.client_connected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_bidirectional_sync_state_connected_integration_test,
       ipc_client_destruction_with_connected_service_and_reconnect) {
    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);
    Client_connector_with_callbacks client(get_client_runtime(), socom_server_config, instance);

    // kill the IPC client
    this->client.reset();
    EXPECT_EQ(client.client_disconnected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    // restart the IPC client
    client.expect_client_connected(socom_server_config);
    this->client =
        create_ipc_client(*runtime_client, client_shm_config, {}, server_shared_memory_configs);
    EXPECT_EQ(client.client_connected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

}  // namespace score::gateway_ipc_binding
