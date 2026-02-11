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

#include <algorithm>
#include <chrono>
#include <future>
#include <iterator>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

#include "score/socom/bridge_t.hpp"
#include "score/socom/client_connector.hpp"
#include "score/socom/clients_t.hpp"
#include "score/socom/connector_factory.hpp"
#include "score/socom/runtime.hpp"
#include "score/socom/server_t.hpp"
#include "score/socom/service_interface_configuration.hpp"
#include "score/socom/single_connection_test_fixture.hpp"
#include "score/socom/socom_mocks.hpp"
#include "score/socom/utilities.hpp"

namespace socom = score::socom;
using namespace std::chrono_literals;

using ::score::socom::append;
using ::score::socom::Bridge_data;
using ::score::socom::Client_connector_callbacks_mock;
using ::score::socom::Client_data;
using ::score::socom::Connector_factory;
using ::score::socom::create_instances;
using ::score::socom::create_service_instance;
using ::score::socom::create_service_interface_configuration;
using ::score::socom::create_service_interfaces;
using ::score::socom::Server_connector_callbacks_mock;
using ::score::socom::Server_data;
using ::score::socom::SingleConnectionTest;
using ::score::socom::wait_for_atomics;
using socom::Bridge_identity;
using socom::Client_connector;
using socom::Construction_error;
using socom::create_runtime;
using socom::Disabled_server_connector;
using socom::Find_result_change_callback;
using socom::Find_result_change_callback_mock;
using socom::Find_result_container;
using socom::Find_result_status;
using socom::Find_subscription;
using socom::Find_subscription_handle;
using socom::Legacy_find_result_callback_mock;
using socom::Request_service_function;
using socom::Request_service_function_mock;
using socom::Runtime;
using socom::Service_bridge_registration;
using socom::Service_instance;
using socom::Service_interface;
using socom::Service_interface_configuration;
using socom::Service_state;
using socom::Service_state_change_callback_mock;
using socom::Subscribe_find_service_function;
using socom::Subscribe_find_service_function_mock;
using ::testing::_;
using ::testing::Assign;
using ::testing::Bool;
using ::testing::ByMove;
using ::testing::Combine;
using ::testing::InSequence;
using ::testing::Invoke;
using ::testing::InvokeWithoutArgs;
using ::testing::Return;
using ::testing::TestParamInfo;
using ::testing::Values;
using ::testing::WithParamInterface;

namespace {

enum class Destruction_order { requests_first, bridges_first };
using Bridge_param_tuple = std::tuple<size_t, size_t, size_t, Destruction_order, bool, bool>;
using Subscribe_find_service_param_tuple = std::tuple<size_t, size_t, size_t, size_t, bool>;
using Bridges = std::vector<std::unique_ptr<Bridge_data>>;
using Atomic_getter = std::atomic<bool> const& (Bridge_data::*)() const;

struct Bridge_param {
    size_t requests_before_bridge_creation;
    size_t requests_after_bridge_creation;
    size_t num_bridges;
    Destruction_order order;
    bool delete_and_recreate_bridges;
    bool delete_and_recreate_requests;

    explicit Bridge_param(Bridge_param_tuple const& param_tuple)
        : requests_before_bridge_creation{std::get<0>(param_tuple)},
          requests_after_bridge_creation{std::get<1>(param_tuple)},
          num_bridges{std::get<2>(param_tuple)},
          order{std::get<3>(param_tuple)},
          delete_and_recreate_bridges{std::get<4>(param_tuple)},
          delete_and_recreate_requests{std::get<5>(param_tuple)} {}

    size_t get_total_requests() const {
        return requests_before_bridge_creation + requests_after_bridge_creation;
    }

    Bridge_data::Creation_sequence get_sequence() const {
        auto const sequence = 0 == requests_before_bridge_creation
                                  ? Bridge_data::bridge_then_expect
                                  : Bridge_data::expect_then_bridge;
        return sequence;
    }

    Bridge_param with_clients_already_created() const {
        return Bridge_param{std::make_tuple(get_total_requests(), 0, num_bridges, order,
                                            delete_and_recreate_bridges,
                                            delete_and_recreate_requests)};
    }

    Bridge_data::Expect expect_or_nothing(Bridge_data::Expect const& expect) const {
        return 0 != get_total_requests() ? expect : Bridge_data::nothing;
    }
};

struct Subscribe_find_service_params {
    size_t num_interfaces;
    size_t num_instances;
    size_t num_subscriptions_before_server_creation;
    size_t num_subscriptions_after_server_creation;
    bool clear_subscriptions_first;

    explicit Subscribe_find_service_params(Subscribe_find_service_param_tuple const& tuple)
        : num_interfaces{std::get<0>(tuple)},
          num_instances{std::get<1>(tuple)},
          num_subscriptions_before_server_creation{std::get<2>(tuple)},
          num_subscriptions_after_server_creation{std::get<3>(tuple)},
          clear_subscriptions_first{std::get<4>(tuple)} {}

