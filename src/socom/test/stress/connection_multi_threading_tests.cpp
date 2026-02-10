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

#include <atomic>
#include <cstddef>

#include "gtest/gtest.h"
#include "multi_threaded_test_template.hpp"
#include "score/socom/client_connector.hpp"
#include "score/socom/event.hpp"
#include "score/socom/method.hpp"
#include "score/socom/payload.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/service_interface_configuration.hpp"
#include "single_connection_test_fixture.hpp"

namespace socom = score::socom;

using ::ac::multi_threaded_test_template;
using ::ac::SingleConnectionTest;
using ::ac::Stop_condition;
using socom::Application_return;
using socom::Client_connector;
using socom::Disabled_server_connector;
using socom::Enabled_server_connector;
using socom::Event_id;
using socom::Event_mode;
using socom::Event_payload_allocate_callback;
using socom::Event_request_update_callback;
using socom::Event_state;
using socom::Event_subscription_change_callback;
using socom::Event_update_callback;
using socom::Method_call_credentials_callback;
using socom::Method_id;
using socom::Method_payload_allocate_callback;
using socom::Method_reply_callback;
using socom::Method_result;
using socom::Payload;
using socom::Service_interface_configuration;
using socom::Service_state;
using socom::Writable_payload;

namespace {

class Event_method_counter {
    std::size_t const m_min_events_received = 1000;
    std::size_t const m_min_method_call_responses_received = 1000;
    std::atomic<std::size_t> m_events_received{0};
    std::atomic<std::size_t> m_method_call_responses_received{0};

   public:
    void event_received() { m_events_received++; }
    std::size_t num_events_received() const { return m_events_received; }

    void method_response_received() { m_method_call_responses_received++; }
    std::size_t num_method_responses_received() const { return m_method_call_responses_received; }

    Stop_condition create_stop_condition() const {
        return [this]() {
            return num_events_received() >= m_min_events_received &&
                   num_method_responses_received() >= m_min_method_call_responses_received;
        };
    }
};

void subscribe_events(Client_connector const& cc, std::size_t const num_events) {
    for (std::size_t i{0}; i < num_events; i++) {
        auto const event_mode =
            0 == i % 2 ? Event_mode::update : Event_mode::update_and_initial_value;
        cc.subscribe_event(i, event_mode);
    }
}

void call_methods(Client_connector const& cc, std::size_t const num_methods,
                  Payload::Sptr const& payload, Method_reply_callback const& on_method_reply) {
    for (std::size_t i{0}; i < num_methods; i++) {
        auto reply_cb = 0 == i % 2 ? on_method_reply : Method_reply_callback{};
        (void)cc.call_method(i, payload, reply_cb);
    }
}

class ConnectionMultiThreadingTest : public SingleConnectionTest {
   protected:
    Event_method_counter counter;

    Method_reply_callback const on_method_reply = [this](Method_result const& /*result*/) {
        counter.method_response_received();
    };

    Event_update_callback const on_event_update =
        [this](Client_connector const& /*cc*/, Event_id const& /*eid*/,
               Payload::Sptr const& /*pl*/) { counter.event_received(); };

    Event_payload_allocate_callback const event_payload_allocate_fail_on_call =
        [](Client_connector const& /*cc*/, Event_id const& event_id) {
            ADD_FAILURE() << "Unexpected call to on_event_payload_allocate for event_id "
                          << event_id;
            return score::MakeUnexpected(
                score::socom::Server_connector_error::runtime_error_no_client_subscribed_for_event);
        };

    Method_call_credentials_callback const on_method_call =
        [](Enabled_server_connector& /*esc*/, Method_id /* mid */, Payload::Sptr const& /* pl */,
           Method_reply_callback const& reply, auto const& cred, auto const& reply_allocator) {
            if (reply) {
                reply(Method_result{Application_return{}});
            }
            return nullptr;
        };

    Event_subscription_change_callback const on_event_subscription_change =
        [this](Enabled_server_connector& esc, Event_id const& eid, Event_state const& /* state */) {
            esc.update_event(eid, real_payload);
        };

    Event_request_update_callback const on_event_update_request =
        [this](Enabled_server_connector& esc, Event_id const& eid) {
            esc.update_event(eid, real_payload);
        };

    Method_payload_allocate_callback const method_payload_allocate_fail_on_call =
        [](Enabled_server_connector& /*esc*/,
           Method_id const& /* mid */) -> score::Result<Writable_payload::Uptr> {
        ADD_FAILURE() << "Unexpected call to on_method_payload_allocate for method_id " << event_id;
        return score::MakeUnexpected(
            score::socom::Server_connector_error::logic_error_id_out_of_range);
    };

    Disabled_server_connector::Callbacks const sc_callbacks{
        on_method_call, on_event_subscription_change, on_event_update_request,
        method_payload_allocate_fail_on_call};

    std::function<void()> const server_thread = [this]() {
        auto esc = Disabled_server_connector::enable(
            this->connector_factory.create_server_connector(sc_callbacks));

        for (std::size_t i{0}; i < connector_factory.get_num_events(); i++) {
            esc->update_event(i, real_payload);
        }
    };

