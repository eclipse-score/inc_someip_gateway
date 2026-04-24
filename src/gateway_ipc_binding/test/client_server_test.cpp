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
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_client.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <score/socom/callback_mocks.hpp>
#include <score/socom/client_connector.hpp>
#include <score/socom/client_connector_mock.hpp>
#include <score/socom/error.hpp>
#include <score/socom/payload_mock.hpp>
#include <score/socom/runtime.hpp>
#include <score/socom/runtime_mock.hpp>
#include <score/socom/server_connector.hpp>
#include <score/socom/server_connector_mock.hpp>
#include <string>
#include <thread>

#include "mocks.hpp"
#include "test_constants.hpp"
#include "util.hpp"

using testing::_;
using testing::AtMost;
using testing::Return;
using testing::Values;
using namespace std::chrono_literals;

namespace score::gateway_ipc_binding {

class Gateway_ipc_binding_unconnected_test : public ::testing::Test, protected Test_constants {
   protected:
    socom::Runtime::Uptr runtime_server = score::socom::create_runtime();
    socom::Runtime::Uptr runtime_client = score::socom::create_runtime();

    Shared_memory_manager_factory_mock* mock_server_shared_memory_manager_factory = nullptr;
    Shared_memory_manager_factory_mock* mock_client_shared_memory_manager_factory = nullptr;

    std::unique_ptr<Gateway_ipc_binding_server> server;
    std::unique_ptr<Gateway_ipc_binding_client> client;

    Gateway_ipc_binding_unconnected_test() {
        if (!runtime_server) {
            throw std::runtime_error("Failed to create server runtime");
        }
        if (!runtime_client) {
            throw std::runtime_error("Failed to create client runtime");
        }

        score::message_passing::UnixDomainServerFactory server_factory;
        auto ipc_server = server_factory.Create(protocol_config, server_config);
        auto mock_server_factory =
            create_mock_unique_ptr(mock_server_shared_memory_manager_factory);

        // Create gateway IPC binding server with pre-created IPC server
        server = Gateway_ipc_binding_server::create(*runtime_server, std::move(ipc_server),
                                                    std::move(mock_server_factory),
                                                    [](auto, auto, bool) {});
        assert(server && "Server creation failed");

        // Create gateway IPC binding client
        score::message_passing::UnixDomainClientFactory client_factory;
        auto connection = client_factory.Create(protocol_config, client_config);
        auto mock_client_factory =
            create_mock_unique_ptr(mock_client_shared_memory_manager_factory);
        client = Gateway_ipc_binding_client::create(*runtime_client, std::move(connection),
                                                    std::move(mock_client_factory));
        assert(client && "Client creation failed");
    }
};

TEST_F(Gateway_ipc_binding_unconnected_test, connect) {
    // This test verifies that a client can connect to the server and receive a reply.

    // Start the server
    auto start_result = server->start();
    ASSERT_TRUE(start_result);

    // Wait for the client to connect and receive the reply
    while (!client->is_connected()) {
        std::this_thread::sleep_for(1ms);
    }
}

class Gateway_ipc_binding_test : public Gateway_ipc_binding_unconnected_test {
   protected:
    socom::Service_state_change_callback_mock mock_service_state_change_cb;
    socom::Event_update_callback_mock mock_event_update_cb;
    socom::Event_payload_allocate_callback_mock mock_event_payload_allocate_cb;

    socom::Method_call_credentials_callback_mock mock_method_call_credentials_cb;
    socom::Event_subscription_change_callback_mock mock_event_subscription_change_cb;
    socom::Event_request_update_callback_mock mock_event_request_update_cb;
    socom::Method_call_payload_allocate_callback_mock mock_method_payload_allocate_cb;

    Shared_memory_slot_manager_mock* mock_client_slot_manager = nullptr;
    Shared_memory_slot_manager_mock* mock_server_slot_manager = nullptr;

    Gateway_ipc_binding_test() : Gateway_ipc_binding_unconnected_test() {
        // Start the server
        auto start_result = server->start();
        assert(start_result);

        // Wait for the client to connect and receive the reply
        while (!client->is_connected()) {
            std::this_thread::sleep_for(1ms);
        }
    }

    ~Gateway_ipc_binding_test() {
        client.reset();
        server.reset();
    }