    size_t get_total_subsciptions() const {
        return num_subscriptions_before_server_creation + num_subscriptions_after_server_creation;
    }
};

std::string readable_test_names_wildcard(
    TestParamInfo<Subscribe_find_service_param_tuple> const& param_info) {
    Subscribe_find_service_params const params{param_info.param};
    std::stringstream ss;
    ss << params.num_interfaces << "_interfaces__and_";
    ss << params.num_instances << "_instances__and_";
    ss << params.num_subscriptions_before_server_creation
       << "_subscriptions_before_server_creation__and_";
    ss << params.num_subscriptions_after_server_creation
       << "_subscriptions_after_server_creation__and_";
    ss << "deleting_";
    if (params.clear_subscriptions_first) {
        ss << "subscriptions";
    } else {
        ss << "servers";
    }
    ss << "_first";

    return ss.str();
}

std::string readable_test_names_bridge(TestParamInfo<Bridge_param_tuple> const& param_info) {
    auto const param = Bridge_param{param_info.param};
    std::stringstream ss;
    ss << "with_" << param.num_bridges << "_bridges";
    if (0 != param.requests_before_bridge_creation) {
        ss << "__and_" << param.requests_before_bridge_creation << "_requests_before_bridge";
    }
    if (0 != param.requests_after_bridge_creation) {
        ss << "__and_" << param.requests_after_bridge_creation << "_requests_after_bridge";
    }
    if (Destruction_order::requests_first == param.order) {
        ss << "__and_requests_and_then_bridges_destroyed";
    } else {
        ss << "__and_bridges_and_then_requests_destroyed";
    }
    if (param.delete_and_recreate_bridges) {
        ss << "__and_deleting_and_recreating_bridges";
    }
    if (param.delete_and_recreate_requests) {
        ss << "__and_deleting_and_recreating_requests";
    }

    return ss.str();
}

std::vector<Find_subscription> create_subscriptions(size_t const num_find_services,
                                                    Find_result_change_callback_mock& fsus_mock,
                                                    Connector_factory& connector_factory) {
    auto handles = std::vector<Find_subscription>{};
    handles.reserve(num_find_services);
    for (auto i = size_t{0}; i < num_find_services; i++) {
        handles.emplace_back(connector_factory.subscribe_find_service(
            fsus_mock.AsStdFunction(), connector_factory.get_instance()));
    }

    return handles;
}

template <typename SubscriptionMockT>
std::vector<Find_subscription> create_wildcard_subscriptions(size_t const num,
                                                             SubscriptionMockT& mock,
                                                             Connector_factory& factory) {
    std::vector<Find_subscription> subscriptions;
    subscriptions.reserve(num);
    for (size_t i = 0; i < num; i++) {
        subscriptions.emplace_back(factory.subscribe_find_service_wildcard(mock.AsStdFunction()));
    }
    return subscriptions;
}

template <typename T>
void pop_empty(std::vector<T>& v, std::function<bool()> const& get_atom,
               Destruction_order const& order) {
    while (!v.empty()) {
        EXPECT_FALSE((Destruction_order::requests_first == order) && get_atom());
        v.erase(std::begin(v));
    }
    wait_for_atomics(get_atom());
}

Bridges create_bridges(Bridge_param const& param, Bridge_data::Expect const& expect,
                       Connector_factory& factory) {
    auto result = Bridges{param.num_bridges};
    for (auto& bridge : result) {
        bridge = std::make_unique<Bridge_data>(param.get_sequence(),
                                               param.expect_or_nothing(expect), factory);
    }
    return result;
}

bool request_service_destroyed(Bridges const& bridges) {
    auto const request_find_service_destroyed = [](Bridges::value_type const& bridge) {
        return bridge->get_request_find_service_destroyed().load();
    };

    return std::all_of(std::begin(bridges), std::end(bridges), request_find_service_destroyed);
}

bool subscribe_find_service_destroyed(Bridges const& bridges) {
    auto const find_service_destroyed = [](Bridges::value_type const& bridge) {
        return bridge->get_subscribe_find_service_destroyed().load();
    };

    return std::all_of(std::begin(bridges), std::end(bridges), find_service_destroyed);
}

std::function<bool()> create_get_destroyed(
    Bridges const& bridges, std::function<bool(Bridges const&)> const& get_destroyed_fun) {
    auto const get_destroyed = [&bridges, get_destroyed_fun]() {
        return get_destroyed_fun(bridges);
    };
    return get_destroyed;
}

void check_construction_of_callback(
    Bridge_param const& param, Bridges const& bridges, Atomic_getter const& getter,
    std::function<void(Bridge_data const&)> const& post_check_action) {
    for (auto const& bridge : bridges) {
        if (0 != param.get_total_requests()) {
            wait_for_atomics(((*bridge).*getter)());
            post_check_action(*bridge);
        } else {
            EXPECT_FALSE(((*bridge).*getter)());
        }
    }
}

void clean_bridges(Bridges& bridges, bool const clients_destroyed) {
    if (!clients_destroyed) {
        for (auto& bridge : bridges) {
            bridge->no_destroyed_check();
        }
    }
    bridges.clear();
}

void expect_callbacks(Bridges& bridges, Bridge_data::Expect const& expect,
                      Connector_factory const& connector_factory) {
    for (auto& bridge : bridges) {
        bridge->expect_callbacks(expect, connector_factory);
    }
}

template <typename T>
void clean_test(std::vector<T>& requests, Bridges& bridges, Bridge_param const& param,
                std::function<bool()> const& get_destroyed) {
    if (Destruction_order::requests_first == param.order) {
        pop_empty(requests, get_destroyed, param.order);
        clean_bridges(bridges, true);
    } else {
        clean_bridges(bridges, false);
        pop_empty(requests, get_destroyed, param.order);
    }
}

void recreate_bridges(Bridge_param const& param, Bridge_data::Expect const& expect,
                      Atomic_getter const& getter, Bridges& bridges,
                      Connector_factory& connector_factory,
                      std::function<void(Bridge_data const&)> const& post_check_action) {
    if (param.delete_and_recreate_bridges) {
        clean_bridges(bridges, false);
        append(bridges,
               create_bridges(param.with_clients_already_created(), expect, connector_factory));
        check_construction_of_callback(param, bridges, getter, post_check_action);
    }
}

template <typename REQUESTS, typename REQUESTSCREATOR>
void recreate_requests(REQUESTS& requests, Bridges& bridges,
                       Connector_factory const& connector_factory, Bridge_param const& param,
                       Bridge_data::Expect const& expect, Atomic_getter const& getter,
                       std::function<bool()> const& get_destroyed,
                       REQUESTSCREATOR const& create_requests,
                       std::function<void(Bridge_data const&)> const& post_check_action) {
    if (param.delete_and_recreate_requests) {
        pop_empty(requests, get_destroyed, Destruction_order::requests_first);
        expect_callbacks(bridges, param.expect_or_nothing(expect), connector_factory);
        append(requests, create_requests(param.get_total_requests()));
        check_construction_of_callback(param, bridges, getter, post_check_action);
    }
}

template <typename CREATEREQUESTS>
void bridge_test_template(CREATEREQUESTS const& create_requests, Bridge_param const& param,
                          Connector_factory& connector_factory, Bridge_data::Expect const& expect,
                          std::function<bool(Bridges const&)> const& get_destroyed_fun,
                          Atomic_getter const& getter,
                          std::function<void(Bridge_data const&)> const& post_check_action) {
    auto clients = create_requests(param.requests_before_bridge_creation);

    auto bridges = create_bridges(param, expect, connector_factory);
    auto const get_destroyed = create_get_destroyed(bridges, get_destroyed_fun);

    append(clients, create_requests(param.requests_after_bridge_creation));

    check_construction_of_callback(param, bridges, getter, post_check_action);

    recreate_bridges(param, expect, getter, bridges, connector_factory, post_check_action);

    recreate_requests(clients, bridges, connector_factory, param, expect, getter, get_destroyed,
                      create_requests, post_check_action);

    clean_test(clients, bridges, param, get_destroyed);
}

TEST(RuntimeFactoryTest, DefaultConstructorWorks) {
    Runtime::Uptr const rt = create_runtime();
    EXPECT_NE(nullptr, rt);
}

class RuntimeTest : public SingleConnectionTest {
   protected:
    std::vector<Service_instance> const input_find_result{Service_instance{"first instance"},
                                                          Service_instance{"second instance"},
                                                          connector_factory.get_instance()};
    Service_instance const expected_find_result{connector_factory.get_instance()};

    std::atomic<bool> subscribe_find_service_cb_called{false};
    Find_result_change_callback_mock fsus_mock;
    Legacy_find_result_callback_mock legacy_fsus_mock;

    Subscribe_find_service_function_mock sfsf_mock;
    Request_service_function_mock rsf_mock;

    RuntimeTest() {
        ON_CALL(fsus_mock, Call(_, _, _))
            .WillByDefault(Assign(&subscribe_find_service_cb_called, true));
    }
};

class RuntimeLegacySubscribeFindServiceTest : public RuntimeTest {};

// NOLINTNEXTLINE(readability-redundant-string-init)
MATCHER_P(Multi_set_equal, expected, "") {
    std::multiset<Service_instance> const expected_set{expected.cbegin(), expected.cend()};
    std::multiset<Service_instance> const input_set{arg.cbegin(), arg.cend()};
    return (input_set == expected_set);
}

TEST_F(RuntimeTest, SubscribeFindServiceWithNoServersDoesNotCallCallback) {
    EXPECT_CALL(fsus_mock, Call(_, _, _)).Times(0);

    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());
}

