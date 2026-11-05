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

#include "score/socom/server_t.hpp"

#include <atomic>
#include <future>
#include <memory>

#include "score/socom/payload.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/socom_mocks.hpp"
#include "score/socom/utilities.hpp"
#include "score/socom/vector_payload.hpp"

using ::testing::_;
using ::testing::Assign;
using ::testing::DoAll;
namespace score::socom {
namespace {
auto ignore_call() {
    return []() {};
}

}  // namespace

static_assert(!std::is_default_constructible<Server_data>::value, "");

Server_data::Server_data(Connector_factory& factory)
    : m_connector{factory.create_and_enable(m_callbacks)} {}

Server_data::Server_data(Connector_factory& factory,
                         Server_service_interface_definition const& configuration,
                         Service_instance const& instance)
    : m_connector{factory.create_and_enable(configuration, instance, m_callbacks)} {}

Server_data::Server_data(Connector_factory& factory,
                         Server_service_interface_definition const& configuration,
                         Service_instance const& instance, Posix_credentials const& credentials)
    : m_connector{factory.create_and_enable(configuration, instance, m_credential_callbacks,
                                            credentials)} {}

Server_data::~Server_data() { wait_for_atomics(m_callback_called, m_subscribed, m_unsubscribed); }

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

void Server_data::update_event(Event_id const& event_id, Payload const& payload) {
    EXPECT_TRUE(m_connector->update_event(event_id, clone_payload(payload)));
}

void Server_data::update_requested_event(Event_id const& event_id, Payload const& payload) {
    EXPECT_TRUE(m_connector->update_requested_event(event_id, clone_payload(payload)));
}

std::atomic<bool> const& Server_data::expect_on_event_subscription_change(
    Event_id const& event_id, Event_state const& state,
    Event_subscription_change_callback subscription_change_callback) {
    auto& atomi = state == Event_state::subscribed ? m_subscribed : m_unsubscribed;
    EXPECT_TRUE(atomi);
    atomi = false;

    auto callback = std::make_shared<Event_subscription_change_callback>(
        std::move(subscription_change_callback));
    auto call_callback = [callback](auto& sc, auto const event_id, auto const state) {
        if (!callback->empty()) {
            (*callback)(sc, event_id, state);
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

std::atomic<bool> const& Server_data::expect_update_event_requests(Event_id const& event_id) {
    EXPECT_TRUE(m_callback_called);
    m_callback_called = false;
    EXPECT_CALL(m_callbacks, on_event_update_request(_, event_id))
        .WillOnce(Assign(&m_callback_called, true))
        .WillRepeatedly(ignore_call());
    return m_callback_called;
}

void Server_data::expect_and_respond_update_event_request(Event_id const& event_id,
                                                          Payload const& payload) {
    auto cloned = std::make_shared<Payload>(clone_payload(payload));
    EXPECT_CALL(m_callbacks, on_event_update_request(_, event_id))
        .WillOnce([cloned](Enabled_server_connector& connector, Event_id const& eid) {
            EXPECT_TRUE(connector.update_requested_event(eid, clone_payload(*cloned)));
        });
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

}  // namespace score::socom
