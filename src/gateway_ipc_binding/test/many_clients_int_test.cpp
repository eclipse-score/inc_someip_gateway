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
#include <score/socom/vector_payload.hpp>
#include <string>
#include <thread>
#include <vector>

#include "test_constants.hpp"
#include "test_fixtures.hpp"
#include "util.hpp"

using testing::_;
using testing::Values;
using namespace std::chrono_literals;

namespace score::gateway_ipc_binding {

class Gateway_ipc_binding_many_clients_integration_test
    : public Gateway_ipc_binding_integration_test,
      public ::testing::WithParamInterface<std::size_t> {
   protected:
    std::size_t const num_clients = GetParam();

    std::vector<std::unique_ptr<socom::Runtime>> client_runtimes;
    std::vector<std::unique_ptr<Gateway_ipc_binding_client>> clients;

    void SetUp() override {
        Gateway_ipc_binding_integration_test::SetUp();

        for (std::size_t i = 0; i < num_clients; ++i) {
            Shared_memory_metadata const client_metadata_1 =
                make_metadata("/gw_client_shm_many_clients_tests_" + std::to_string(i), 256, 8);
            Shared_memory_manager_factory::Shared_memory_configuration const client_shm_config_1{
                {interface, {{instance, client_metadata_1}}}};

            auto client_runtime = socom::create_runtime();
            clients.push_back(create_ipc_client(*client_runtime, client_shm_config_1));
            client_runtimes.push_back(std::move(client_runtime));
        }

        for (auto& client : clients) {
            while (!client->is_connected()) {
                std::this_thread::sleep_for(1ms);
            }
        }
    }

    std::vector<Client_connector_with_callbacks> create_connected_clients() {
        std::vector<Client_connector_with_callbacks> client_connectors(num_clients);

        for (std::size_t i = 0; i < num_clients; ++i) {
            client_connectors.at(i).expect_client_connected(socom_server_config);
            client_connectors.at(i).create_connector(*client_runtimes.at(i), socom_server_config,
                                                     instance);
        }

        for (auto& connector_with_cbs : client_connectors) {
            EXPECT_EQ(connector_with_cbs.client_connected_promise.get_future().wait_for(
                          very_long_timeout),
                      std::future_status::ready);
        }
        return client_connectors;
    }

    void subscribe_events(
        socom::Event_subscription_change_callback_mock& mock_event_subscription_change_cb,
        std::vector<Client_connector_with_callbacks>& client_connectors) {
        // All clients subscribe to the event; but the server only will know that there is at least
        // one subscriber
        std::promise<void> all_subscribed_promise;
        EXPECT_CALL(mock_event_subscription_change_cb,
                    Call(_, event_id, socom::Event_state::subscribed))
            .Times(1)
            .WillOnce([&](auto&, auto, auto) { all_subscribed_promise.set_value(); });

        for (auto const& cc : client_connectors) {
            auto result = cc.connector->subscribe_event(event_id, socom::Event_mode::update);
            ASSERT_TRUE(result);
        }
        ASSERT_EQ(all_subscribed_promise.get_future().wait_for(very_long_timeout),
                  std::future_status::ready);

        EXPECT_CALL(mock_event_subscription_change_cb,
                    Call(_, event_id, socom::Event_state::unsubscribed))
            .Times(AtMost(1));
    }
};

class Gateway_ipc_binding_payload_lifetime_regression_test
    : public Gateway_ipc_binding_unconnected_integration_test {
   protected:
    Shared_memory_metadata const server_one_slot_metadata =
        make_metadata("/gw_server_shm_payload_lifetime", 512, 1);
    Shared_memory_metadata const client1_metadata =
        make_metadata("/gw_client1_shm_payload_lifetime", 256, 8);
    Shared_memory_metadata const client2_metadata =
        make_metadata("/gw_client2_shm_payload_lifetime", 256, 8);

    Shared_memory_manager_factory::Shared_memory_configuration const server_one_slot_shm_config{
        {interface, {{instance, server_one_slot_metadata}}}};
    Shared_memory_manager_factory::Shared_memory_configuration const client1_shm_config{
        {interface, {{instance, client1_metadata}}}};
    Shared_memory_manager_factory::Shared_memory_configuration const client2_shm_config{
        {interface, {{instance, client2_metadata}}}};

    socom::Runtime::Uptr runtime_client2 = score::socom::create_runtime();
    std::unique_ptr<Gateway_ipc_binding_client> client2;

    Gateway_ipc_binding_payload_lifetime_regression_test() {
        server.reset();
        client.reset();

        server = create_ipc_server(*runtime_server, server_one_slot_shm_config);
        client = create_ipc_client(*runtime_client, client1_shm_config);
        client2 = create_ipc_client(*runtime_client2, client2_shm_config);

        start_and_wait_for_all_clients();
    }

    ~Gateway_ipc_binding_payload_lifetime_regression_test() {
        client2.reset();
        client.reset();
        server.reset();
    }

    void start_and_wait_for_all_clients() {
        auto start_result = server->start();
        ASSERT_TRUE(start_result);

        while (!client->is_connected() || !client2->is_connected()) {
            std::this_thread::sleep_for(1ms);
        }
    }
};

INSTANTIATE_TEST_SUITE_P(, Gateway_ipc_binding_many_clients_integration_test,
                         Values(1, 2, 3, 4, 5));

TEST_P(Gateway_ipc_binding_many_clients_integration_test, clients_connect_to_server) {
    Server_connector_with_callbacks server{*runtime_server, socom_server_config, instance};
    std::vector<Client_connector_with_callbacks> client_connectors = create_connected_clients();
    EXPECT_FALSE(testing::Test::HasFailure());
}

TEST_P(Gateway_ipc_binding_many_clients_integration_test, clients_subscribe_to_server_event) {
    Server_connector_with_callbacks server{*runtime_server, socom_server_config, instance};
    auto client_connectors = create_connected_clients();
    subscribe_events(server.mock_event_subscription_change_cb, client_connectors);
    EXPECT_FALSE(testing::Test::HasFailure());
}

TEST_P(Gateway_ipc_binding_many_clients_integration_test,
       server_sends_event_update_to_all_clients) {
    Server_connector_with_callbacks server{*runtime_server, socom_server_config, instance};
    auto client_connectors = create_connected_clients();
    subscribe_events(server.mock_event_subscription_change_cb, client_connectors);

    // Allocate a payload and fill it with test data
    std::vector<std::byte> const expected_payload{std::byte{1}, std::byte{2}, std::byte{3},
                                                  std::byte{4}};
    auto payload_handle = create_payload(*server.connector, event_id, expected_payload);

    // Set up per-client expectations before calling update_event
    std::vector<std::promise<socom::Payload::Sptr>> event_received_promises(num_clients);
    for (std::size_t i = 0; i < num_clients; ++i) {
        EXPECT_CALL(client_connectors[i].mock_event_update_cb, Call(_, event_id, _))
            .Times(1)
            .WillOnce([&promise = event_received_promises[i]](auto&, auto, auto payload) {
                promise.set_value(payload);
            });
    }

    // Server sends the event update – all subscribed clients must receive it
    auto update_result = server.connector->update_event(event_id, std::move(payload_handle));
    ASSERT_TRUE(update_result);

    for (std::size_t i = 0; i < num_clients; ++i) {
        auto future = event_received_promises[i].get_future();
        ASSERT_EQ(future.wait_for(very_long_timeout), std::future_status::ready)
            << "Client " << i << " did not receive the event update";
        auto received_payload = future.get();
        ASSERT_TRUE(received_payload);
        ASSERT_GE(received_payload->data().size(), expected_payload.size());
        EXPECT_EQ(received_payload->data()[0], expected_payload[0]);
        EXPECT_EQ(received_payload->data()[1], expected_payload[1]);
        EXPECT_EQ(received_payload->data()[2], expected_payload[2]);
        EXPECT_EQ(received_payload->data()[3], expected_payload[3]);
    }
}

TEST_P(Gateway_ipc_binding_many_clients_integration_test,
       server_sends_event_update_only_to_subscribed_clients) {
    Server_connector_with_callbacks server{*runtime_server, socom_server_config, instance};
    auto client_connectors = create_connected_clients();

    // Calculate which clients will subscribe (first half) and which won't (second half)
    std::size_t num_subscribed = (num_clients + 1) / 2;  // Round up

    // Only subscribed clients follow-up means the server sees the subscription state changed
    std::promise<void> subscription_change_promise;
    EXPECT_CALL(server.mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::subscribed))
        .Times(1)
        .WillOnce([&](auto&, auto, auto) { subscription_change_promise.set_value(); });
    EXPECT_CALL(server.mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::unsubscribed))
        .Times(AtMost(1));

    // Subscribe only the first half of clients
    for (std::size_t i = 0; i < num_subscribed; ++i) {
        auto result =
            client_connectors[i].connector->subscribe_event(event_id, socom::Event_mode::update);
        ASSERT_TRUE(result);
    }

    ASSERT_EQ(subscription_change_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    // Allocate a payload
    auto payload_handle = server.connector->allocate_event_payload(event_id);
    ASSERT_TRUE(payload_handle);

    // Set up per-client expectations: only subscribed clients should receive the update
    std::vector<std::promise<socom::Payload::Sptr>> event_received_promises(num_subscribed);
    for (std::size_t i = 0; i < num_subscribed; ++i) {
        EXPECT_CALL(client_connectors[i].mock_event_update_cb, Call(_, event_id, _))
            .Times(1)
            .WillOnce([&promise = event_received_promises[i]](auto&, auto, auto payload) {
                promise.set_value(payload);
            });
    }

    // Unsubscribed clients should NOT receive the event
    for (std::size_t i = num_subscribed; i < num_clients; ++i) {
        EXPECT_CALL(client_connectors[i].mock_event_update_cb, Call(_, event_id, _)).Times(0);
    }

    // Server sends the event update
    auto update_result = server.connector->update_event(event_id, std::move(*payload_handle));
    ASSERT_TRUE(update_result);

    // Verify subscribed clients received the event
    for (std::size_t i = 0; i < num_subscribed; ++i) {
        auto future = event_received_promises[i].get_future();
        ASSERT_EQ(future.wait_for(very_long_timeout), std::future_status::ready)
            << "Subscribed client " << i << " did not receive the event update";
        auto received_payload = future.get();
        ASSERT_TRUE(received_payload);
    }
}