TEST_F(RuntimeTest, SubscribeFindServiceWithDisabledServerConnectorDoesNotCallCallback) {
    EXPECT_CALL(fsus_mock, Call(_, _, _)).Times(0);

    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    Server_connector_callbacks_mock sc_callbacks;
    auto const disabled_server = connector_factory.create_server_connector(sc_callbacks);
}

TEST_F(RuntimeTest,
       SubscribeFindServiceWithLaterStartedServerCallsCallbackWhenServerIsStartedAndStopped) {
    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    {
        InSequence const is;
        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::added));

        Server_data server{connector_factory};
        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::deleted));
    }

    wait_for_atomics(subscribe_find_service_cb_called);
}

TEST_F(RuntimeTest,
       SubscribeFindServiceWithLaterCreatedServerCallsCallbackAfterServerAndClientAreConnected) {
    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    {
        InSequence const is;

        Client_data client{connector_factory, Client_data::no_connect};
        auto const& client_connected = client.expect_service_state_change(Service_state::available);

        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::added));

        Server_data server{connector_factory};

        wait_for_atomics(client_connected);

        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::deleted));
        client.expect_service_state_change(Service_state::not_available);
    }

    wait_for_atomics(subscribe_find_service_cb_called);
}

TEST_F(RuntimeTest, SubscribeFindServiceWithLaterCreatedServerCallsMethodCall) {
    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    Client_data client{connector_factory, Client_data::might_connect};

    auto call_method = [this, &client](auto const& /*interface*/, auto const& /*instance*/,
                                       auto const /*status*/) {
        client.call_method(method_id, real_payload);
    };

    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added))
        .WillOnce(DoAll(call_method, Assign(&subscribe_find_service_cb_called, true)));

    auto const& client_connected = client.expect_service_state_change(Service_state::available);
    Server_data server{connector_factory, method_id, real_payload};

    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::deleted));
    client.expect_service_state_change(Service_state::not_available);

    wait_for_atomics(subscribe_find_service_cb_called, client_connected);
}

TEST_F(
    RuntimeTest,
    SubscribeFindServiceWithConcurrentlyStartedServerCallsCallbackWhenServerIsStartedAndStopped) {
    for (int i = -100; i < 100; i++) {
        auto const server_sweep_delay = i > 0 ? i : 0;
        auto const find_sweep_delay = i < 0 ? (i * -1) : 0;

        {
            InSequence const is;
            EXPECT_CALL(fsus_mock,
                        Call(connector_factory.get_configuration().get_interface(),
                             connector_factory.get_instance(), Find_result_status::added));

            EXPECT_CALL(fsus_mock,
                        Call(connector_factory.get_configuration().get_interface(),
                             connector_factory.get_instance(), Find_result_status::deleted));
        }

        std::atomic_bool find_subscription_completed{false};
        auto server_thread =
            std::thread([this, server_sweep_delay, &find_subscription_completed]() {
                std::this_thread::sleep_for(std::chrono::nanoseconds(server_sweep_delay));

                Server_data server{connector_factory};

                wait_for_atomics(find_subscription_completed);
            });

        std::this_thread::sleep_for(std::chrono::nanoseconds(find_sweep_delay));

        auto const find_subscription =
            connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

        find_subscription_completed = true;
        server_thread.join();
    }
}

TEST_F(RuntimeTest, SubscribeFindServiceWithCreatedAndDestroyedStartedServerCallsNoCallback) {
    std::unique_ptr<Client_data> client{};
    {
        Server_data server{connector_factory};
        client = std::make_unique<Client_data>(
            connector_factory, Client_data::No_connect_helper::might_connect,
            [](auto&& /*unused*/, auto&& /*unused*/, auto&& /*unused*/) {});
    }

    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    EXPECT_FALSE(subscribe_find_service_cb_called);
}

TEST_F(RuntimeTest, SubscribeFindServiceWithStartedServerReturnsInterfaceAndInstanceId) {
    Server_data const server{connector_factory};

    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added));

    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());
    EXPECT_TRUE(subscribe_find_service_cb_called);

    wait_for_atomics(subscribe_find_service_cb_called);
}

TEST_F(
    RuntimeTest,
    SubscribeFindServiceWithStartedServerReturnsInterfaceAndInstanceIdAndCreatesClientConnector) {
    Server_data const server{connector_factory};
    std::optional<Client_data> client;

    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added))
        .WillOnce(DoAll(
            [this, &client](Service_interface const& /*ignore*/, Service_instance const& /*ignore*/,
                            Find_result_status /*ignore*/) { client.emplace(connector_factory); },
            Assign(&subscribe_find_service_cb_called, true)));

    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    wait_for_atomics(subscribe_find_service_cb_called);
}

TEST_F(
    RuntimeTest,
    SubscribeFindServiceWithLaterStartedServerReturnsInterfaceAndInstanceIdAndCreatesClientConnector) {
    std::atomic<bool> available{false};
    Client_connector_callbacks_mock client_mocks;
    std::optional<Client_connector::Uptr> client;

    {
        InSequence const is;
        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::added))
            .WillOnce(DoAll(
                [this, &client, &client_mocks, &available](Service_interface const& /*ignore*/,
                                                           Service_instance const& /*ignore*/,
                                                           Find_result_status /*ignore*/) {
                    // switch to Enabled_server_connector
                    EXPECT_CALL(client_mocks,
                                on_service_state_change(_, Service_state::available, _))
                        .WillOnce(Assign(&available, true));
                    client.emplace(connector_factory.create_client_connector(client_mocks));
                },
                Assign(&subscribe_find_service_cb_called, true)));

        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::deleted));
    }

    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    Server_data const server{connector_factory};

    wait_for_atomics(subscribe_find_service_cb_called, available);
    EXPECT_TRUE(client);
    EXPECT_CALL(client_mocks, on_service_state_change(_, Service_state::not_available, _));
}

TEST_F(RuntimeTest, SubscribeFindServiceCallbackDeletesClientOnServerDestructionWithoutDeadlock) {
    std::atomic<bool> available{false};
    Client_connector_callbacks_mock client_mocks;
    std::optional<Client_connector::Uptr> client;

    {
        InSequence const is;
        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::added));

        auto const destroy_client = [&client](auto const& /* interface*/, auto const& /* instance*/,
                                              auto const& /*status*/) { client.reset(); };

        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::deleted))
            .WillOnce(destroy_client);
    }

    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    {
        Server_data const server{connector_factory};

        EXPECT_CALL(client_mocks, on_service_state_change(_, Service_state::available, _))
            .WillOnce(Assign(&available, true));
        client.emplace(connector_factory.create_client_connector(client_mocks));

        wait_for_atomics(available);
        EXPECT_TRUE(client);
    }
    EXPECT_FALSE(client);
}

