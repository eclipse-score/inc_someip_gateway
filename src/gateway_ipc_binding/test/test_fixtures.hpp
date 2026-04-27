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

#ifndef SRC_GATEWAY_IPC_BINDING_TEST_TEST_FIXTURES
#define SRC_GATEWAY_IPC_BINDING_TEST_TEST_FIXTURES

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <score/message_passing/unix_domain/unix_domain_client_factory.h>
#include <score/message_passing/unix_domain/unix_domain_server_factory.h>
#include <score/result/result.h>

#include <cstddef>
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_client.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <score/socom/callback_mocks.hpp>
#include <score/socom/client_connector.hpp>
#include <score/socom/client_connector_mock.hpp>
#include <score/socom/error.hpp>
#include <score/socom/runtime.hpp>
#include <score/socom/runtime_mock.hpp>
#include <score/socom/server_connector.hpp>
#include <score/socom/server_connector_mock.hpp>
#include <string_view>
#include <thread>
#include <utility>

#include "mocks.hpp"
#include "test_constants.hpp"

namespace score::gateway_ipc_binding {

class Gateway_ipc_binding_unconnected_integration_test : public ::testing::Test,
                                                         protected Test_constants {
   protected:
    socom::Runtime::Uptr runtime_server = score::socom::create_runtime();
    socom::Runtime::Uptr runtime_client = score::socom::create_runtime();

    On_find_service_change_mock mock_on_find_service_change_cb;

    std::unique_ptr<Gateway_ipc_binding_server> server =
        create_ipc_server(*runtime_server, server_shm_config);
    std::unique_ptr<Gateway_ipc_binding_client> client =
        create_ipc_client(*runtime_client, client_shm_config);

    ~Gateway_ipc_binding_unconnected_integration_test() {
        client.reset();
        server.reset();
    }

    std::unique_ptr<Gateway_ipc_binding_server> create_ipc_server(
        socom::Runtime& runtime,
        Shared_memory_manager_factory::Shared_memory_configuration shm_config) {
        score::message_passing::UnixDomainServerFactory server_factory;
        auto ipc_server = server_factory.Create(protocol_config, server_config);

        // Create gateway IPC binding server with pre-created IPC server
        auto server = Gateway_ipc_binding_server::create(
            runtime, std::move(ipc_server), Shared_memory_manager_factory::create(shm_config),
            mock_on_find_service_change_cb.as_function());

        assert(server && "Server creation failed");
        return server;
    }

    std::unique_ptr<Gateway_ipc_binding_client> create_ipc_client(
        socom::Runtime& runtime,
        Shared_memory_manager_factory::Shared_memory_configuration shm_config,
        Find_service_elements find_service_elements = {}, std::string_view identifier = {}) {
        score::message_passing::UnixDomainClientFactory client_factory;
        auto connection = client_factory.Create(protocol_config, client_config);
        auto client = Gateway_ipc_binding_client::create(
            runtime, std::move(connection), Shared_memory_manager_factory::create(shm_config),
            std::move(find_service_elements), identifier);

        assert(client && "Client creation failed");
        return client;
    }

    void start_and_wait_for_client_connection() {
        // Start the server
        auto start_result = server->start();
        assert(start_result);

        // Wait for the client to connect and receive the reply
        while (!client->is_connected()) {
            std::this_thread::sleep_for(1ms);
        }
    }
};

class Gateway_ipc_binding_integration_test
    : public Gateway_ipc_binding_unconnected_integration_test {
   protected:
    socom::Service_state_change_callback_mock mock_service_state_change_cb;
    socom::Event_update_callback_mock mock_event_update_cb;
    socom::Event_payload_allocate_callback_mock mock_event_payload_allocate_cb;

    socom::Method_call_credentials_callback_mock mock_method_call_credentials_cb;
    socom::Event_subscription_change_callback_mock mock_event_subscription_change_cb;
    socom::Event_request_update_callback_mock mock_event_request_update_cb;
    socom::Method_call_payload_allocate_callback_mock mock_method_payload_allocate_cb;

    Gateway_ipc_binding_integration_test() : Gateway_ipc_binding_unconnected_integration_test() {
        start_and_wait_for_client_connection();
    }
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_TEST_TEST_FIXTURES
