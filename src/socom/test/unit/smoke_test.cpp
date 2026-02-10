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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <score/socom/callback_mocks.hpp>
#include <score/socom/runtime.hpp>

using score::socom::Application_return;
using score::socom::Client_connector;
using score::socom::create_runtime;
using score::socom::Disabled_server_connector;
using score::socom::empty_payload;
using score::socom::Enabled_server_connector;
using score::socom::Event_mode;
using score::socom::Event_payload_allocate_callback_mock;
using score::socom::Event_request_update_callback;
using score::socom::Event_request_update_callback_mock;
using score::socom::Event_state;
using score::socom::Event_subscription_change_callback;
using score::socom::Event_subscription_change_callback_mock;
using score::socom::Event_update_callback;
using score::socom::Event_update_callback_mock;
using score::socom::Find_result_change_callback;
using score::socom::Find_result_change_callback_mock;
using score::socom::Find_result_status;
using score::socom::Method_call_credentials_callback;
using score::socom::Method_call_credentials_callback_mock;
using score::socom::Method_reply_callback;
using score::socom::Method_reply_callback_mock;
using score::socom::Runtime;
using score::socom::Server_service_interface_configuration;
using score::socom::Service_instance;
using score::socom::Service_interface;
using score::socom::Service_state;
using score::socom::Service_state_change_callback;
using score::socom::Service_state_change_callback_mock;
using score::socom::to_num_of_events;
using score::socom::to_num_of_methods;
using testing::_;
using testing::DoAll;
using testing::MockFunction;
using testing::Return;
using testing::SaveArgByMove;

class Runtime_test : public ::testing::Test {
   protected:
    Server_service_interface_configuration config{Service_interface{"example.interface", {1, 0}},
                                                  to_num_of_methods(1), to_num_of_events(1)};
    Service_instance instance{"instance1"};

    Runtime::Uptr runtime = create_runtime();

    // Runtime mocks
    Find_result_change_callback_mock m_find_result_mock;

    // Client_connector mocks
    Service_state_change_callback_mock m_service_state_change_mock;
    Event_update_callback_mock m_event_update_mock;
    Event_update_callback_mock m_requested_event_update_mock;
    Event_payload_allocate_callback_mock m_event_payload_allocate_mock;

    Client_connector::Callbacks client_callbacks{m_service_state_change_mock.AsStdFunction(),
                                                 m_event_update_mock.AsStdFunction(),
                                                 m_requested_event_update_mock.AsStdFunction(),
                                                 m_event_payload_allocate_mock.AsStdFunction()};

    // Server_connector mocks
    Event_subscription_change_callback_mock m_event_subscription_change_mock;
    Event_request_update_callback_mock m_event_update_request_mock;
    Method_call_credentials_callback_mock m_method_call_mock;

    Disabled_server_connector::Callbacks server_callbacks{
        m_method_call_mock.AsStdFunction(), m_event_subscription_change_mock.AsStdFunction(),
        m_event_update_request_mock.AsStdFunction()};
};

class Connection_test : public Runtime_test {
   protected:
    void SetUp() override {
        Runtime_test::SetUp();

        auto client_connector_result =
            runtime->make_client_connector(config, instance, client_callbacks);
        auto server_connector_result =
            runtime->make_server_connector(config, instance, server_callbacks);

        ASSERT_TRUE(client_connector_result);
        ASSERT_TRUE(server_connector_result);

        EXPECT_CALL(m_service_state_change_mock, Call(_, Service_state::available, _)).Times(1);

        client_connector = std::move(client_connector_result.value());
        server_connector =
            Disabled_server_connector::enable(std::move(server_connector_result.value()));
    }

    void TearDown() override {
        EXPECT_CALL(m_service_state_change_mock, Call(_, Service_state::not_available, _)).Times(1);
        Runtime_test::TearDown();
    }

    std::unique_ptr<Client_connector> client_connector;
    std::unique_ptr<Enabled_server_connector> server_connector;

    // Method mocks
    Method_call_credentials_callback_mock m_method_call_credentials_mock;
    Method_reply_callback_mock m_method_reply_mock;
};

TEST_F(Runtime_test, client_connector_construction_works) {
    auto const client_connector_result =
        runtime->make_client_connector(config, instance, client_callbacks);
    EXPECT_TRUE(client_connector_result);
}

TEST_F(Runtime_test, server_connector_construction_works) {
    auto const server_connector_result =
        runtime->make_server_connector(config, instance, server_callbacks);
    EXPECT_TRUE(server_connector_result);
}

TEST_F(Runtime_test, subscribe_find_service_finds_server) {
    auto const find_subscription = runtime->subscribe_find_service(
        m_find_result_mock.AsStdFunction(), config.get_interface(), std::nullopt, std::nullopt);

    EXPECT_CALL(m_find_result_mock, Call(_, _, Find_result_status::added)).Times(1);

    auto server_connector_result =
        runtime->make_server_connector(config, instance, server_callbacks);
    ASSERT_TRUE(server_connector_result);
    auto enabled_server_connector =
        Disabled_server_connector::enable(std::move(server_connector_result.value()));

    EXPECT_CALL(m_find_result_mock, Call(_, _, Find_result_status::deleted)).Times(1);

    enabled_server_connector.reset();
}

TEST_F(Runtime_test, connection_setup_works) {
    auto const client_connector_result =
        runtime->make_client_connector(config, instance, client_callbacks);
    auto server_connector_result =
        runtime->make_server_connector(config, instance, server_callbacks);

    ASSERT_TRUE(client_connector_result);
    ASSERT_TRUE(server_connector_result);

    EXPECT_CALL(m_service_state_change_mock, Call(_, Service_state::available, _)).Times(1);

    auto const enabled_server_connector =
        Disabled_server_connector::enable(std::move(server_connector_result.value()));

    EXPECT_CALL(m_service_state_change_mock, Call(_, Service_state::not_available, _)).Times(1);
}

TEST_F(Connection_test, server_sends_event_which_is_received_by_the_client) {
    EXPECT_CALL(m_event_update_mock, Call(_, _, _)).Times(1);
    EXPECT_CALL(m_event_subscription_change_mock, Call(_, 0, Event_state::subscribed)).Times(1);
    ASSERT_TRUE(client_connector->subscribe_event(0, Event_mode::update));
    server_connector->update_event(0, empty_payload());
}

TEST_F(Connection_test, client_calls_method_and_gets_response) {
    Method_reply_callback pointer;
    EXPECT_CALL(m_method_call_mock, Call(_, 0, _, _, _, _))
        .WillOnce(DoAll(SaveArgByMove<3>(&pointer), Return(nullptr)));
    auto const invocation =
        client_connector->call_method(0, empty_payload(), m_method_reply_mock.AsStdFunction());
    // ASSERT_TRUE(invocation);

    EXPECT_CALL(m_method_reply_mock, Call).Times(1);
    pointer(Application_return{empty_payload()});
}
