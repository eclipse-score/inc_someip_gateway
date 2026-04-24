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
#include <score/message_passing/unix_domain/unix_domain_client_factory.h>
#include <score/message_passing/unix_domain/unix_domain_server_factory.h>
#include <score/result/result.h>

#include <cstddef>
#include <future>
#include <score/gateway_ipc_binding/error.hpp>
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
#include <thread>

#include "test_constants.hpp"
#include "test_fixtures.hpp"
#include "util.hpp"

using testing::_;
using testing::AtMost;
using testing::Values;
using namespace std::chrono_literals;

namespace score::gateway_ipc_binding {

TEST_F(Gateway_ipc_binding_unconnected_integration_test, connect) {
    // This test verifies that a client can connect to the server and receive a reply.

    // Start the server
    auto start_result = server->start();
    ASSERT_TRUE(start_result);

    // Wait for the client to connect and receive the reply
    while (!client->is_connected()) {
        std::this_thread::sleep_for(1ms);
    }
}

class Gateway_ipc_binding_bidirectional_integration_test
    : public Gateway_ipc_binding_integration_test,
      public ::testing::WithParamInterface<Direction> {
   protected:
    socom::Runtime& get_client_runtime() {
        return GetParam() == Direction::Client_to_server ? *runtime_client : *runtime_server;
    }
    socom::Runtime& get_server_runtime() {
        return GetParam() == Direction::Client_to_server ? *runtime_server : *runtime_client;
    }

    Shared_memory_metadata const& get_server_metadata() {
        return GetParam() == Direction::Client_to_server ? server_metadata : client_metadata;
    }
};

INSTANTIATE_TEST_SUITE_P(, Gateway_ipc_binding_bidirectional_integration_test,
                         Values(Direction::Client_to_server, Direction::Server_to_client));

TEST_P(Gateway_ipc_binding_bidirectional_integration_test,
       client_connects_to_server_with_client_connector) {
    Client_connector_with_callbacks client;
    client.create_connector(get_client_runtime(), socom_server_config, instance);
    client.expect_client_connected(socom_server_config);
    Server_connector_with_callbacks server(get_server_runtime(), socom_server_config, instance);

    EXPECT_EQ(client.client_connected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    server.connector.reset();
    EXPECT_EQ(client.client_disconnected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

class Gateway_ipc_binding_connected_bidirectional_integration_test
    : public Gateway_ipc_binding_bidirectional_integration_test {
   protected:
    Server_connector_with_callbacks server{get_server_runtime(), socom_server_config, instance};
    Client_connector_with_callbacks client{get_client_runtime(), socom_server_config, instance};
};

INSTANTIATE_TEST_SUITE_P(, Gateway_ipc_binding_connected_bidirectional_integration_test,
                         Values(Direction::Client_to_server, Direction::Server_to_client));

TEST_P(Gateway_ipc_binding_connected_bidirectional_integration_test, client_subscribes_to_event) {
    std::promise<void> event_subscription_change_promise;
    EXPECT_CALL(server.mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::subscribed))
        .WillOnce([&event_subscription_change_promise](auto&, auto, auto) {
            event_subscription_change_promise.set_value();
        });

    auto const subscribe_result =
        client.connector->subscribe_event(event_id, score::socom::Event_mode::update);
    ASSERT_TRUE(subscribe_result);

    EXPECT_EQ(event_subscription_change_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    std::promise<void> event_unsubscription_change_promise;
    EXPECT_CALL(server.mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::unsubscribed))
        .WillOnce([&event_unsubscription_change_promise](auto&, auto, auto) {
            event_unsubscription_change_promise.set_value();
        });

    auto const unsubscribe_result = client.connector->unsubscribe_event(event_id);
    ASSERT_TRUE(unsubscribe_result);

    EXPECT_EQ(event_unsubscription_change_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_connected_bidirectional_integration_test,
       server_allocates_event_payload) {
    client.subscribe_event(server.mock_event_subscription_change_cb, event_id);

    auto payload_handle = server.connector->allocate_event_payload(event_id);
    ASSERT_TRUE(payload_handle);
    ASSERT_NE(*payload_handle, nullptr);
    EXPECT_EQ((*payload_handle)->data().size(), get_server_metadata().slot_size);
    EXPECT_NE((*payload_handle)->data().data(), nullptr);

    // Test that the allocated slot is released when the payload is destroyed
    payload_handle->reset();
}

TEST_P(Gateway_ipc_binding_connected_bidirectional_integration_test,
       event_payload_allocation_without_any_subscription_fails) {
    auto payload_handle = server.connector->allocate_event_payload(event_id);
    EXPECT_EQ(payload_handle,
              MakeUnexpected(
                  socom::Server_connector_error::runtime_error_no_client_subscribed_for_event));
}

TEST_P(Gateway_ipc_binding_connected_bidirectional_integration_test,
       exhausted_event_payload_slots_return_error) {
    client.subscribe_event(server.mock_event_subscription_change_cb, event_id);

    std::vector<socom::Writable_payload::Uptr> payload_handles;

    for (std::size_t i = 0; i < get_server_metadata().slot_count; ++i) {
        auto payload_handle = server.connector->allocate_event_payload(event_id);
        ASSERT_TRUE(payload_handle);
        ASSERT_NE(*payload_handle, nullptr);
        payload_handles.push_back(std::move(*payload_handle));
    }

    auto payload_handle = server.connector->allocate_event_payload(event_id);
    EXPECT_EQ(
        payload_handle,
        MakeUnexpected(
            gateway_ipc_binding::Shared_memory_manager_error::runtime_error_no_available_slots));

    // destruction of payload handle should enable to allocate again
    payload_handles.erase(std::begin(payload_handles));
    EXPECT_TRUE(server.connector->allocate_event_payload(event_id));
}

TEST_P(Gateway_ipc_binding_connected_bidirectional_integration_test, server_sends_event_update) {
    client.subscribe_event(server.mock_event_subscription_change_cb, event_id);

    auto payload_handle = create_payload(*server.connector, event_id, expected_payload);

    std::promise<socom::Payload::Uptr> event_update_received_promise;
    EXPECT_CALL(client.mock_event_update_cb, Call(_, event_id, _))
        .Times(1)
        .WillOnce([&event_update_received_promise](auto&, auto, auto payload) {
            event_update_received_promise.set_value(std::move(payload));
        });
    auto update_result = server.connector->update_event(event_id, std::move(payload_handle));
    ASSERT_TRUE(update_result);

    auto payload_future = event_update_received_promise.get_future();
    ASSERT_EQ(payload_future.wait_for(very_long_timeout), std::future_status::ready);

    auto received_payload = payload_future.get();
    ASSERT_TRUE(received_payload);
    EXPECT_EQ(received_payload->data().size(), get_server_metadata().slot_size);
    EXPECT_EQ(received_payload->data()[0], expected_payload[0]);
    EXPECT_EQ(received_payload->data()[1], expected_payload[1]);
    EXPECT_EQ(received_payload->data()[2], expected_payload[2]);
    EXPECT_EQ(received_payload->data()[3], expected_payload[3]);
}

TEST_P(Gateway_ipc_binding_connected_bidirectional_integration_test,
       client_keeps_event_payloads_until_server_receives_consumed_notification) {
    client.subscribe_event(server.mock_event_subscription_change_cb, event_id);

    std::vector<socom::Payload::Uptr> payload_handles;

    for (std::size_t i = 0; i < get_server_metadata().slot_count; ++i) {
        auto payload_handle = create_payload(*server.connector, event_id, expected_payload);
        ASSERT_NE(payload_handle, nullptr);
        payload_handle->wdata()[0] =
            std::byte{static_cast<std::uint8_t>(i)};  // differentiate payloads

        std::promise<socom::Payload::Uptr> event_update_received_promise;
        EXPECT_CALL(client.mock_event_update_cb, Call(_, event_id, _))
            .Times(1)
            .WillOnce([&event_update_received_promise](auto&, auto, auto payload) {
                event_update_received_promise.set_value(std::move(payload));
            });
        auto update_result = server.connector->update_event(event_id, std::move(payload_handle));
        ASSERT_TRUE(update_result);

        auto payload_future = event_update_received_promise.get_future();
        ASSERT_EQ(payload_future.wait_for(very_long_timeout), std::future_status::ready);

        auto received_payload = payload_future.get();
        ASSERT_TRUE(received_payload);
        EXPECT_EQ(received_payload->data().size(), get_server_metadata().slot_size);
        EXPECT_EQ(received_payload->data()[0], std::byte{static_cast<std::uint8_t>(i)});

        payload_handles.push_back(std::move(received_payload));
    }

    auto payload_handle = server.connector->allocate_event_payload(event_id);
    EXPECT_EQ(
        payload_handle,
        MakeUnexpected(
            gateway_ipc_binding::Shared_memory_manager_error::runtime_error_no_available_slots));

    // destruction of payload handle should enable to allocate again
    payload_handles.erase(std::begin(payload_handles));
    // need to wait for IPC
    while (
        server.connector->allocate_event_payload(event_id) ==
        MakeUnexpected(
            gateway_ipc_binding::Shared_memory_manager_error::runtime_error_no_available_slots)) {
        std::this_thread::sleep_for(10ms);
    }
}

TEST_P(Gateway_ipc_binding_connected_bidirectional_integration_test, client_disconnects) {
    // misuse Event subscription notification to detect client disconnect
    client.subscribe_event(server.mock_event_subscription_change_cb, event_id);

    std::promise<void> event_unsubscription_change_promise;
    EXPECT_CALL(server.mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::unsubscribed))
        .WillOnce([&event_unsubscription_change_promise](auto&, auto, auto) {
            event_unsubscription_change_promise.set_value();
        });

    client.connector.reset();

    EXPECT_EQ(event_unsubscription_change_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_connected_bidirectional_integration_test,
       client_disconnects_and_reconnects) {
    // misuse Event subscription notification to detect client disconnect
    client.subscribe_event(server.mock_event_subscription_change_cb, event_id);

    std::promise<void> event_unsubscription_change_promise;
    EXPECT_CALL(server.mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::unsubscribed))
        .WillOnce([&event_unsubscription_change_promise](auto&, auto, auto) {
            event_unsubscription_change_promise.set_value();
        });

    client.connector.reset();

    EXPECT_EQ(event_unsubscription_change_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    // now reconnect to service
    std::promise<void> client_connected_promise;
    EXPECT_CALL(client.mock_service_state_change_cb,
                Call(_, score::socom::Service_state::available, socom_server_config))
        .Times(1)
        .WillOnce([&client_connected_promise](auto&, auto, auto) {
            client_connected_promise.set_value();
        });

    EXPECT_CALL(client.mock_service_state_change_cb,
                Call(_, score::socom::Service_state::not_available, socom_server_config))
        // Callback call is racy with IPC message exchange at test teardown
        .Times(AtMost(1));

    client.create_connector(get_client_runtime(), socom_server_config, instance);

    EXPECT_EQ(client_connected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_connected_bidirectional_integration_test, server_disconnects) {
    server.connector.reset();
    EXPECT_EQ(client.client_disconnected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_connected_bidirectional_integration_test,
       server_disconnects_and_reconnects) {
    server.connector.reset();
    EXPECT_EQ(client.client_disconnected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    std::promise<void> client_connected_promise;
    EXPECT_CALL(client.mock_service_state_change_cb,
                Call(_, score::socom::Service_state::available, socom_server_config))
        .Times(1)
        .WillOnce([&client_connected_promise](auto&, auto, auto) {
            client_connected_promise.set_value();
        });

    server.create_connector(get_server_runtime(), socom_server_config, instance);

    EXPECT_EQ(client_connected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    EXPECT_CALL(client.mock_service_state_change_cb,
                Call(_, score::socom::Service_state::not_available, socom_server_config))
        // Callback call is racy with IPC message exchange at test teardown
        .Times(AtMost(1));
}

}  // namespace score::gateway_ipc_binding
