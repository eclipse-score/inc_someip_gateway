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
#include <mutex>

#include "score/socom/clients_t.hpp"
#include "score/socom/multi_threaded_test_template.hpp"
#include "score/socom/single_connection_test_fixture.hpp"

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::Assign;

namespace score::socom {

struct State_data {
    // State_data can be updated concurrently by Server_connector and Client_connector
    std::mutex m_mutex;
    std::optional<Service_state> m_old_state;
    bool m_not_available{false};
    bool m_available{false};

    void check_state_machine(Service_state const& state) const {
        // check state machine transitions
        // First state is always not_available
        if (!m_old_state) {
            return;
        }

        // not_available -> available|not_available
        EXPECT_TRUE((Service_state::not_available != *m_old_state) ||
                    (Service_state::available == state) || (Service_state::not_available == state))
            << "old_state == " << *m_old_state << ", state == " << state;
        // available -> not_available
        EXPECT_TRUE((Service_state::available != *m_old_state) ||
                    (Service_state::not_available == state))
            << "old_state == " << *m_old_state << ", state == " << state;
    }

    bool update_reached_states(Service_state const& state) {
        m_old_state = state;
        m_not_available = m_not_available || Service_state::not_available == state;
        m_available = m_available || Service_state::available == state;
        return m_not_available && m_available;
    }
};

class Client_to_connect {
    std::atomic<bool> m_client_connected{false};
    Service_state_change_callback_mock m_state_change_mock;

   public:
    using Callback_creator_t = std::function<Service_state_change_callback()>;

    Client_to_connect() {
        EXPECT_CALL(m_state_change_mock, Call(_, _, _))
            .Times(AnyNumber())
            .WillRepeatedly(Assign(&m_client_connected, true));
    }

    void reset() { m_client_connected = false; }

    bool is_satisfied() { return m_client_connected; }

    Loop_function_t create_thread_function(Connector_factory& factory) {
        return create_thread_function(factory, factory.get_configuration(), factory.get_instance());
    }

    Loop_function_t create_thread_function(Connector_factory& factory, Callback_creator_t cbcrtr) {
        reset();
        return [&factory, cbcrtr = std::move(cbcrtr)]() {
            Client_data client{factory, Client_data::might_connect, cbcrtr()};
        };
    }

    Loop_function_t create_thread_function(
        Connector_factory& factory, Server_service_interface_configuration const& configuration,
        Service_instance const& instance) {
        reset();
        return [&factory, &configuration, &instance,
                state_change_cb = m_state_change_mock.AsStdFunction()]() {
            Client_data client{factory, Client_data::might_connect, configuration, instance,
                               state_change_cb};
        };
    }

    Service_state_change_callback create_callback() {
        return [this, data = std::make_shared<State_data>()](auto const& /*cc*/, auto state,
                                                             auto const& /*conf*/) {
            std::unique_lock<std::mutex> const lock{data->m_mutex};
            data->check_state_machine(state);
            m_client_connected = data->update_reached_states(state);
        };
    }

    Callback_creator_t callback_creator() {
        return [this]() { return create_callback(); };
    }
};

struct Subscribe_find_service_to_call {
    std::atomic<bool> cb_called{false};
    Find_result_change_callback_mock fsus_mock;

    explicit Subscribe_find_service_to_call(Connector_factory const& connector_factory) {
        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::added))
            .Times(AnyNumber())
            .WillRepeatedly(Assign(&cb_called, true));

        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::deleted))
            .Times(AnyNumber());
    }

    Loop_function_t create_thread_function(Connector_factory& connector_factory) {
        cb_called = false;
        return [&connector_factory, cb = fsus_mock.AsStdFunction()]() {
            auto const find_subscription = connector_factory.subscribe_find_service(cb);
        };
    }
};

class RuntimeMultiThreadingTest : public SingleConnectionTest {
   protected:
    std::vector<Service_instance> const input_find_result{Service_instance{"first instance"},
                                                          Service_instance{"second instance"},
                                                          connector_factory.get_instance()};

    Find_result_change_callback_mock fsus_mock;

    Subscribe_find_service_function_mock sfsf_mock;
    Request_service_function_mock rsf_mock;

    Loop_function_t create_servers_thread_main() {
        return [this]() { Server_data server{connector_factory}; };
    }

    Loop_function_t create_servers_thread_main(
        Server_service_interface_configuration const& configuration,
        Service_instance const& instance) {
        return [this, &configuration, &instance]() {
            Server_data server{connector_factory, configuration, instance};
        };
    }

    Loop_function_t create_bridges_thread_main() {
        auto const register_bridges = [this, sfsf_mock_function = sfsf_mock.AsStdFunction(),
                                       rsf_mock_function = rsf_mock.AsStdFunction()]() {
            auto const registration = connector_factory.register_service_bridge(
                Bridge_identity::make(*this), sfsf_mock_function, rsf_mock_function);
            ASSERT_TRUE(registration);
        };
        return register_bridges;
    }
};

