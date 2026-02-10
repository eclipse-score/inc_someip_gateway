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

#include "server_t.hpp"

#include <atomic>
#include <future>

#include "score/socom/method.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/server_connector.hpp"
#include "socom_mocks.hpp"
#include "utilities.hpp"

using ::score::socom::Disabled_server_connector;
using ::score::socom::Enabled_server_connector;
using ::score::socom::Event_id;
using ::score::socom::Event_mode;
using ::score::socom::Event_state;
using ::score::socom::Event_subscription_change_callback;
using ::score::socom::Method_id;
using ::score::socom::Method_invocation;
using ::score::socom::Method_reply_callback;
using ::score::socom::Method_result;
using ::score::socom::Payload;
using ::score::socom::Server_service_interface_configuration;
using ::score::socom::Service_instance;
using ::testing::_;
using ::testing::Assign;
using ::testing::ByMove;
using ::testing::DoAll;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;

namespace {
::ac::Server_connector_callbacks_mock& expect_method_call(
    ::ac::Server_connector_callbacks_mock& sc_callbacks, Method_id const& method_id,
    Payload::Sptr const& expected_payload) {
    EXPECT_CALL(sc_callbacks, on_method_call(_, method_id, expected_payload, _));
    return sc_callbacks;
}

auto ignore_call() {
    return InvokeWithoutArgs([]() {});
}

}  // namespace