    score::socom::Client_connector::Callbacks make_client_callbacks() {
        return score::socom::Client_connector::Callbacks{
            mock_service_state_change_cb.as_function(), mock_event_update_cb.as_function(),
            mock_event_update_cb.as_function(), mock_event_payload_allocate_cb.as_function()};
    }

    score::socom::Disabled_server_connector::Callbacks make_server_callbacks() {
        return score::socom::Disabled_server_connector::Callbacks{
            mock_method_call_credentials_cb.as_function(),
            mock_event_subscription_change_cb.as_function(),
            mock_event_request_update_cb.as_function(),
            mock_method_payload_allocate_cb.as_function()};
    }

    std::tuple<socom::Client_connector::Uptr, socom::Enabled_server_connector::Uptr>
    create_connectors(socom::Runtime& client_runtime, socom::Runtime& server_runtime) {
        EXPECT_CALL(*mock_server_shared_memory_manager_factory, create(interface, instance))
            .WillOnce([this](auto, auto) {
                auto mock = create_mock_unique_ptr(mock_server_slot_manager);

                EXPECT_CALL(*mock_server_slot_manager, get_slot_size())
                    .WillRepeatedly(Return(server_metadata.slot_size));
                EXPECT_CALL(*mock_server_slot_manager, get_slot_count())
                    .WillRepeatedly(Return(server_metadata.slot_count));
                EXPECT_CALL(*mock_server_slot_manager, get_path())
                    .WillRepeatedly(Return(fixed_string_to_string(server_metadata.path)));

                return mock;
            });
        EXPECT_CALL(*mock_client_shared_memory_manager_factory, create(interface, instance))
            .WillOnce([this](auto, auto) {
                auto mock = create_mock_unique_ptr(mock_client_slot_manager);

                EXPECT_CALL(*mock_client_slot_manager, get_slot_size())
                    .WillRepeatedly(Return(client_metadata.slot_size));
                EXPECT_CALL(*mock_client_slot_manager, get_slot_count())
                    .WillRepeatedly(Return(client_metadata.slot_count));
                EXPECT_CALL(*mock_client_slot_manager, get_path())
                    .WillRepeatedly(Return(fixed_string_to_string(client_metadata.path)));

                return mock;
            });

        auto client_connector_result = client_runtime.make_client_connector(
            socom_client_config, instance, make_client_callbacks());
        assert(client_connector_result);
        auto client_connector = std::move(client_connector_result.value());
        assert(client_connector);

        auto server_connector_result = server_runtime.make_server_connector(
            socom_server_config, instance, make_server_callbacks());
        assert(server_connector_result);
        auto enabled_server_connector = score::socom::Disabled_server_connector::enable(
            std::move(server_connector_result.value()));
        assert(enabled_server_connector);

        return {std::move(client_connector), std::move(enabled_server_connector)};
    }

    std::tuple<socom::Client_connector::Uptr, socom::Enabled_server_connector::Uptr>
    create_connected_connectors(socom::Runtime& client_runtime, socom::Runtime& server_runtime) {
        std::promise<void> client_connected_promise;

        EXPECT_CALL(mock_service_state_change_cb,
                    Call(_, score::socom::Service_state::available, socom_server_config))
            .Times(1)
            .WillOnce([&client_connected_promise](auto&, auto, auto) {
                client_connected_promise.set_value();
            });

        EXPECT_CALL(mock_service_state_change_cb,
                    Call(_, score::socom::Service_state::not_available, socom_server_config))
            // Callback call is racy with IPC message exchange at test teardown
            .Times(AtMost(1));

        auto connectors = create_connectors(client_runtime, server_runtime);

        EXPECT_EQ(client_connected_promise.get_future().wait_for(very_long_timeout),
                  std::future_status::ready);
        return connectors;
    }

    void subscribe_event(socom::Client_connector const& client_connector) {
        std::promise<void> event_subscription_change_promise;
        EXPECT_CALL(mock_event_subscription_change_cb,
                    Call(_, event_id, socom::Event_state::subscribed))
            .WillOnce([&event_subscription_change_promise](auto&, auto, auto) {
                event_subscription_change_promise.set_value();
            });

        // EXPECT_CALL(mock_event_subscription_change_cb,
        //             Call(_, event_id, socom::Event_state::unsubscribed));

        auto const subscribe_result =
            client_connector.subscribe_event(event_id, score::socom::Event_mode::update);
        ASSERT_TRUE(subscribe_result);

        EXPECT_EQ(event_subscription_change_promise.get_future().wait_for(very_long_timeout),
                  std::future_status::ready);
    }
};

class Gateway_ipc_binding_param_test : public Gateway_ipc_binding_test,
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