TEST_F(RuntimeTest, SubscribeFindServiceCallbackResetsItself) {
    Find_subscription find_subscription{};
    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added))
        .WillOnce([&find_subscription](auto const& /*ignore*/, auto const& /*ignore*/,
                                       auto /*ignore*/) { find_subscription.reset(); });
    find_subscription = connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());
    Server_data server{connector_factory};
}

TEST_F(RuntimeTest,
       SubscribeFindServiceCallbackResetsItselfAndCreatesServerToOwnSubscriptionWhichIsNotCalled) {
    Find_subscription find_subscription{};
    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added))
        .WillOnce([this, &find_subscription](auto const& /*ignore*/, auto const& /*ignore*/,
                                             auto /*ignore*/) {
            static int instance_number = 0;
            auto instance = Service_instance{std::to_string(instance_number++)};
            find_subscription.reset();
            Server_data server{this->connector_factory, this->connector_factory.get_configuration(),
                               instance};  // Replace with another factory
        });
    find_subscription = connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());
    Server_data server{connector_factory};
}

TEST_F(RuntimeTest, SubscribeFindServiceCallbackResetsItselfAndCreatesClient) {
    Find_subscription find_subscription{};
    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added))
        .WillOnce([this, &find_subscription](auto const& /*ignore*/, auto const& /*ignore*/,
                                             auto /*ignore*/) {
            find_subscription.reset();
            Client_data server{this->connector_factory};
        });
    find_subscription = connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());
    Server_data server{connector_factory};
}

TEST_F(RuntimeTest, SubscribeFindServiceCallbackIsResetWhileRuntimeCallsCallbacks) {
    Find_subscription find_subscription{};
    Find_result_change_callback_mock fsus_mock2;
    Find_subscription find_subscription2{};
    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added))
        .WillOnce([&find_subscription2](auto const& /*ignore*/, auto const& /*ignore*/,
                                        auto /*ignore*/) { find_subscription2.reset(); });
    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::deleted));
    find_subscription = connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());
    find_subscription2 = connector_factory.subscribe_find_service(fsus_mock2.AsStdFunction());
    Server_data server{connector_factory};
}

TEST_F(RuntimeTest,
       SubscribeFindServiceCallbackDestructionIsBlockedUntilCallbackExecutionIsFinished) {
    std::promise<void> callback_called;
    std::promise<void> proceed_in_callback;

    auto wait_for_subscription_reset = [&callback_called, &proceed_in_callback]() {
        auto const timeout = 10ms;
        // the other thread is continuing after setting the promise, thus taking this as the
        // start time to be sure 100ms really have elapsed on any server load
        callback_called.set_value();
        EXPECT_EQ(std::future_status::timeout, proceed_in_callback.get_future().wait_for(timeout));
    };

    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added))
        .WillOnce(InvokeWithoutArgs(wait_for_subscription_reset));

    auto find_subscription = connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    auto delete_subscription = std::async(
        std::launch::async, [&callback_called, &proceed_in_callback, &find_subscription]() {
            callback_called.get_future().wait();
            find_subscription.reset();
            proceed_in_callback.set_value();
        });

    Server_data server{connector_factory};

    delete_subscription.wait();
}

TEST_F(RuntimeTest, SubscribeFindServiceWithWildcardReturnsInterfaceAndInstanceId) {
    Server_data const server{connector_factory};

    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added));

    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    wait_for_atomics(subscribe_find_service_cb_called);
}

TEST_F(RuntimeTest, SubscribeFindServiceWithUnknownServiceInstanceDoesNotCallCallback) {
    Server_data const server{connector_factory};

    EXPECT_CALL(fsus_mock, Call(_, _, _)).Times(0);

    auto const find_subscription = connector_factory.subscribe_find_service(
        fsus_mock.AsStdFunction(), Service_instance{"not_to_be_known_by_anyone"});
}

TEST_F(RuntimeTest, DeletingSubscribeFindServiceHandleStopsReporting) {
    Find_result_change_callback_mock sanity_fsus_mock;
    std::atomic<bool> sanity_cb_called{false};

    {
        InSequence const is;
        EXPECT_CALL(sanity_fsus_mock,
                    Call(connector_factory.get_configuration().get_interface(),
                         connector_factory.get_instance(), Find_result_status::added))
            .WillOnce(Assign(&sanity_cb_called, true));

        EXPECT_CALL(sanity_fsus_mock,
                    Call(connector_factory.get_configuration().get_interface(),
                         connector_factory.get_instance(), Find_result_status::deleted));
    }

    auto const sanity_find_subscription =
        connector_factory.subscribe_find_service(sanity_fsus_mock.AsStdFunction());

    {
        EXPECT_CALL(fsus_mock, Call(_, _, _)).Times(0);

        auto const find_subscription =
            connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());
    }

    Server_data const server{connector_factory};

    wait_for_atomics(sanity_cb_called);
}

TEST_F(RuntimeTest, SubscribeFindServiceWithInvalidCallbackHasNoEffect) {
    Server_data const server{connector_factory};

    EXPECT_NO_FATAL_FAILURE(auto const find_subscription = connector_factory.subscribe_find_service(
                                Find_result_change_callback{}));
}

TEST_F(RuntimeTest, SubscribeFindServiceWithWildCardWithInvalidCallbackHasNoEffect) {
    Server_data const server{connector_factory};

    EXPECT_NO_FATAL_FAILURE(
        auto const find_subscription =
            connector_factory.subscribe_find_service_wildcard(Find_result_change_callback{}));
}

TEST_F(RuntimeTest, SubscribeFindServiceDuringRegisterServiceBridge) {
    auto const normal_subscription = connector_factory.subscribe_find_service(
        fsus_mock.AsStdFunction(), connector_factory.get_instance());

    auto instance_2 = Service_instance{"TestInstance2"};
    Find_subscription normal_subscription_2;

    Bridge_data* bridge_ptr{nullptr};
    auto bridge_sfs_expect = [&bridge_ptr](Bridge_data& bridge) { bridge_ptr = &bridge; };

    auto bridge_sfs_callback{[this, &instance_2, &normal_subscription_2, &bridge_ptr](
                                 auto cb, auto interface, auto ref_instance) {
        bridge_ptr->expect_another_subscribe_find_service(
            connector_factory.get_configuration().get_interface(),
            std::optional<Service_instance>(instance_2));

        normal_subscription_2 =
            connector_factory.subscribe_find_service(fsus_mock.AsStdFunction(), instance_2);

        bridge_ptr->expect_another_subscribe_find_service(
            connector_factory.get_configuration().get_interface(),
            std::optional<Service_instance>(instance_2));

        return Bridge_data::sfs_do_nothing()(cb, interface, ref_instance);
    }};

    Bridge_data bridge{Bridge_data::expect_then_bridge, Bridge_data::subscribe_find_service,
                       connector_factory, bridge_sfs_expect, bridge_sfs_callback};
    bridge.no_destroyed_check();
}