    Client_connector::Callbacks create_client_callbacks(
        socom::Service_state_change_callback const& on_state_change) {
        return {on_state_change, on_event_update, on_event_update,
                event_payload_allocate_fail_on_call};
    }
};

TEST_F(ConnectionMultiThreadingTest,
       ClientSubscribesEventsAndCallsMethodsInStateChangeCallbackWithoutRace) {
    auto on_state_change = [this](Client_connector const& cc, Service_state const& state,
                                  Service_interface_configuration const& /*conf*/) {
        if (Service_state::available == state) {
            subscribe_events(cc, connector_factory.get_num_events());
            call_methods(cc, connector_factory.get_num_methods(), real_payload, on_method_reply);
        }
    };

    auto const client_thread = [this, cc_callbacks = create_client_callbacks(on_state_change)]() {
        auto const cc = this->connector_factory.create_client_connector(cc_callbacks);
        (void)cc;
    };

    multi_threaded_test_template({client_thread, server_thread}, counter.create_stop_condition());
}

TEST_F(ConnectionMultiThreadingTest, ClientSubscribesEventsServerThreadAndCallsMethodsInOwnThread) {
    auto on_state_change = [this](Client_connector const& cc, Service_state const& state,
                                  Service_interface_configuration const& /*conf*/) {
        if (Service_state::available == state) {
            subscribe_events(cc, connector_factory.get_num_events());
        }
    };

    auto const client_thread = [this, cc_callbacks = create_client_callbacks(on_state_change)]() {
        auto const cc = this->connector_factory.create_client_connector(cc_callbacks);
        call_methods(*cc, connector_factory.get_num_methods(), real_payload, on_method_reply);
    };

    multi_threaded_test_template({client_thread, server_thread}, counter.create_stop_condition());
}

TEST_F(ConnectionMultiThreadingTest,
       ClientCallsMethodsInServerThreadAndSubscribesEventsInOwnThreadWithoutRace) {
    auto on_state_change = [this](Client_connector const& cc, Service_state const& state,
                                  Service_interface_configuration const& /*conf*/) {
        if (Service_state::available == state) {
            call_methods(cc, connector_factory.get_num_methods(), real_payload, on_method_reply);
        }
    };

    auto const client_thread = [this, cc_callbacks = create_client_callbacks(on_state_change)]() {
        auto const cc = this->connector_factory.create_client_connector(cc_callbacks);
        subscribe_events(*cc, connector_factory.get_num_events());
    };

    multi_threaded_test_template({client_thread, server_thread}, counter.create_stop_condition());
}

TEST_F(ConnectionMultiThreadingTest, ClientCallsMethodsAndSubscribesEventsInOwnThreadWithoutRace) {
    auto on_state_change = [](Client_connector const& /* cc */, Service_state const& /* state */,
                              Service_interface_configuration const& /*conf*/) {};

    auto const client_thread = [this, cc_callbacks = create_client_callbacks(on_state_change)]() {
        auto const cc = this->connector_factory.create_client_connector(cc_callbacks);
        subscribe_events(*cc, connector_factory.get_num_events());
        call_methods(*cc, connector_factory.get_num_methods(), real_payload, on_method_reply);
    };

    multi_threaded_test_template({client_thread, server_thread}, counter.create_stop_condition());
}

TEST_F(ConnectionMultiThreadingTest,
       ServerConnectsToClientWhileClientDtorIsRunningWithoutDeadlock) {
    std::mutex mtx_connected;
    std::mutex mtx_client_destroyed;
    std::condition_variable cv_connected;
    std::condition_variable cv_client_destroyed;
    bool connected = false;
    std::atomic<bool> client_destroyed{true};
    std::atomic<std::size_t> num_connected{0};
    std::size_t const num_connection_limit{5000};
    auto const on_state_change = [&num_connected, &connected, &cv_connected, &mtx_connected](
                                     Client_connector const& /* cc */, Service_state const& state,
                                     Service_interface_configuration const& /*conf*/) {
        num_connected += static_cast<std::size_t>(Service_state::available == state);
        if (Service_state::available == state) {
            std::unique_lock<std::mutex> lock_connected(mtx_connected);
            connected = true;
            cv_connected.notify_one();  // Notify client_thread that connection is established
        }
    };

    auto const client_thread = [this, cc_callbacks = create_client_callbacks(on_state_change),
                                &client_destroyed, &connected, &cv_client_destroyed, &cv_connected,
                                &mtx_connected, &mtx_client_destroyed]() {
        {
            client_destroyed = false;
            auto const cc = this->connector_factory.create_client_connector(cc_callbacks);

            // Wait for server to connect
            std::unique_lock<std::mutex> lock_connected(mtx_connected);
            if (!connected) {
                cv_connected.wait(lock_connected);
            }
        }

        std::unique_lock<std::mutex> lock_destroyed(mtx_client_destroyed);
        client_destroyed = true;
        cv_client_destroyed.notify_one();  // Notify server_thread that client is destroyed
    };

    auto const server_thread = [this, &client_destroyed, &cv_client_destroyed,
                                &mtx_client_destroyed]() {
        // Exit while client is marked as destroyed, Multi_threaded_test_template will run us again.
        // We need to do it like this in order to handle cases where server_thread already
        // starts, but the stop condition prevents creation of a new client_thread.
        if (client_destroyed) {
            return;
        }

        auto const esc = Disabled_server_connector::enable(
            connector_factory.create_server_connector(sc_callbacks));

        std::unique_lock<std::mutex> lock_destroyed(mtx_client_destroyed);
        // Wait for client to be destroyed
        if (!client_destroyed) {
            cv_client_destroyed.wait(lock_destroyed);
        }
    };

    multi_threaded_test_template({client_thread, server_thread}, [&num_connected]() {
        return num_connected > num_connection_limit;
    });
}

}  // namespace