TEST_P(Gateway_ipc_binding_many_clients_integration_test,
       all_clients_subscribe_then_some_unsubscribe_before_update) {
    auto constexpr k_wait_timeout = 2s;

    Server_connector_with_callbacks server{*runtime_server, socom_server_config, instance};
    auto client_connectors = create_connected_clients();
    subscribe_events(server.mock_event_subscription_change_cb, client_connectors);

    std::size_t const num_still_subscribed = (num_clients + 1) / 2;

    // Unsubscribe the second half of clients. The first half remains subscribed.
    for (std::size_t i = num_still_subscribed; i < num_clients; ++i) {
        auto unsubscribe_result = client_connectors[i].connector->unsubscribe_event(event_id);
        ASSERT_TRUE(unsubscribe_result);
    }

    auto payload_handle = server.connector->allocate_event_payload(event_id);
    ASSERT_TRUE(payload_handle);

    std::vector<std::promise<socom::Payload::Sptr>> event_received_promises(num_still_subscribed);
    for (std::size_t i = 0; i < num_still_subscribed; ++i) {
        EXPECT_CALL(client_connectors[i].mock_event_update_cb, Call(_, event_id, _))
            .Times(1)
            .WillOnce([&promise = event_received_promises[i]](auto&, auto, auto payload) {
                promise.set_value(payload);
            });
    }

    for (std::size_t i = num_still_subscribed; i < num_clients; ++i) {
        EXPECT_CALL(client_connectors[i].mock_event_update_cb, Call(_, event_id, _)).Times(0);
    }

    auto update_result = server.connector->update_event(event_id, std::move(*payload_handle));
    ASSERT_TRUE(update_result);

    for (std::size_t i = 0; i < num_still_subscribed; ++i) {
        auto future = event_received_promises[i].get_future();
        ASSERT_EQ(future.wait_for(k_wait_timeout), std::future_status::ready)
            << "Subscribed client " << i << " did not receive the event update";
        auto received_payload = future.get();
        ASSERT_TRUE(received_payload);
    }
}