TEST_F(RuntimeTest, SubscribeFindServiceWithoutInstanceBeforeRegisterServiceBridge) {
    auto const normal_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    Subscribe_find_service_function_mock m_sfsf_mock;
    Request_service_function_mock m_rsf_mock;

    std::optional<Service_instance> opt_ref_instance_empty{};

    EXPECT_CALL(m_sfsf_mock, Call(_, connector_factory.get_configuration().get_interface(),
                                  opt_ref_instance_empty))
        .WillOnce(Return(ByMove(std::make_unique<Find_subscription_handle>())));

    auto const registration = connector_factory.register_service_bridge(
        Bridge_identity::make(*this), m_sfsf_mock.AsStdFunction(), m_rsf_mock.AsStdFunction());
}

TEST_F(RuntimeTest, RegisterServiceBridgeWithIncompleteCallbacksReturnsCallbacksMissing) {
    auto const callback_missing = score::MakeUnexpected(Construction_error::callback_missing);

    auto const callbacks =
        std::vector<std::pair<Subscribe_find_service_function, Request_service_function>>{
            {nullptr, nullptr},
            {sfsf_mock.AsStdFunction(), nullptr},
            {nullptr, rsf_mock.AsStdFunction()}};

    for (auto const& cb : callbacks) {
        auto const registration = connector_factory.register_service_bridge(
            Bridge_identity::make(*this), cb.first, cb.second);
        EXPECT_EQ(registration, callback_missing);
    }
}

TEST_F(RuntimeTest, RegisterServiceBridgeWitCallbacksReturnsRegistration) {
    score::Result<Service_bridge_registration> const registration =
        connector_factory.register_service_bridge(
            Bridge_identity::make(*this), sfsf_mock.AsStdFunction(), rsf_mock.AsStdFunction());
    ASSERT_TRUE(registration);
    EXPECT_TRUE(registration.value());
}

TEST_F(RuntimeTest, BridgeReceivesRequestServiceFunctionCallForUnknownService) {
    Bridge_data bridge{Bridge_data::bridge_then_expect, Bridge_data::request_service_function,
                       connector_factory};

    Client_data const client{connector_factory, Client_data::no_connect};
    EXPECT_TRUE(bridge.get_request_find_service_created());
}

TEST_F(RuntimeTest, BridgeDoesNotReceiveRequestServiceFunctionCallForKnownService) {
    Bridge_data bridge{Bridge_data::bridge_then_expect, Bridge_data::nothing, connector_factory};

    Server_data const server{connector_factory};
    Client_data const client{connector_factory};
    EXPECT_FALSE(bridge.get_request_find_service_created());
}

TEST_F(RuntimeTest,
       BridgeCreatesServerConnectorInRequestServiceFunctionCallbackAtClientConnectorCreation) {
    std::optional<Server_data> server;
    auto const create_server = [this, &server](
                                   Service_interface_configuration const& /*configuration*/,
                                   Service_instance const& /*instance*/) {
        server.emplace(connector_factory);
    };

    Bridge_data bridge{Bridge_data::bridge_then_expect, Bridge_data::nothing, connector_factory};
    bridge.expect_request_find_service(connector_factory.get_configuration(),
                                       connector_factory.get_instance(), create_server);

    Service_state_change_callback_mock state_change_callback;
    {
        InSequence const is;
        EXPECT_CALL(state_change_callback,
                    Call(_, Service_state::available, connector_factory.get_configuration()));
    }
    Client_data const client{connector_factory, Client_data::might_connect,
                             state_change_callback.AsStdFunction()};
}

TEST_F(RuntimeTest, SubscribeFindServiceWithWildcardReturnsBridgesMultipleFoundServices) {
    Bridge_data bridge{Bridge_data::bridge_then_expect, Bridge_data::nothing, connector_factory};
    bridge.expect_subscribe_find_service(connector_factory.get_configuration().get_interface(), {});

    auto const find_handle = connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());
    wait_for_atomics(bridge.get_subscribe_find_service_created());
    std::atomic<bool> fsus_with_input_called{false};
    for (auto const& result : input_find_result) {
        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(), result,
                                    Find_result_status::added))
            .WillOnce(Assign(&fsus_with_input_called, true));
        fsus_with_input_called = false;
        bridge.find_service(connector_factory.get_configuration().get_interface(), result,
                            Find_result_status::added);
        wait_for_atomics(fsus_with_input_called);
    }
}

TEST_F(RuntimeTest, BridgeDeletionWillReportFoundServicesDeleted) {
    auto const normal_subscription = connector_factory.subscribe_find_service(
        fsus_mock.AsStdFunction(), connector_factory.get_instance());

    Bridge_data bridge{Bridge_data::expect_then_bridge, Bridge_data::subscribe_find_service,
                       connector_factory};

    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added));

    bridge.find_service(connector_factory.get_configuration().get_interface(),
                        connector_factory.get_instance(), Find_result_status::added);

    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::deleted));

    bridge.no_destroyed_check();
}

TEST_F(RuntimeTest, BridgeDeletionOfNonAvailableService) {
    auto const normal_subscription = connector_factory.subscribe_find_service(
        fsus_mock.AsStdFunction(), connector_factory.get_instance());

    Bridge_data bridge{Bridge_data::expect_then_bridge, Bridge_data::subscribe_find_service,
                       connector_factory};

    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::deleted))
        .Times(0U);

    auto const interface = Service_interface{"TestInterface1", {9U, 1U}};
    bridge.find_service(interface, connector_factory.get_instance(), Find_result_status::deleted);

    bridge.no_destroyed_check();
}

TEST_F(RuntimeTest, BridgeDoesNotReceiveWildcardSubscription) {
    Bridge_data const bridge{Bridge_data::bridge_then_expect, Bridge_data::nothing,
                             connector_factory};

    EXPECT_CALL(fsus_mock, Call(_, _, _)).Times(0);
    auto const subscription =
        connector_factory.subscribe_find_service_wildcard(fsus_mock.AsStdFunction());

    EXPECT_FALSE(bridge.get_subscribe_find_service_created());
}

TEST_F(RuntimeTest, BridgeDoesNotReceiveWildcardSubscriptionsWhenItIsRegisteredLater) {
    EXPECT_CALL(fsus_mock, Call(_, _, _)).Times(0);
    auto const subscription =
        connector_factory.subscribe_find_service_wildcard(fsus_mock.AsStdFunction());

    Bridge_data const bridge{Bridge_data::bridge_then_expect, Bridge_data::nothing,
                             connector_factory};

    EXPECT_FALSE(bridge.get_subscribe_find_service_created());
}

TEST_F(RuntimeTest, BridgeFindResultsAreNotForwardedToWildcardSubscriptions) {
    Bridge_data const bridge{Bridge_data::bridge_then_expect, Bridge_data::subscribe_find_service,
                             connector_factory};

    auto const normal_subscription = connector_factory.subscribe_find_service(
        fsus_mock.AsStdFunction(), connector_factory.get_instance());

    Find_result_change_callback_mock wildcard_mock;
    auto const subscription =
        connector_factory.subscribe_find_service_wildcard(wildcard_mock.AsStdFunction());

    EXPECT_CALL(wildcard_mock, Call(_, _, _)).Times(0);
    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added));

    bridge.find_service(connector_factory.get_configuration().get_interface(),
                        connector_factory.get_instance(), Find_result_status::added);
}