namespace ac {

static_assert(!std::is_default_constructible<Server_data>::value, "");

Server_data::Server_data(Connector_factory& factory)
    : m_connector{factory.create_and_enable(m_callbacks)} {}

Server_data::Server_data(Connector_factory& factory, Method_id method_id,
                         Payload::Sptr const& expected_payload)
    : m_connector{factory.create_and_enable(
          expect_method_call(m_callbacks, method_id, expected_payload))} {}

Server_data::Server_data(Connector_factory& factory,
                         Server_service_interface_configuration const& configuration,
                         Service_instance const& instance)
    : m_connector{factory.create_and_enable(configuration, instance, m_callbacks)} {}

Server_data::Server_data(
    Connector_factory& factory,
    ::score::socom::Server_service_interface_configuration const& configuration,
    ::score::socom::Service_instance const& instance,
    ::score::socom::Posix_credentials const& credentials)
    : m_connector{factory.create_and_enable(configuration, instance, m_credential_callbacks,
                                            credentials)} {}

Server_data::~Server_data() {
    wait_for_atomics(m_callback_called, m_method_callback_called, m_subscribed, m_unsubscribed);
}

Server_connector_callbacks_mock& Server_data::get_callbacks() { return m_callbacks; }

Server_connector_credentials_callbacks_mock& Server_data::get_credentials_callbacks() {
    return m_credential_callbacks;
}

Enabled_server_connector& Server_data::get_connector() { return *m_connector; }

Disabled_server_connector::Uptr Server_data::disable() {
    return Enabled_server_connector::disable(std::move(m_connector));
}

void Server_data::enable(Disabled_server_connector::Uptr disabled_connector) {
    m_connector = Disabled_server_connector::enable(std::move(disabled_connector));
}

void Server_data::update_event(Event_id const& event_id, Payload::Sptr const& payload) {
    m_connector->update_event(event_id, payload);
}

void Server_data::update_requested_event(Event_id const& event_id, Payload::Sptr const& payload) {
    m_connector->update_requested_event(event_id, payload);
}

std::atomic<bool> const& Server_data::expect_on_event_subscription_change(
    Event_id const& event_id, Event_state const& state,
    Event_subscription_change_callback subscription_change_callback) {
    auto& atomi = state == Event_state::subscribed ? m_subscribed : m_unsubscribed;
    EXPECT_TRUE(atomi);
    atomi = false;

    auto call_callback = [subscription_change_callback = std::move(subscription_change_callback)](
                             auto& sc, auto const event_id, auto const state) {
        if (subscription_change_callback) {
            subscription_change_callback(sc, event_id, state);
        }
    };

    EXPECT_CALL(m_callbacks, on_event_subscription_change(_, event_id, state))
        .WillOnce(DoAll(call_callback, Assign(&atomi, true)));
    return atomi;
}

void Server_data::expect_on_event_subscription_change_nosync(Event_id const& event_id,
                                                             Event_state const& state) {
    EXPECT_CALL(m_callbacks, on_event_subscription_change(_, event_id, state));
}

std::atomic<bool> const& Server_data::expect_update_event_request(Event_id const& event_id) {
    EXPECT_TRUE(m_callback_called);
    m_callback_called = false;
    EXPECT_CALL(m_callbacks, on_event_update_request(_, event_id))
        .WillOnce(Assign(&m_callback_called, true));
    return m_callback_called;
}

std::atomic<bool> const& Server_data::expect_update_event_requests(
    ::score::socom::Event_id const& event_id) {
    EXPECT_TRUE(m_callback_called);
    m_callback_called = false;
    EXPECT_CALL(m_callbacks, on_event_update_request(_, event_id))
        .WillOnce(Assign(&m_callback_called, true))
        .WillRepeatedly(ignore_call());
    return m_callback_called;
}

void Server_data::expect_and_respond_update_event_request(Event_id const& event_id,
                                                          Payload::Sptr const& payload) {
    EXPECT_CALL(m_callbacks, on_event_update_request(_, event_id))
        .WillOnce([&payload](Enabled_server_connector& connector, Event_id const& eid) {
            connector.update_requested_event(eid, payload);
        });
}

std::atomic<bool> const& Server_data::expect_and_respond_method_calls(size_t const counter,
                                                                      Method_id const& method_id,
                                                                      Payload::Sptr const& payload,
                                                                      Method_result const& result) {
    EXPECT_TRUE(m_method_callback_called);
    m_method_callback_called = false;
    m_num_method_calls = 0;

    auto const reply = [this, &result, counter](auto& /*connector*/, auto /*mid*/,
                                                auto const& /*pl*/, auto const& cb) {
        if (cb) {
            cb(result);
        }
        m_num_method_calls++;
        if (counter == m_num_method_calls) {
            m_method_callback_called = true;
        }
        return std::make_unique<Method_invocation>();
    };

    EXPECT_CALL(m_callbacks, on_method_call(_, method_id, payload, _))
        .Times(counter)
        .WillRepeatedly(reply);
    return m_method_callback_called;
}

std::atomic<bool> const& Server_data::expect_and_respond_method_call(Method_id const& method_id,
                                                                     Payload::Sptr const& payload,
                                                                     Method_result const& result) {
    return expect_and_respond_method_calls(1, method_id, payload, result);
}

std::future<Method_reply_callback> Server_data::expect_and_return_method_call(
    Method_id const& method_id, Payload::Sptr const& payload) {
    EXPECT_TRUE(m_method_callback_called);
    m_method_callback_called = false;

    auto saved_callback = std::make_shared<std::promise<Method_reply_callback>>();
    auto const reply = [saved_callback](Enabled_server_connector& /*connector*/, auto /*mid*/,
                                        auto const& /*pl*/,
                                        auto const& cb) { saved_callback->set_value(cb); };

    EXPECT_CALL(m_callbacks, on_method_call(_, method_id, payload, _))
        .WillOnce(DoAll(Assign(&m_method_callback_called, true), reply,
                        Return(ByMove(std::make_unique<Method_invocation>()))));

    return saved_callback->get_future();
}

std::future<void> Server_data::expect_method_calls(std::size_t const& min_num,
                                                   ::score::socom::Method_id const& method_id,
                                                   ::score::socom::Payload::Sptr const& payload) {
    std::promise<void> methods_called;
    auto methods_called_future = methods_called.get_future();

    auto check_update_count =
        create_check_update_count(m_num_method_calls, min_num, std::move(methods_called));

    EXPECT_CALL(m_callbacks, on_method_call(_, method_id, payload, _))
        .WillRepeatedly(DoAll(check_update_count, InvokeWithoutArgs([]() { return nullptr; })));
    return methods_called_future;
}

Event_mode Server_data::get_event_mode(Event_id server_id) const {
    auto result = m_connector->get_event_mode(server_id);
    EXPECT_TRUE(result);
    return std::move(result).value_or(Event_mode::update);
}

void Server_data::expect_event_subscription(Event_id const& event_id) {
    expect_on_event_subscription_change_nosync(event_id, Event_state::subscribed);
    expect_on_event_subscription_change_nosync(event_id, Event_state::unsubscribed);
}

}  // namespace ac