    Shared_memory_slot_manager_mock& get_server_slot_manager() {
        return GetParam() == Direction::Client_to_server ? *mock_server_slot_manager
                                                         : *mock_client_slot_manager;
    }

    Shared_memory_manager_factory_mock& get_client_shared_memory_manager_factory() {
        return GetParam() == Direction::Client_to_server
                   ? *mock_client_shared_memory_manager_factory
                   : *mock_server_shared_memory_manager_factory;
    }
};

INSTANTIATE_TEST_SUITE_P(, Gateway_ipc_binding_param_test,
                         Values(Direction::Client_to_server, Direction::Server_to_client));

TEST_P(Gateway_ipc_binding_param_test, client_connects_to_server_with_client_connector) {
    std::promise<void> client_connected_promise;

    EXPECT_CALL(mock_service_state_change_cb,
                Call(_, score::socom::Service_state::available, socom_server_config))
        .Times(1)
        .WillOnce([&client_connected_promise](auto&, auto, auto) {
            client_connected_promise.set_value();
        });

    auto connectors = create_connectors(get_client_runtime(), get_server_runtime());
    EXPECT_EQ(client_connected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    std::promise<void> client_disconnected_promise;
    EXPECT_CALL(mock_service_state_change_cb,
                Call(_, score::socom::Service_state::not_available, socom_server_config))
        .WillOnce([&client_disconnected_promise](auto&, auto, auto) {
            client_disconnected_promise.set_value();
        });

    std::get<1>(connectors).reset();
    EXPECT_EQ(client_disconnected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_param_test, client_subscribes_to_event) {
    auto [client_connector, server_connector] =
        create_connected_connectors(get_client_runtime(), get_server_runtime());

    std::promise<void> event_subscription_change_promise;
    EXPECT_CALL(mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::subscribed))
        .WillOnce([&event_subscription_change_promise](auto&, auto, auto) {
            event_subscription_change_promise.set_value();
        });

    auto const subscribe_result =
        client_connector->subscribe_event(event_id, score::socom::Event_mode::update);
    ASSERT_TRUE(subscribe_result);

    EXPECT_EQ(event_subscription_change_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);

    std::promise<void> event_unsubscription_change_promise;
    EXPECT_CALL(mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::unsubscribed))
        .WillOnce([&event_unsubscription_change_promise](auto&, auto, auto) {
            event_unsubscription_change_promise.set_value();
        });

    auto const unsubscribe_result = client_connector->unsubscribe_event(event_id);
    ASSERT_TRUE(unsubscribe_result);

    EXPECT_EQ(event_unsubscription_change_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_param_test, server_allocates_event_payload) {
    auto [client_connector, server_connector] =
        create_connected_connectors(get_client_runtime(), get_server_runtime());

    subscribe_event(*client_connector);

    std::vector<std::byte> server_memory(get_server_metadata().slot_size, std::byte{0});

    auto slot_guard = get_server_slot_manager().create_slot_guard(Slot_handle(0), server_memory);

    EXPECT_CALL(get_server_slot_manager(), allocate_slot())
        .WillOnce(Return(Result<Shared_memory_slot_guard>(std::move(slot_guard))));

    auto payload_handle = server_connector->allocate_event_payload(event_id);
    ASSERT_TRUE(payload_handle);
    ASSERT_NE(*payload_handle, nullptr);
    EXPECT_EQ((*payload_handle)->data().size(), get_server_metadata().slot_size);
    EXPECT_EQ((*payload_handle)->data().data(), server_memory.data());

    // Test that the allocated slot is released when the payload is destroyed
    EXPECT_CALL(get_server_slot_manager(), release_slot(Slot_handle(0)))
        .WillOnce(Return(Result<void>()));
    payload_handle->reset();
    testing::Mock::VerifyAndClearExpectations(&get_server_slot_manager());
}

TEST_P(Gateway_ipc_binding_param_test, server_sends_event_update) {
    auto [client_connector, server_connector] =
        create_connected_connectors(get_client_runtime(), get_server_runtime());

    subscribe_event(*client_connector);

    std::vector<std::byte> server_memory(get_server_metadata().slot_size, std::byte{0});

    auto slot_guard = get_server_slot_manager().create_slot_guard(Slot_handle(0), server_memory);

    EXPECT_CALL(get_server_slot_manager(), allocate_slot())
        .WillOnce(Return(Result<Shared_memory_slot_guard>(std::move(slot_guard))));

    std::vector<std::byte> const expected_payload{std::byte{1}, std::byte{2}, std::byte{3},
                                                  std::byte{4}};

    auto payload_handle = create_payload(*server_connector, event_id, expected_payload);

    Read_only_shared_memory_slot_manager_mock* mock_read_only_slot_manager = nullptr;
    auto mock_read_only_slot_manager_uptr = create_mock_unique_ptr(mock_read_only_slot_manager);

    EXPECT_CALL(get_client_shared_memory_manager_factory(), open(get_server_metadata()))
        .WillOnce(Return(Result<Read_only_shared_memory_slot_manager::Uptr>(
            std::move(mock_read_only_slot_manager_uptr))));

    Read_only_shared_memory_slot_manager::On_payload_destruction_callback
        payload_destruction_callback;

    EXPECT_CALL(*mock_read_only_slot_manager,
                get_payload(Shared_memory_handle{.slot_index = 0,
                                                 .used_bytes = get_server_metadata().slot_size},
                            _))
        .WillOnce([&server_memory, &payload_destruction_callback](auto, auto callback) {
            payload_destruction_callback = callback;

            auto payload_mock = std::make_unique<socom::Payload_mock>();
            EXPECT_CALL(*payload_mock, data())
                .WillRepeatedly(Return(socom::Payload::Span{server_memory}));
            return payload_mock;
        });

    std::promise<socom::Payload::Uptr> event_update_received_promise;
    EXPECT_CALL(mock_event_update_cb, Call(_, event_id, _))
        .Times(1)
        .WillOnce([&event_update_received_promise](auto&, auto, auto payload) {
            event_update_received_promise.set_value(std::move(payload));
        });
    auto update_result = server_connector->update_event(event_id, std::move(payload_handle));
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

    // test release of read-only payload memory after event update processing is complete
    std::promise<void> payload_released_promise;
    EXPECT_CALL(get_server_slot_manager(), release_slot(Slot_handle(0)))
        .WillOnce([&payload_released_promise](Slot_handle) {
            payload_released_promise.set_value();
            return Result<void>();
        });
    payload_destruction_callback();
    EXPECT_EQ(payload_released_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_F(Gateway_ipc_binding_unconnected_test, get_client_identifiers_reports_identifier_on_connect) {
    // Create two named clients and verify get_client_identifiers() returns their identifiers.
    auto make_named_client = [&](std::string_view identifier) {
        score::message_passing::UnixDomainClientFactory client_factory;
        auto connection = client_factory.Create(protocol_config, client_config);
        auto mock_factory_raw = create_mock_unique_ptr(mock_client_shared_memory_manager_factory);
        auto client = Gateway_ipc_binding_client::create(
            *runtime_client, std::move(connection), std::move(mock_factory_raw), {}, identifier);

        while (!client->is_connected()) {
            std::this_thread::sleep_for(1ms);
        }

        return client;
    };

    auto const start_result = server->start();
    ASSERT_TRUE(start_result);
    auto const named_client_a = make_named_client("client_A");
    auto const named_client_b = make_named_client("client_B");

    auto const identifiers = server->get_client_identifiers();

    std::vector<std::string> identifier_values;
    for (auto const& [id, info] : identifiers) {
        identifier_values.push_back(info.identifier);
    }
    EXPECT_THAT(identifier_values, ::testing::IsSupersetOf({"client_A", "client_B"}));
}

TEST_F(Gateway_ipc_binding_unconnected_test,
       get_client_identifiers_returns_empty_name_for_client_without_identifier) {
    auto const start_result = server->start();
    ASSERT_TRUE(start_result);

    // The default client was created without an identifier
    while (!client->is_connected()) {
        std::this_thread::sleep_for(1ms);
    }

    auto const identifiers = server->get_client_identifiers();
    ASSERT_EQ(identifiers.size(), 1U);
    EXPECT_EQ(identifiers.begin()->second.identifier, "");
}

TEST_F(Gateway_ipc_binding_unconnected_test,
       get_client_identifiers_returns_empty_set_without_clients) {
    auto const identifiers = server->get_client_identifiers();
    ASSERT_EQ(identifiers.size(), 0U);
}

}  // namespace score::gateway_ipc_binding