TEST_F(RuntimeTest, BridgeFindResultsFromCacheAreNotForwardedToWildcardSubscription) {
    Bridge_data const bridge{Bridge_data::bridge_then_expect, Bridge_data::subscribe_find_service,
                             connector_factory};

    auto const normal_subscription = connector_factory.subscribe_find_service(
        fsus_mock.AsStdFunction(), connector_factory.get_instance());

    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added));

    bridge.find_service(connector_factory.get_configuration().get_interface(),
                        connector_factory.get_instance(), Find_result_status::added);

    Find_result_change_callback_mock wildcard_mock;
    EXPECT_CALL(wildcard_mock, Call(_, _, _)).Times(0);
    auto const subscription =
        connector_factory.subscribe_find_service_wildcard(wildcard_mock.AsStdFunction());
}

TEST_F(RuntimeTest, BridgeDeletionWillNotCallWildcardSubscription) {
    auto const normal_subscription = connector_factory.subscribe_find_service(
        fsus_mock.AsStdFunction(), connector_factory.get_instance());

    Find_result_change_callback_mock wildcard_mock;
    EXPECT_CALL(wildcard_mock, Call(_, _, _)).Times(0);
    auto const subscription =
        connector_factory.subscribe_find_service_wildcard(wildcard_mock.AsStdFunction());

    Bridge_data bridge{Bridge_data::expect_then_bridge, Bridge_data::subscribe_find_service,
                       connector_factory};

    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::added));
    bridge.find_service(connector_factory.get_configuration().get_interface(),
                        connector_factory.get_instance(), Find_result_status::added);
    EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                connector_factory.get_instance(), Find_result_status::deleted));

    bridge.no_destroyed_check();
}

TEST_F(RuntimeTest, FindSubscriptionsNoLoopBridgeOnly) {
    Bridge_data const bridge{Bridge_data::bridge_then_expect, Bridge_data::nothing,
                             connector_factory};

    auto const self_subscription = connector_factory.subscribe_find_service(
        fsus_mock.AsStdFunction(), connector_factory.get_instance(), bridge.get_identity());
}

TEST_F(RuntimeTest, FindSubscriptionsNoLoopBridgeOnly2Times) {
    Bridge_data const bridge{Bridge_data::bridge_then_expect, Bridge_data::nothing,
                             connector_factory};

    {
        auto const self_subscription = connector_factory.subscribe_find_service(
            fsus_mock.AsStdFunction(), connector_factory.get_instance(), bridge.get_identity());
    }

    {
        auto const self_subscription = connector_factory.subscribe_find_service(
            fsus_mock.AsStdFunction(), connector_factory.get_instance(), bridge.get_identity());
    }
}

TEST_F(RuntimeTest, FindSubscriptionsNoLoopBridgeSecondAndOther) {
    Bridge_data const bridge{Bridge_data::bridge_then_expect, Bridge_data::subscribe_find_service,
                             connector_factory};

    auto const other_subscription = connector_factory.subscribe_find_service(
        fsus_mock.AsStdFunction(), connector_factory.get_instance());

    ASSERT_TRUE(bridge.get_subscribe_find_service_created());
    {
        auto const self_subscription = connector_factory.subscribe_find_service(
            fsus_mock.AsStdFunction(), connector_factory.get_instance(), bridge.get_identity());

        ASSERT_TRUE(bridge.get_subscribe_find_service_created());
        ASSERT_FALSE(bridge.get_subscribe_find_service_destroyed());
    }
    EXPECT_FALSE(bridge.get_subscribe_find_service_destroyed());
}

TEST_F(RuntimeTest, FindSubscriptionsNoLoopTwoBridgesSecondAndOther) {
    Bridge_data first_bridge{Bridge_data::bridge_then_expect, Bridge_data::subscribe_find_service,
                             connector_factory};

    Bridge_data second_bridge{Bridge_data::bridge_then_expect, Bridge_data::subscribe_find_service,
                              connector_factory};

    auto const self_subscription_first_bridge = connector_factory.subscribe_find_service(
        fsus_mock.AsStdFunction(), connector_factory.get_instance(), first_bridge.get_identity());

    EXPECT_TRUE(second_bridge.get_subscribe_find_service_created());

    auto const self_subscription_second_bridge = connector_factory.subscribe_find_service(
        fsus_mock.AsStdFunction(), connector_factory.get_instance(), second_bridge.get_identity());

    EXPECT_TRUE(first_bridge.get_subscribe_find_service_created());
    EXPECT_TRUE(second_bridge.get_subscribe_find_service_created());

    EXPECT_FALSE(first_bridge.get_subscribe_find_service_destroyed());
    EXPECT_FALSE(second_bridge.get_subscribe_find_service_destroyed());

    first_bridge.no_destroyed_check();
    second_bridge.no_destroyed_check();
}

TEST_F(RuntimeTest, AllExceptionsAreCatchedWhileInformingSubscribersAtServerDestruction) {
    auto const find_subscription =
        connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());

    EXPECT_NO_THROW({
        InSequence const is;
        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::added));

        Server_data server{connector_factory};

        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), Find_result_status::deleted))
            .WillOnce([](auto, auto, auto) { throw std::exception(); });
    });
    wait_for_atomics(subscribe_find_service_cb_called);
}

TEST_F(RuntimeTest, ConstructDuplicateReturnsDuplicateServiceError) {
    Server_connector_callbacks_mock callbacks;
    auto const scd = connector_factory.create_server_connector_with_result(callbacks);
    ASSERT_TRUE(scd.has_value());

    Server_connector_callbacks_mock callbacks_2;
    auto const scd_2 = connector_factory.create_server_connector_with_result(callbacks_2);
    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    EXPECT_EQ(duplicate_service, scd_2);
}

TEST_F(RuntimeTest, ConstructDuplicateMultipleTimesReturnsDuplicateServiceError) {
    Server_connector_callbacks_mock callbacks;
    auto const scd = connector_factory.create_server_connector_with_result(callbacks);
    ASSERT_TRUE(scd.has_value());

    Server_connector_callbacks_mock callbacks_2;
    auto const scd_2 = connector_factory.create_server_connector_with_result(callbacks_2);

    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    ASSERT_EQ(duplicate_service, scd_2);

    Server_connector_callbacks_mock callbacks_3;
    auto const scd_3 = connector_factory.create_server_connector_with_result(callbacks_3);

    EXPECT_EQ(duplicate_service, scd_3);
}

TEST_F(RuntimeTest, CreatingServerConnectorDeletingItAndRecreatingReturnsValidServerConnector) {
    Server_connector_callbacks_mock callbacks;
    {
        auto const scd = connector_factory.create_server_connector_with_result(callbacks);
        ASSERT_TRUE(scd.has_value());
        Server_connector_callbacks_mock callbacks_2;
        score::Result<Disabled_server_connector::Uptr> const duplicate_service =
            score::MakeUnexpected(Construction_error::duplicate_service);
        EXPECT_EQ(duplicate_service,
                  connector_factory.create_server_connector_with_result(callbacks_2));
    }

    {
        auto const scd = connector_factory.create_server_connector_with_result(callbacks);
        ASSERT_TRUE(scd.has_value());
    }
}