TEST_F(RuntimeMultiThreadingTest, CreationOfServerAndClientConnectorsHasNoRaceCondition) {
    Client_to_connect client;

    auto const start_clients = client.create_thread_function(connector_factory);

    multi_threaded_test_template({create_servers_thread_main(), start_clients},
                                 [&client]() { return static_cast<bool>(client.is_satisfied()); });
}

TEST_F(RuntimeMultiThreadingTest, CreationOfServerAndClientConnectorsCallsCallbacksInCorrectOrder) {
    Client_to_connect client;

    auto const start_clients =
        client.create_thread_function(connector_factory, client.callback_creator());

    multi_threaded_test_template({create_servers_thread_main(), start_clients},
                                 [&client]() { return static_cast<bool>(client.is_satisfied()); });
}

TEST_F(RuntimeMultiThreadingTest,
       SubscribeFindServiceAndServerConnectorCreationHasNoRaceCondition) {
    Subscribe_find_service_to_call subscribe{connector_factory};

    auto const start_subscription = subscribe.create_thread_function(connector_factory);

    multi_threaded_test_template({create_servers_thread_main(), start_subscription},
                                 [&subscribe]() { return static_cast<bool>(subscribe.cb_called); });
}

TEST_F(RuntimeMultiThreadingTest,
       MultipleSubscribeFindServiceAndServerConnectorCreationHasNoRaceCondition) {
    Subscribe_find_service_to_call subscribe0{connector_factory};
    auto const start_subscription0 = subscribe0.create_thread_function(connector_factory);

    Subscribe_find_service_to_call subscribe1{connector_factory};
    auto const start_subscription1 = subscribe1.create_thread_function(connector_factory);

    multi_threaded_test_template(
        {create_servers_thread_main(), start_subscription0, start_subscription1},
        [&subscribe0, &subscribe1]() { return subscribe0.cb_called && subscribe1.cb_called; });
}

TEST_F(RuntimeMultiThreadingTest,
       BridgesWhichCreateServerConnectorsAndClientConnectorsHaveNoRaceCondition) {
    EXPECT_CALL(rsf_mock, Call(_, _))
        .Times(AnyNumber())
        .WillRepeatedly([this](auto const& configuration, auto const& /*unused*/) {
            static std::atomic_int instance_number{0};
            auto instance = Service_instance{std::to_string(instance_number++)};
            Server_data server{
                connector_factory,
                Server_service_interface_configuration{configuration.interface,
                                                       to_num_of_methods(configuration.num_methods),
                                                       to_num_of_events(configuration.num_events)},
                instance};
            return nullptr;
        });

    std::atomic<bool> client_connected{false};
    Service_state_change_callback_mock state_change_mock;
    EXPECT_CALL(state_change_mock, Call(_, _, _))
        .Times(AnyNumber())
        .WillRepeatedly(Assign(&client_connected, true));
    auto const start_clients = [this, state_change_cb = state_change_mock.AsStdFunction()]() {
        Client_data client{connector_factory, Client_data::might_connect, state_change_cb};
    };

    multi_threaded_test_template(
        {create_bridges_thread_main(), start_clients},
        [&client_connected]() { return static_cast<bool>(client_connected); });
}

TEST_F(RuntimeMultiThreadingTest, BridgesAndClientConnectorsHaveNoRaceCondition) {
    EXPECT_CALL(rsf_mock, Call(_, _)).Times(AnyNumber());

    auto const start_clients = [this]() {
        Client_data client{connector_factory, Client_data::no_connect};
    };

    multi_threaded_test_template({create_bridges_thread_main(), start_clients},
                                 []() { return true; });
}

TEST_F(RuntimeMultiThreadingTest, BridgesAndSubscribeFindServiceHaveNoRaceConditions) {
    EXPECT_CALL(sfsf_mock, Call(_, _, _))
        .Times(AnyNumber())
        .WillRepeatedly([this](auto cb, auto const& /*interface*/, auto /*instance*/) {
            for (auto const& result : input_find_result) {
                cb(connector_factory.get_configuration().get_interface(), result,
                   Find_result_status::added);
            }
            return nullptr;
        });
    std::atomic<bool> fsus_called_added{false};
    std::atomic<bool> fsus_called_delete{false};
    for (auto const& result : input_find_result) {
        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(), result,
                                    Find_result_status::added))
            .Times(AnyNumber())
            .WillRepeatedly(Assign(&fsus_called_added, true));
        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(), result,
                                    Find_result_status::deleted))
            .Times(AnyNumber())
            .WillRepeatedly(Assign(&fsus_called_delete, true));
    }

    auto const subscribe_find_service = [this, fsus_mock_function = fsus_mock.AsStdFunction()]() {
        auto const handle = connector_factory.subscribe_find_service(fsus_mock_function);
    };

    multi_threaded_test_template({create_bridges_thread_main(), subscribe_find_service},
                                 [&fsus_called_added, &fsus_called_delete]() {
                                     return static_cast<bool>(fsus_called_added) &&
                                            static_cast<bool>(fsus_called_delete);
                                 });
}

}  // namespace score::socom