TEST_F(Gateway_ipc_binding_payload_lifetime_regression_test,
       keeps_slot_allocated_until_last_client_releases_payload) {
    auto constexpr k_wait_timeout = 2s;

    Server_connector_with_callbacks server_connector{*runtime_server, socom_server_config,
                                                     instance};
    Client_connector_with_callbacks client_connector_1{*runtime_client, socom_server_config,
                                                       instance};
    Client_connector_with_callbacks client_connector_2{*runtime_client2, socom_server_config,
                                                       instance};

    std::promise<void> subscribed_promise;
    EXPECT_CALL(server_connector.mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::subscribed))
        .Times(1)
        .WillOnce([&subscribed_promise](auto&, auto, auto) { subscribed_promise.set_value(); });
    EXPECT_CALL(server_connector.mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::unsubscribed))
        .Times(AtMost(1));

    ASSERT_TRUE(client_connector_1.connector->subscribe_event(event_id, socom::Event_mode::update));
    ASSERT_TRUE(client_connector_2.connector->subscribe_event(event_id, socom::Event_mode::update));
    ASSERT_EQ(subscribed_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    auto payload_handle = create_payload(*server_connector.connector, event_id, expected_payload);

    std::promise<socom::Payload::Sptr> client1_payload_promise;
    std::promise<socom::Payload::Sptr> client2_payload_promise;

    EXPECT_CALL(client_connector_1.mock_event_update_cb, Call(_, event_id, _))
        .Times(1)
        .WillOnce([&client1_payload_promise](auto&, auto, auto payload) {
            client1_payload_promise.set_value(payload);
        });
    EXPECT_CALL(client_connector_2.mock_event_update_cb, Call(_, event_id, _))
        .Times(1)
        .WillOnce([&client2_payload_promise](auto&, auto, auto payload) {
            client2_payload_promise.set_value(payload);
        });

    ASSERT_TRUE(server_connector.connector->update_event(event_id, std::move(payload_handle)));

    auto client1_payload_future = client1_payload_promise.get_future();
    ASSERT_EQ(client1_payload_future.wait_for(k_wait_timeout), std::future_status::ready);
    auto client1_payload = client1_payload_future.get();
    ASSERT_TRUE(client1_payload);

    auto client2_payload_future = client2_payload_promise.get_future();
    ASSERT_EQ(client2_payload_future.wait_for(k_wait_timeout), std::future_status::ready);
    auto client2_payload = client2_payload_future.get();
    ASSERT_TRUE(client2_payload);

    // First client releases payload, second client still holds it. With one slot configured,
    // allocation must remain blocked until the last consumer releases.
    EXPECT_EQ(server_connector.connector->allocate_event_payload(event_id),
              MakeUnexpected(Shared_memory_manager_error::runtime_error_no_available_slots));
    client1_payload.reset();
    EXPECT_EQ(server_connector.connector->allocate_event_payload(event_id),
              MakeUnexpected(Shared_memory_manager_error::runtime_error_no_available_slots));

    client2_payload.reset();

    auto const deadline = std::chrono::steady_clock::now() + k_wait_timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        auto alloc_after_all_released =
            server_connector.connector->allocate_event_payload(event_id);
        if (alloc_after_all_released) {
            SUCCEED();
            return;
        }
        std::this_thread::sleep_for(1ms);
    }

    FAIL() << "Expected event payload allocation to succeed after last client released payload";
}

}  // namespace score::gateway_ipc_binding