TEST_F(RuntimeTest, ConnectorDestroyedAfterRuntimeDoesNotCrash) {
    Disabled_server_connector::Uptr connector{nullptr};
    {
        Connector_factory factory{connector_factory};
        Server_connector_callbacks_mock callbacks;
        connector = factory.create_server_connector(callbacks);
    }
}

TEST_F(RuntimeTest, SubscribeFindServiceWithStartedServerFindResultCallbackThrowsException) {
    Server_data const server{connector_factory};

    EXPECT_NO_THROW({
        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    connector_factory.get_instance(), _))
            .WillOnce(
                Invoke([this](Service_interface const& /*ignore*/,
                              Service_instance const& /*ignore*/, Find_result_status /*ignore*/) {
                    subscribe_find_service_cb_called = true;
                    throw std::runtime_error("fatal error");
                }));

        auto const find_subscription =
            connector_factory.subscribe_find_service(fsus_mock.AsStdFunction());
    });

    wait_for_atomics(subscribe_find_service_cb_called);
}

class RuntimeSubscribeFindServiceTest
    : public RuntimeTest,
      public WithParamInterface<Subscribe_find_service_param_tuple> {
   protected:
    Subscribe_find_service_params const m_param{GetParam()};
};

TEST_P(RuntimeSubscribeFindServiceTest, SubscribeFindServiceWithWildCardReportsServices) {
    auto const interfaces = create_service_interfaces(m_param.num_interfaces);
    auto const instances = create_instances(m_param.num_instances);

    auto subscriptions = create_wildcard_subscriptions(
        m_param.num_subscriptions_before_server_creation, fsus_mock, connector_factory);

    std::vector<std::unique_ptr<Server_data>> servers;
    for (auto const& interface : interfaces) {
        for (auto const& instance : instances) {
            EXPECT_CALL(fsus_mock,
                        Call(interface.get_interface(), instance, Find_result_status::added))
                .Times(static_cast<int>(m_param.get_total_subsciptions()));
            servers.emplace_back(
                std::make_unique<Server_data>(connector_factory, interface, instance));
            EXPECT_CALL(fsus_mock,
                        Call(interface.get_interface(), instance, Find_result_status::deleted))
                .Times(static_cast<int>(
                    m_param.get_total_subsciptions() *
                    static_cast<std::uint8_t>(!m_param.clear_subscriptions_first)));
        }
    }

    append(subscriptions,
           create_wildcard_subscriptions(m_param.num_subscriptions_after_server_creation, fsus_mock,
                                         connector_factory));
    if (m_param.clear_subscriptions_first) {
        subscriptions.clear();
    }
}

INSTANTIATE_TEST_SUITE_P(Wildcard, RuntimeSubscribeFindServiceTest,
                         Combine(Values(0, 1, 10), Values(1, 10), Values(0, 1, 10),
                                 Values(0, 1, 10), Bool()),
                         readable_test_names_wildcard);

TEST_F(RuntimeLegacySubscribeFindServiceTest, SubscriptionTriggersCallback) {
    std::size_t const interface_id{0};
    auto const interface = create_service_interface_configuration(interface_id);

    std::size_t const instance_id{0};
    auto const instance = create_service_instance(instance_id);

    Server_data const server{connector_factory, interface, instance};

    Find_result_container expected_container{instance};
    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(expected_container))).Times(1);
    (void)connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface.get_interface(), instance);
}

TEST_F(RuntimeLegacySubscribeFindServiceTest, SubscriptionTriggersCallbackForMultipleInstances) {
    std::size_t const interface_id{0};
    auto const interface = create_service_interface_configuration(interface_id);
    std::size_t const instance_id0{0};
    auto const instance0 = create_service_instance(instance_id0);
    std::size_t const instance_id1{1};
    auto const instance1 = create_service_instance(instance_id1);
    std::size_t const instance_id2{2};
    auto const instance2 = create_service_instance(instance_id2);

    Server_data const server0{connector_factory, interface, instance0};
    Server_data const server1{connector_factory, interface, instance1};
    Server_data const server2{connector_factory, interface, instance2};

    Find_result_container expected_container{instance0, instance1, instance2};

    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(expected_container))).Times(1);
    (void)connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface.get_interface(), {});
}

TEST_F(RuntimeLegacySubscribeFindServiceTest, SubscriptionTriggersCallbackForMultipleInterfaces) {
    std::size_t const interface_id0{0};
    auto const interface0 = create_service_interface_configuration(interface_id0);
    std::size_t const interface_id1{1};
    auto const interface1 = create_service_interface_configuration(interface_id1);
    std::size_t const interface_id2{2};
    auto const interface2 = create_service_interface_configuration(interface_id2);

    std::size_t const instance_id{0};
    auto const instance = create_service_instance(instance_id);

    Server_data const server0{connector_factory, interface0, instance};
    Server_data const server1{connector_factory, interface1, instance};
    Server_data const server2{connector_factory, interface2, instance};

    Find_result_container expected_container{instance};

    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(expected_container))).Times(3);
    (void)connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface0.get_interface(), instance);
    (void)connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface1.get_interface(), instance);
    (void)connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface2.get_interface(), instance);
}

TEST_F(RuntimeLegacySubscribeFindServiceTest, EachSubscriptionTriggersCallback) {
    std::size_t const interface_id{0};
    auto const interface = create_service_interface_configuration(interface_id);

    std::size_t const instance_id{0};
    auto const instance = create_service_instance(instance_id);

    Server_data const server{connector_factory, interface, instance};

    Find_result_container expected_container{instance};
    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(expected_container))).Times(2);

    auto subscription0 = connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface.get_interface(), instance);
    auto subscription1 = connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface.get_interface(), instance);
}

TEST_F(RuntimeLegacySubscribeFindServiceTest, ServerCreationTriggersCallback) {
    std::size_t const interface_id{0};
    auto const interface = create_service_interface_configuration(interface_id);
    std::size_t const instance_id{0};
    auto const instance = create_service_instance(instance_id);

    auto subscription = connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface.get_interface(), instance);
    Find_result_container const expected_container{instance};
    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(expected_container))).Times(1);

    Server_data const server{connector_factory, interface, instance};

    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(Find_result_container{}))).Times(1);
}

TEST_F(RuntimeLegacySubscribeFindServiceTest,
       EmptyInstanceSetWontTriggerCallbackAtServerDestruction) {
    auto const interface = create_service_interface_configuration(0);
    auto const instance = create_service_instance(0);

    auto subscription = connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface.get_interface(), instance);

    {
        EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(Find_result_container{instance})))
            .WillOnce([](Find_result_container const& result) {
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
                const_cast<Find_result_container&>(result).clear();
            });

        Server_data const server{connector_factory, interface, instance};

        EXPECT_CALL(legacy_fsus_mock, Call(_)).Times(0);
    }
}

