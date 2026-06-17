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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstddef>
#include <future>
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_client.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <score/socom/client_connector.hpp>
#include <score/socom/server_connector.hpp>

#include "test_constants.hpp"
#include "test_fixtures.hpp"
#include "util.hpp"

using testing::_;

namespace score::gateway_ipc_binding {

class Gateway_ipc_binding_different_interface_ids_integration_test
    : public Gateway_ipc_binding_unconnected_integration_test {
   protected:
    socom::Service_interface_identifier const client_service_interface{
        "com.test.service.client", socom::Literal_tag{}, {1, 0}};
    socom::Service_interface_identifier const server_service_interface{
        "com.test.service.server", socom::Literal_tag{}, {1, 0}};

    socom::Server_service_interface_definition const client_service_config{
        client_service_interface, score::socom::to_num_of_methods(0),
        score::socom::to_num_of_events(1)};
    socom::Server_service_interface_definition const server_service_config{
        server_service_interface, score::socom::to_num_of_methods(0),
        score::socom::to_num_of_events(1)};

    Shared_memory_metadata const client_side_client_metadata =
        make_metadata("/gw_client_side_client_service_shm", 256, 8);
    Shared_memory_metadata const client_side_server_metadata =
        make_metadata("/gw_client_side_server_service_shm", 1, 1);
    Shared_memory_metadata const server_side_client_metadata =
        make_metadata("/gw_server_side_client_service_shm", 1, 1);
    Shared_memory_metadata const server_side_server_metadata =
        make_metadata("/gw_server_side_server_service_shm", 512, 4);

    Shared_memory_manager_factory::Shared_memory_configuration const client_shm_config{
        {client_service_interface, {{instance, client_side_client_metadata}}},
        {server_service_interface,
         {{instance, client_side_server_metadata}}},  // TODO should not be needed
    };

    Shared_memory_manager_factory::Shared_memory_configuration const server_shm_config{
        {client_service_interface,
         {{instance, server_side_client_metadata}}},  // TODO should not be needed
        {server_service_interface, {{instance, server_side_server_metadata}}}};

    Gateway_ipc_binding_different_interface_ids_integration_test() {
        client.reset();
        server.reset();
        client = create_ipc_client(*runtime_client, client_shm_config, {},
                                   make_shared_memory_configs(server_shm_config));
        server = create_ipc_server(*runtime_server);
        start_and_wait_for_client_connection();
    }
};

using Gateway_ipc_binding_different_interface_ids_test =
    Gateway_ipc_binding_different_interface_ids_integration_test;

TEST_F(Gateway_ipc_binding_different_interface_ids_test,
       cross_service_event_updates_are_delivered) {
    Server_connector_with_callbacks client_side_server_connector{*runtime_client,
                                                                 client_service_config, instance};
    Server_connector_with_callbacks server_side_server_connector{*runtime_server,
                                                                 server_service_config, instance};
    Client_connector_with_callbacks client_side_client_connector{*runtime_client,
                                                                 server_service_config, instance};
    Client_connector_with_callbacks server_side_client_connector{*runtime_server,
                                                                 client_service_config, instance};

    client_side_client_connector.subscribe_event(
        server_side_server_connector.mock_event_subscription_change_cb, event_id);
    server_side_client_connector.subscribe_event(
        client_side_server_connector.mock_event_subscription_change_cb, event_id);

    auto send_and_expect_update = [this](Server_connector_with_callbacks& server_connector,
                                         Client_connector_with_callbacks& client_connector,
                                         std::byte payload_marker) {
        std::promise<socom::Payload> event_update_received_promise;
        EXPECT_CALL(client_connector.mock_event_update_cb, Call(_, event_id, _))
            .Times(1)
            .WillOnce([&event_update_received_promise](auto&, auto, auto payload) {
                event_update_received_promise.set_value(std::move(payload));
            });

        auto payload_handle =
            create_payload(*server_connector.connector, event_id, expected_payload);
        payload_handle.wdata()[0] = payload_marker;

        auto const update_result =
            server_connector.connector->update_event(event_id, std::move(payload_handle));
        ASSERT_TRUE(update_result);

        auto payload_future = event_update_received_promise.get_future();
        ASSERT_EQ(payload_future.wait_for(very_long_timeout), std::future_status::ready);

        auto received_payload = payload_future.get();
        ASSERT_GE(received_payload.data().size(), expected_payload.size());
        EXPECT_EQ(received_payload.data()[0], payload_marker);
        EXPECT_EQ(received_payload.data()[1], expected_payload[1]);
        EXPECT_EQ(received_payload.data()[2], expected_payload[2]);
        EXPECT_EQ(received_payload.data()[3], expected_payload[3]);
    };

    send_and_expect_update(server_side_server_connector, client_side_client_connector,
                           std::byte{0x11});
    send_and_expect_update(client_side_server_connector, server_side_client_connector,
                           std::byte{0x22});
}

}  // namespace score::gateway_ipc_binding