TEST_F(RuntimeLegacySubscribeFindServiceTest, ServerCreationTriggersCallbackForMultipleInstances) {
    std::size_t const interface_id{0};
    auto const interface = create_service_interface_configuration(interface_id);

    std::size_t const instance_id0{0};
    auto const instance0 = create_service_instance(instance_id0);
    std::size_t const instance_id1{1};
    auto const instance1 = create_service_instance(instance_id1);

    auto subscription = connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface.get_interface(), {});

    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(Find_result_container{instance0})));

    std::vector<std::unique_ptr<Server_data>> servers;
    servers.emplace_back(std::make_unique<Server_data>(connector_factory, interface, instance0));
    // callback is invoked for the second server
    EXPECT_CALL(legacy_fsus_mock,
                Call(Multi_set_equal(Find_result_container{instance0, instance1})));
    servers.emplace_back(std::make_unique<Server_data>(connector_factory, interface, instance1));

    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(Find_result_container{instance1})));
    servers.erase(std::begin(servers));
    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(Find_result_container{})));
}

TEST_F(RuntimeLegacySubscribeFindServiceTest, ServerCreationTriggersCallbackForMultipleInterfaces) {
    std::size_t const interface_id0{0};
    auto const interface0 = create_service_interface_configuration(interface_id0);
    std::size_t const interface_id1{1};
    auto const interface1 = create_service_interface_configuration(interface_id1);

    std::size_t const instance_id{0};
    auto const instance = create_service_instance(instance_id);

    auto subscription0 = connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface0.get_interface(), {});
    auto subscription1 = connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface1.get_interface(), {});

    std::vector<std::unique_ptr<Server_data>> servers;
    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(Find_result_container{instance})));
    servers.emplace_back(std::make_unique<Server_data>(connector_factory, interface0, instance));

    // callback is invoked for second server
    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(Find_result_container{instance})));
    servers.emplace_back(std::make_unique<Server_data>(connector_factory, interface1, instance));

    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(Find_result_container{}))).Times(2);
}

TEST_F(RuntimeLegacySubscribeFindServiceTest, CreateServerTriggersCallbackForEachSubscription) {
    std::size_t const interface_id{0};
    auto const interface = create_service_interface_configuration(interface_id);

    std::size_t const instance_id{0};
    auto const instance = create_service_instance(instance_id);

    auto subscription0 = connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface.get_interface(), {});
    auto subscription1 = connector_factory.get_service_finder().subscribe_find_service(
        legacy_fsus_mock.AsStdFunction(), interface.get_interface(), {});

    Find_result_container expected_container{instance};
    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(expected_container))).Times(2);

    Server_data const server{connector_factory, interface, instance};
    EXPECT_CALL(legacy_fsus_mock, Call(Multi_set_equal(Find_result_container{}))).Times(2);
}

class RuntimeBridgeTest : public RuntimeTest, public WithParamInterface<Bridge_param_tuple> {
   protected:
    Bridge_param m_param = Bridge_param{GetParam()};
};

TEST_P(RuntimeBridgeTest, CreationOfClientsWillCallRequestServiceFunction) {
    auto const create_requests = [this](size_t num_requests) {
        return Client_data::create_clients(connector_factory, num_requests,
                                           Client_data::no_connect);
    };

    auto const post_check = [](Bridge_data const& /*bridge*/) {};

    bridge_test_template(create_requests, m_param, connector_factory,
                         Bridge_data::request_service_function, request_service_destroyed,
                         &Bridge_data::get_request_find_service_created, post_check);
}

TEST_P(RuntimeBridgeTest, SubscribeFindServiceWillCallCallbackOfBridges) {
    auto const create_requests = [this](size_t num_requests) {
        return create_subscriptions(num_requests, fsus_mock, connector_factory);
    };

    auto const post_check = [this](Bridge_data const& bridge) {
        InSequence const is;

        // immediately delete services to prevent usage of cache
        std::array<Find_result_status, 2> const stati = {Find_result_status::added,
                                                         Find_result_status::deleted};
        for (auto const& status : stati) {
            EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                        expected_find_result, status))
                .Times(m_param.get_total_requests());
            bridge.find_service(connector_factory.get_configuration().get_interface(),
                                expected_find_result, status);
        }
    };

    bridge_test_template(create_requests, m_param, connector_factory,
                         Bridge_data::subscribe_find_service, subscribe_find_service_destroyed,
                         &Bridge_data::get_subscribe_find_service_created, post_check);
}

TEST_P(RuntimeBridgeTest, SubscribeFindServiceWithCachedServicesUpdatesCacheOnBridgeChanges) {
    size_t check_called{0};
    auto const create_requests = [this, &check_called](size_t num_requests) {
        // prepare for deletion of found services when bridges are going to be destroyed
        if (1 == check_called && m_param.delete_and_recreate_bridges) {
            EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                        expected_find_result, Find_result_status::deleted))
                .Times(static_cast<int>(
                    m_param.get_total_requests() * m_param.num_bridges *
                    (1 + static_cast<std::uint8_t>(
                             !m_param.delete_and_recreate_requests &&
                             (Destruction_order::bridges_first == m_param.order)))));
        }

        // Callback is called last time, prepare callbacks for Bridge destruction
        if (1 == check_called && !m_param.delete_and_recreate_bridges &&
            !m_param.delete_and_recreate_requests &&
            Destruction_order::bridges_first == m_param.order) {
            EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                        expected_find_result, Find_result_status::deleted))
                .Times(static_cast<int>(m_param.get_total_requests() * m_param.num_bridges));
        }

        // requests are recreated and possible already reported Services by Bridges need to be
        // expected
        if (2 == check_called) {
            // expect already reported services
            EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                        expected_find_result, Find_result_status::added))
                .Times(static_cast<int>(m_param.get_total_requests() * m_param.num_bridges));
            if (Destruction_order::bridges_first == m_param.order) {
                // expect destruction of previously reported services and soon to be reported
                // services
                EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                            expected_find_result, Find_result_status::deleted))
                    .Times(static_cast<int>(
                        m_param.get_total_requests() * m_param.num_bridges *
                        (1 + static_cast<std::uint8_t>(!m_param.delete_and_recreate_bridges ||
                                                       m_param.delete_and_recreate_requests))));
            }
        }
        check_called++;
        return create_subscriptions(num_requests, fsus_mock, connector_factory);
    };

    auto const post_check = [this](Bridge_data const& bridge) {
        EXPECT_CALL(fsus_mock, Call(connector_factory.get_configuration().get_interface(),
                                    expected_find_result, Find_result_status::added))
            .Times(static_cast<int>(m_param.get_total_requests()));
        bridge.find_service(connector_factory.get_configuration().get_interface(),
                            expected_find_result, Find_result_status::added);
    };

    bridge_test_template(create_requests, m_param, connector_factory,
                         Bridge_data::subscribe_find_service, subscribe_find_service_destroyed,
                         &Bridge_data::get_subscribe_find_service_created, post_check);
}

INSTANTIATE_TEST_SUITE_P(Bridge, RuntimeBridgeTest,
                         Combine(Values(0, 1, 10), Values(0, 1, 10), Values(1, 10),
                                 Values(Destruction_order::requests_first,
                                        Destruction_order::bridges_first),
                                 Bool(), Bool()),
                         readable_test_names_bridge);

}  // namespace
