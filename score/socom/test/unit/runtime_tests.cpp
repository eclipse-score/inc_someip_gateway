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
 * SPDX-License-Identifier: Apache-2.0 AND CC0-1.0
 * AI Disclosure: Modifications for issue #84 were generated with OpenAI Codex
 * (model revision unavailable). These AI-generated modifications are offered under
 * CC0-1.0; pre-existing content retains Apache-2.0. Human review is pending.
 ********************************************************************************/

#include <algorithm>
#include <iterator>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include "score/socom/bridge_t.hpp"
#include "score/socom/client_connector.hpp"
#include "score/socom/clients_t.hpp"
#include "score/socom/connector_factory.hpp"
#include "score/socom/runtime.hpp"
#include "score/socom/server_t.hpp"
#include "score/socom/service_interface_definition.hpp"
#include "score/socom/single_connection_test_fixture.hpp"
#include "score/socom/socom_mocks.hpp"
#include "score/socom/utilities.hpp"

using namespace std::chrono_literals;

using ::testing::_;
using ::testing::Bool;
using ::testing::Combine;
using ::testing::InSequence;
using ::testing::TestParamInfo;
using ::testing::Values;
using ::testing::WithParamInterface;

namespace score::socom {

enum class Destruction_order { requests_first, bridges_first };
using Bridge_param_tuple = std::tuple<size_t, size_t, size_t, Destruction_order, bool, bool>;
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
    std::vector<Service_instance> const input_find_result{
        Service_instance{"first instance", Literal_tag{}},
        Service_instance{"second instance", Literal_tag{}}, connector_factory.get_instance()};
    Service_instance const expected_find_result{connector_factory.get_instance()};

    Request_service_function_mock rsf_mock;

    /// \brief Records the source-supported S-CORE test metadata for the duplicate-server
    ///        registration-key cases (issue #84).
    ///
    ///        DerivationTechnique: these cases are derived from the equivalence classes of the
    ///        registration key - the same (instance, service id, major) identity is a duplicate,
    ///        while a different component is a distinct key - and from the design of the
    ///        registration-key lifetime in Runtime_impl.
    ///
    ///        TestType and the PartiallyVerifies/FullyVerifies links are deliberately NOT recorded:
    ///        this bounded slice has no verified native requirement or interface identifier to link
    ///        to and inventing one is prohibited (same rationale as service_identifier_tests.cpp).
    void record_registration_key_metadata(std::string const& description) {
        RecordProperty("DerivationTechnique", "design-analysis,equivalence-classes");
        RecordProperty("Description", description);
    }
};

class RuntimeLegacySubscribeFindServiceTest : public RuntimeTest {};

// NOLINTNEXTLINE(readability-redundant-string-init)
MATCHER_P(Multi_set_equal, expected, "") {
    std::multiset<Service_instance> const expected_set{expected.cbegin(), expected.cend()};
    std::multiset<Service_instance> const input_set{arg.cbegin(), arg.cend()};
    return (input_set == expected_set);
}

TEST_F(RuntimeTest, RegisterServiceBridgeWithIncompleteCallbacksReturnsCallbacksMissing) {
    auto const callback_missing = score::MakeUnexpected(Construction_error::callback_missing);

    auto const registration =
        connector_factory.register_service_bridge(Bridge_identity::make(*this), nullptr);
    EXPECT_EQ(registration, callback_missing);
}

TEST_F(RuntimeTest, RegisterServiceBridgeWitCallbacksReturnsRegistration) {
    score::Result<Service_bridge_registration> const registration =
        connector_factory.register_service_bridge(Bridge_identity::make(*this),
                                                  rsf_mock.AsStdFunction());
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
                                   Service_interface_definition const& /*configuration*/,
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
                             state_change_callback.as_function()};
}

TEST_F(RuntimeTest, ConstructDuplicateReturnsDuplicateServiceError) {
    record_registration_key_metadata(
        "Runtime::make_server_connector refuses a second server connector that resolves to the "
        "same duplicate-server registration key (instance, service id, major) with "
        "Construction_error::duplicate_service.");
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
    record_registration_key_metadata(
        "Repeated Runtime::make_server_connector calls for the same registration key are each "
        "refused with Construction_error::duplicate_service while the first connector is alive.");
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
    record_registration_key_metadata(
        "Destroying a server connector erases its registration key, so the same (instance, "
        "service id, major) slot can be registered again with a valid connector.");
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

// Focused failing regression for issue #84: two server connectors registered for the same
// service id, major version and instance, differing only in the minor version, are the same
// duplicate-server registration key. On the pre-correction key (minor included) the second
// make_server_connector() succeeds, so this test fails; once the key ignores the minor version
// the second call returns Construction_error::duplicate_service and this test passes.
//
// Both connectors are constructed but never enabled, i.e. both remain disabled. The duplicate is
// therefore rejected at construction; the second connector never reaches
// Service_record::register_server_connector(), whose single-server precondition
// (SCORE_LANGUAGE_FUTURECPP_ASSERT(!m_server)) would otherwise be violated once both were enabled.
TEST_F(RuntimeTest, ConstructDuplicateDifferingOnlyInMinorVersionReturnsDuplicateServiceError) {
    record_registration_key_metadata(
        "Two connectors sharing instance, service id and major but differing only in the minor "
        "version are the same registration key, so the second construction is refused with "
        "duplicate_service (the minor version is not part of the key identity).");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_minor_2{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 2U}};
    Service_interface const interface_minor_3{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 3U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};

    Server_service_interface_definition const configuration_minor_2{
        interface_minor_2, to_num_of_methods(2U), to_num_of_events(3U)};
    Server_service_interface_definition const configuration_minor_3{
        interface_minor_3, to_num_of_methods(2U), to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto const first = runtime->make_server_connector(configuration_minor_2, instance,
                                                      create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration_minor_3, instance,
                                                       create_server_callbacks(callbacks_2));
    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    EXPECT_EQ(duplicate_service, second);
}

// The duplicate-server registration key tracks server connectors in every state, including the
// disabled state: Runtime_impl::make_server_connector inserts the key at construction and the
// connector is only removed from the key when it is destroyed. This test keeps two connectors
// disabled (neither is ever enabled) and pins that a second construction for exactly the same
// service id, major version and instance is rejected as duplicate_service while both are still
// disabled. The single-server precondition in Service_record::register_server_connector() is never
// reached because the duplicate is refused on the first (still disabled) registration.
TEST_F(RuntimeTest,
       ConstructDuplicateSameConfigurationWhileBothConnectorsDisabledReturnsDuplicateServiceError) {
    record_registration_key_metadata(
        "A second disabled connector with exactly the same configuration is refused with "
        "duplicate_service while the first disabled connector holds the registration key.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface{"TestInterface", Literal_tag{},
                                      Service_interface::Version{1U, 2U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration{interface, to_num_of_methods(2U),
                                                            to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto const first =
        runtime->make_server_connector(configuration, instance, create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());
    ASSERT_NE(nullptr, first.value().get());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration, instance,
                                                       create_server_callbacks(callbacks_2));
    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    EXPECT_EQ(duplicate_service, second);
}

// Equivalence-class counterpart of the tests above, still with both connectors disabled: a
// different major version is part of the key identity ("service identifier = service id + major",
// issue #84), so two disabled connectors that differ in the major version coexist and both
// constructions succeed.
TEST_F(RuntimeTest,
       ConstructDifferentMajorWhileBothConnectorsDisabledReturnsDistinctServerConnectors) {
    record_registration_key_metadata(
        "A different major version is part of the registration-key identity, so two disabled "
        "connectors that differ in the major version coexist and both constructions succeed.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_major_1{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 2U}};
    Service_interface const interface_major_2{"TestInterface", Literal_tag{},
                                              Service_interface::Version{2U, 2U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration_major_1{
        interface_major_1, to_num_of_methods(2U), to_num_of_events(3U)};
    Server_service_interface_definition const configuration_major_2{
        interface_major_2, to_num_of_methods(2U), to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto const first = runtime->make_server_connector(configuration_major_1, instance,
                                                      create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration_major_2, instance,
                                                       create_server_callbacks(callbacks_2));
    ASSERT_TRUE(second.has_value());

    EXPECT_NE(nullptr, first.value().get());
    EXPECT_NE(nullptr, second.value().get());
}

// Equivalence-class counterpart, still with both connectors disabled: the service instance is the
// primary key component, so two disabled connectors for the same interface/version but different
// instances coexist and both constructions succeed.
TEST_F(RuntimeTest,
       ConstructDifferentInstanceWhileBothConnectorsDisabledReturnsDistinctServerConnectors) {
    record_registration_key_metadata(
        "The instance is the primary registration-key component, so two disabled connectors for "
        "different instances but the same interface and version coexist.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface{"TestInterface", Literal_tag{},
                                      Service_interface::Version{1U, 2U}};
    Service_instance const instance_a{"TestInstanceA", Literal_tag{}};
    Service_instance const instance_b{"TestInstanceB", Literal_tag{}};
    Server_service_interface_definition const configuration{interface, to_num_of_methods(2U),
                                                            to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto const first = runtime->make_server_connector(configuration, instance_a,
                                                      create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration, instance_b,
                                                       create_server_callbacks(callbacks_2));
    ASSERT_TRUE(second.has_value());

    EXPECT_NE(nullptr, first.value().get());
    EXPECT_NE(nullptr, second.value().get());
}

// Equivalence-class counterpart, still with both connectors disabled: the service id is part of the
// key identity (the major-ignoring "service identifier = service id + major" of issue #84), so two
// disabled connectors for the same instance and version but different service ids coexist and both
// constructions succeed. This is the service-id counterpart of the distinct-major and
// distinct-instance cases above; no other runtime case varies only the service id.
TEST_F(RuntimeTest,
       ConstructDifferentServiceIdWhileBothConnectorsDisabledReturnsDistinctServerConnectors) {
    record_registration_key_metadata(
        "The service id is a registration-key component, so two disabled connectors for different "
        "service ids but the same instance and version coexist.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_a{"TestInterfaceA", Literal_tag{},
                                        Service_interface::Version{1U, 2U}};
    Service_interface const interface_b{"TestInterfaceB", Literal_tag{},
                                        Service_interface::Version{1U, 2U}};
    Service_instance const instance{"TestInstance", Literal_tag{}};
    Server_service_interface_definition const configuration_a{interface_a, to_num_of_methods(2U),
                                                              to_num_of_events(3U)};
    Server_service_interface_definition const configuration_b{interface_b, to_num_of_methods(2U),
                                                              to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto const first = runtime->make_server_connector(configuration_a, instance,
                                                      create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration_b, instance,
                                                       create_server_callbacks(callbacks_2));
    ASSERT_TRUE(second.has_value());

    EXPECT_NE(nullptr, first.value().get());
    EXPECT_NE(nullptr, second.value().get());
}

// Boundary-value counterpart, still with both connectors disabled: the minor version is not part of
// the registration key, so the two extreme minor values 0 and 65535 collapse to the same
// (instance, service id, major) identity and the second construction is refused with
// duplicate_service while neither connector is ever enabled.
TEST_F(RuntimeTest,
       ConstructDuplicateMinorBoundariesWhileBothConnectorsDisabledReturnsDuplicateServiceError) {
    record_registration_key_metadata(
        "The minor version is not part of the registration key, so two disabled connectors that "
        "differ only by the extreme minor values 0 and 65535 collide and the second construction "
        "is refused with duplicate_service.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_minor_min{"TestInterface", Literal_tag{},
                                                Service_interface::Version{1U, 0U}};
    Service_interface const interface_minor_max{"TestInterface", Literal_tag{},
                                                Service_interface::Version{1U, 65535U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration_minor_min{
        interface_minor_min, to_num_of_methods(2U), to_num_of_events(3U)};
    Server_service_interface_definition const configuration_minor_max{
        interface_minor_max, to_num_of_methods(2U), to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto const first = runtime->make_server_connector(configuration_minor_min, instance,
                                                      create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration_minor_max, instance,
                                                       create_server_callbacks(callbacks_2));
    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    EXPECT_EQ(duplicate_service, second);
}

// Direction-independence counterpart, still with both connectors disabled: the key ordering ignores
// the minor version entirely, so a duplicate whose minor version is smaller than the incumbent's is
// refused exactly like the ascending case.
TEST_F(RuntimeTest,
       ConstructDuplicateWithSmallerMinorWhileBothConnectorsDisabledReturnsDuplicateServiceError) {
    record_registration_key_metadata(
        "The registration key ignores the minor version independent of ordering, so a disabled "
        "connector with a smaller minor version than the incumbent is still refused with "
        "duplicate_service.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_minor_3{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 3U}};
    Service_interface const interface_minor_2{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 2U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration_minor_3{
        interface_minor_3, to_num_of_methods(2U), to_num_of_events(3U)};
    Server_service_interface_definition const configuration_minor_2{
        interface_minor_2, to_num_of_methods(2U), to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto const first = runtime->make_server_connector(configuration_minor_3, instance,
                                                      create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration_minor_2, instance,
                                                       create_server_callbacks(callbacks_2));
    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    EXPECT_EQ(duplicate_service, second);
}

// The disabled-server registration key is held for as long as the first (still disabled) connector
// lives: after an identical duplicate is refused, the slot stays occupied, so a third construction
// is refused too, while a connector for a different instance is still accepted. Nothing is ever
// enabled, so the single-server precondition is never reached.
TEST_F(RuntimeTest, ConstructDuplicateRepeatedlyWhileBothConnectorsDisabledKeepsSlotHeld) {
    record_registration_key_metadata(
        "While the first disabled connector lives, repeated duplicate constructions are refused "
        "with duplicate_service, yet a different instance still registers, showing the gate blocks "
        "only the occupied slot.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface{"TestInterface", Literal_tag{},
                                      Service_interface::Version{1U, 2U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Service_instance const other_instance{"OtherInterface", Literal_tag{}};
    Server_service_interface_definition const configuration{interface, to_num_of_methods(2U),
                                                            to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto const first =
        runtime->make_server_connector(configuration, instance, create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());

    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration, instance,
                                                       create_server_callbacks(callbacks_2));
    EXPECT_EQ(duplicate_service, second);

    Server_connector_callbacks_mock callbacks_3;
    auto const third = runtime->make_server_connector(configuration, instance,
                                                      create_server_callbacks(callbacks_3));
    EXPECT_EQ(duplicate_service, third);

    Server_connector_callbacks_mock callbacks_other;
    auto const other = runtime->make_server_connector(configuration, other_instance,
                                                      create_server_callbacks(callbacks_other));
    ASSERT_TRUE(other.has_value());
    EXPECT_NE(nullptr, other.value().get());
}

// Enabled-server variants of the duplicate-server registration-key regression. The registration key
// is held for the whole lifetime of a server connector, in every state: Runtime_impl::
// make_server_connector inserts it at construction and only the connector's destruction removes it
// (via the Final_action captured there). Enabling a connector therefore does not release the key,
// so a second construction for the same (instance, service id, major) is still refused while the
// first connector is enabled.
//
// The expectation is expressed by comparing the returned value against the genuine error result
// score::MakeUnexpected(Construction_error::duplicate_service) - the real error carried by the
// Result - rather than a bare boolean assertion, so a wrong or absent error is visible. On the
// pre-correction key (minor included) the second construction would instead succeed, and enabling
// it would reach Service_record::register_server_connector()'s single-server precondition
// (SCORE_LANGUAGE_FUTURECPP_ASSERT(!m_server)): a genuine termination, not a returned error.
TEST_F(RuntimeTest, ConstructDuplicateWhileFirstConnectorEnabledReturnsDuplicateServiceError) {
    record_registration_key_metadata(
        "The registration key is held while the first connector is enabled, so a duplicate "
        "construction for the same (instance, service id, major) is refused with "
        "duplicate_service.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface{"TestInterface", Literal_tag{},
                                      Service_interface::Version{1U, 2U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration{interface, to_num_of_methods(2U),
                                                            to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto first =
        runtime->make_server_connector(configuration, instance, create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());
    Enabled_server_connector::Uptr const first_enabled =
        Disabled_server_connector::enable(std::move(first).value());
    ASSERT_NE(nullptr, first_enabled.get());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration, instance,
                                                       create_server_callbacks(callbacks_2));
    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    EXPECT_EQ(duplicate_service, second);
}

// Enabled-server counterpart of the minor-only regression: with the first connector enabled, a
// second construction that shares service id, major version and instance but differs only in the
// minor version is still the same registration key and is refused with the genuine
// duplicate_service error result.
TEST_F(
    RuntimeTest,
    ConstructDuplicateDifferingOnlyInMinorVersionWhileFirstConnectorEnabledReturnsDuplicateServiceError) {
    record_registration_key_metadata(
        "The minor version is not part of the registration key, so a second construction differing "
        "only in the minor version is refused with duplicate_service even while the first "
        "connector "
        "is enabled.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_minor_2{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 2U}};
    Service_interface const interface_minor_3{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 3U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration_minor_2{
        interface_minor_2, to_num_of_methods(2U), to_num_of_events(3U)};
    Server_service_interface_definition const configuration_minor_3{
        interface_minor_3, to_num_of_methods(2U), to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto first = runtime->make_server_connector(configuration_minor_2, instance,
                                                create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());
    Enabled_server_connector::Uptr const first_enabled =
        Disabled_server_connector::enable(std::move(first).value());
    ASSERT_NE(nullptr, first_enabled.get());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration_minor_3, instance,
                                                       create_server_callbacks(callbacks_2));
    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    EXPECT_EQ(duplicate_service, second);
}

// Equivalence-class counterpart, still with the first connector enabled: the service instance is
// the primary key component, so an enabled connector for instance A does not prevent a connector
// for instance B of the same interface/version from being constructed.
TEST_F(RuntimeTest,
       ConstructDifferentInstanceWhileFirstConnectorEnabledReturnsDistinctServerConnectors) {
    record_registration_key_metadata(
        "An enabled connector does not block another instance's registration, because the instance "
        "is the primary registration-key component.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface{"TestInterface", Literal_tag{},
                                      Service_interface::Version{1U, 2U}};
    Service_instance const instance_a{"TestInstanceA", Literal_tag{}};
    Service_instance const instance_b{"TestInstanceB", Literal_tag{}};
    Server_service_interface_definition const configuration{interface, to_num_of_methods(2U),
                                                            to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto first = runtime->make_server_connector(configuration, instance_a,
                                                create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());
    Enabled_server_connector::Uptr const first_enabled =
        Disabled_server_connector::enable(std::move(first).value());
    ASSERT_NE(nullptr, first_enabled.get());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration, instance_b,
                                                       create_server_callbacks(callbacks_2));
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(nullptr, second.value().get());
}

// Equivalence-class counterpart, still with the first connector enabled: the service id is part of
// the key identity, so an enabled connector for service id A does not prevent a connector for
// service id B of the same instance and version from being constructed. This is the enabled
// service-id counterpart of the distinct-instance case above.
TEST_F(RuntimeTest,
       ConstructDifferentServiceIdWhileFirstConnectorEnabledReturnsDistinctServerConnectors) {
    record_registration_key_metadata(
        "An enabled connector does not block another service id's registration, because the "
        "service id is a registration-key component.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_a{"TestInterfaceA", Literal_tag{},
                                        Service_interface::Version{1U, 2U}};
    Service_interface const interface_b{"TestInterfaceB", Literal_tag{},
                                        Service_interface::Version{1U, 2U}};
    Service_instance const instance{"TestInstance", Literal_tag{}};
    Server_service_interface_definition const configuration_a{interface_a, to_num_of_methods(2U),
                                                              to_num_of_events(3U)};
    Server_service_interface_definition const configuration_b{interface_b, to_num_of_methods(2U),
                                                              to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto first = runtime->make_server_connector(configuration_a, instance,
                                                create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());
    Enabled_server_connector::Uptr const first_enabled =
        Disabled_server_connector::enable(std::move(first).value());
    ASSERT_NE(nullptr, first_enabled.get());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration_b, instance,
                                                       create_server_callbacks(callbacks_2));
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(nullptr, second.value().get());
}

// Boundary-value counterpart, still with the first connector enabled: the minor version is not part
// of the registration key, so the two extreme minor values 0 and 65535 collapse to the same
// (instance, service id, major) identity and the second construction is refused with
// duplicate_service while the first connector remains enabled.
TEST_F(RuntimeTest,
       ConstructDuplicateMinorBoundariesWhileFirstConnectorEnabledReturnsDuplicateServiceError) {
    record_registration_key_metadata(
        "The minor version is not part of the registration key, so a second construction that "
        "differs only by the extreme minor values 0 and 65535 is refused with duplicate_service "
        "even while the first connector is enabled.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_minor_min{"TestInterface", Literal_tag{},
                                                Service_interface::Version{1U, 0U}};
    Service_interface const interface_minor_max{"TestInterface", Literal_tag{},
                                                Service_interface::Version{1U, 65535U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration_minor_min{
        interface_minor_min, to_num_of_methods(2U), to_num_of_events(3U)};
    Server_service_interface_definition const configuration_minor_max{
        interface_minor_max, to_num_of_methods(2U), to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto first = runtime->make_server_connector(configuration_minor_min, instance,
                                                create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());
    Enabled_server_connector::Uptr const first_enabled =
        Disabled_server_connector::enable(std::move(first).value());
    ASSERT_NE(nullptr, first_enabled.get());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration_minor_max, instance,
                                                       create_server_callbacks(callbacks_2));
    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    EXPECT_EQ(duplicate_service, second);
}

// Direction-independence counterpart, still with the first connector enabled: the key ordering
// ignores the minor version entirely, so a duplicate whose minor version is smaller than the
// incumbent's is refused exactly like the ascending case while the first connector stays enabled.
TEST_F(RuntimeTest,
       ConstructDuplicateWithSmallerMinorWhileFirstConnectorEnabledReturnsDuplicateServiceError) {
    record_registration_key_metadata(
        "The registration key ignores the minor version independent of ordering, so a connector "
        "with "
        "a smaller minor version than the enabled incumbent is still refused with "
        "duplicate_service.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_minor_3{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 3U}};
    Service_interface const interface_minor_2{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 2U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration_minor_3{
        interface_minor_3, to_num_of_methods(2U), to_num_of_events(3U)};
    Server_service_interface_definition const configuration_minor_2{
        interface_minor_2, to_num_of_methods(2U), to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto first = runtime->make_server_connector(configuration_minor_3, instance,
                                                create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());
    Enabled_server_connector::Uptr const first_enabled =
        Disabled_server_connector::enable(std::move(first).value());
    ASSERT_NE(nullptr, first_enabled.get());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration_minor_2, instance,
                                                       create_server_callbacks(callbacks_2));
    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);
    EXPECT_EQ(duplicate_service, second);
}

// The enabled-server registration key is held for as long as the first connector lives: after an
// identical duplicate is refused, the slot stays occupied while the first connector is enabled, so
// a third construction is refused too, while a connector for a different instance is still
// accepted.
TEST_F(RuntimeTest, ConstructDuplicateRepeatedlyWhileFirstConnectorEnabledKeepsSlotHeld) {
    record_registration_key_metadata(
        "While the first enabled connector lives, repeated duplicate constructions are refused "
        "with "
        "duplicate_service, yet a different instance still registers, showing the gate blocks only "
        "the occupied slot.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface{"TestInterface", Literal_tag{},
                                      Service_interface::Version{1U, 2U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Service_instance const other_instance{"OtherInterface", Literal_tag{}};
    Server_service_interface_definition const configuration{interface, to_num_of_methods(2U),
                                                            to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto first =
        runtime->make_server_connector(configuration, instance, create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());
    Enabled_server_connector::Uptr const first_enabled =
        Disabled_server_connector::enable(std::move(first).value());
    ASSERT_NE(nullptr, first_enabled.get());

    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration, instance,
                                                       create_server_callbacks(callbacks_2));
    EXPECT_EQ(duplicate_service, second);

    Server_connector_callbacks_mock callbacks_3;
    auto const third = runtime->make_server_connector(configuration, instance,
                                                      create_server_callbacks(callbacks_3));
    EXPECT_EQ(duplicate_service, third);

    Server_connector_callbacks_mock callbacks_other;
    auto const other = runtime->make_server_connector(configuration, other_instance,
                                                      create_server_callbacks(callbacks_other));
    ASSERT_TRUE(other.has_value());
    EXPECT_NE(nullptr, other.value().get());
}

// Equivalence-class counterpart, still with the first connector enabled: a different major version
// is part of the key identity ("service identifier = service id + major", issue #84), so an enabled
// connector for major version 1 does not prevent a connector for major version 2 from being
// constructed. This is the enabled counterpart of the distinct-major disabled case.
TEST_F(RuntimeTest,
       ConstructDifferentMajorWhileFirstConnectorEnabledReturnsDistinctServerConnectors) {
    record_registration_key_metadata(
        "A different major version is part of the registration-key identity, so an enabled "
        "connector "
        "does not block a connector for a different major version.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_major_1{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 2U}};
    Service_interface const interface_major_2{"TestInterface", Literal_tag{},
                                              Service_interface::Version{2U, 2U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration_major_1{
        interface_major_1, to_num_of_methods(2U), to_num_of_events(3U)};
    Server_service_interface_definition const configuration_major_2{
        interface_major_2, to_num_of_methods(2U), to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    auto first = runtime->make_server_connector(configuration_major_1, instance,
                                                create_server_callbacks(callbacks));
    ASSERT_TRUE(first.has_value());
    Enabled_server_connector::Uptr const first_enabled =
        Disabled_server_connector::enable(std::move(first).value());
    ASSERT_NE(nullptr, first_enabled.get());

    Server_connector_callbacks_mock callbacks_2;
    auto const second = runtime->make_server_connector(configuration_major_2, instance,
                                                       create_server_callbacks(callbacks_2));
    ASSERT_TRUE(second.has_value());
    EXPECT_NE(nullptr, second.value().get());
}

// Destroying an enabled connector (which disables it) runs the Final_action that erases the
// registration key, so a later construction for the same (instance, service id, major) is valid
// again. This pins the lifetime/erase half of the key contract in the enabled state.
TEST_F(RuntimeTest, ConstructDuplicateAfterEnabledConnectorDestroyedReturnsValidServerConnector) {
    record_registration_key_metadata(
        "Destroying the enabled connector erases its registration key, so the same slot can be "
        "registered again, while the intermediate duplicate was refused.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface{"TestInterface", Literal_tag{},
                                      Service_interface::Version{1U, 2U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration{interface, to_num_of_methods(2U),
                                                            to_num_of_events(3U)};

    Server_connector_callbacks_mock callbacks;
    {
        auto first = runtime->make_server_connector(configuration, instance,
                                                    create_server_callbacks(callbacks));
        ASSERT_TRUE(first.has_value());
        Enabled_server_connector::Uptr const first_enabled =
            Disabled_server_connector::enable(std::move(first).value());
        ASSERT_NE(nullptr, first_enabled.get());

        Server_connector_callbacks_mock callbacks_2;
        auto const duplicate = runtime->make_server_connector(configuration, instance,
                                                              create_server_callbacks(callbacks_2));
        score::Result<Disabled_server_connector::Uptr> const duplicate_service =
            score::MakeUnexpected(Construction_error::duplicate_service);
        EXPECT_EQ(duplicate_service, duplicate);
    }  // first_enabled goes out of scope -> disabled -> registration key erased

    auto recreated =
        runtime->make_server_connector(configuration, instance, create_server_callbacks(callbacks));
    ASSERT_TRUE(recreated.has_value());
    EXPECT_NE(nullptr, recreated.value().get());
}

TEST_F(RuntimeTest, ConnectorDestroyedAfterRuntimeDoesNotCrash) {
    Disabled_server_connector::Uptr connector{nullptr};
    {
        Connector_factory factory{connector_factory};
        Server_connector_callbacks_mock callbacks;
        connector = factory.create_server_connector(callbacks);
    }
}

// --- Registration-slot lifetime: reuse after destruction and slot isolation ---------------------
//
// The duplicate-server registration key is held for the whole lifetime of a server connector and is
// erased by the Final_action installed in Runtime_impl::make_server_connector() when the connector
// is destroyed, in every connector state. These tests pin the two lifetime halves of the key
// contract: (a) destroying a connector frees exactly its (instance, service id, major) slot for
// reuse, and (b) destroying one slot never releases a different major/instance slot.
//
// Because the key deliberately ignores the minor version (issue #84; "service identifier = service
// id + major"), a freed slot can be reused by a connector that carries a changed minor version.

// Disabled connector lifetime: destroying the still-disabled connector erases its registration key,
// so the freed slot can be reused by a connector with a changed minor version. While the first
// connector is alive, the same slot with a different minor version is still refused as
// duplicate_service.
TEST_F(
    RuntimeTest,
    ConstructDuplicateAfterDisabledConnectorDestroyedWithChangedMinorVersionReturnsValidServerConnector) {
    record_registration_key_metadata(
        "A destroyed disabled connector frees its registration key, so the same slot can be reused "
        "with a changed minor version; while the connector is alive the changed minor is refused "
        "as "
        "duplicate_service.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_minor_2{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 2U}};
    Service_interface const interface_minor_3{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 3U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration_minor_2{
        interface_minor_2, to_num_of_methods(2U), to_num_of_events(3U)};
    Server_service_interface_definition const configuration_minor_3{
        interface_minor_3, to_num_of_methods(2U), to_num_of_events(3U)};

    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);

    {
        Server_connector_callbacks_mock callbacks;
        auto const first = runtime->make_server_connector(configuration_minor_2, instance,
                                                          create_server_callbacks(callbacks));
        ASSERT_TRUE(first.has_value());

        // Same slot, changed minor version -> still a duplicate while the first connector is alive.
        Server_connector_callbacks_mock callbacks_2;
        auto const duplicate = runtime->make_server_connector(configuration_minor_3, instance,
                                                              create_server_callbacks(callbacks_2));
        EXPECT_EQ(duplicate_service, duplicate);
    }  // first destroyed -> its registration key is erased

    Server_connector_callbacks_mock callbacks_3;
    auto const recreated = runtime->make_server_connector(configuration_minor_3, instance,
                                                          create_server_callbacks(callbacks_3));
    ASSERT_TRUE(recreated.has_value());
    EXPECT_NE(nullptr, recreated.value().get());
}

// Enabled connector lifetime: destroying an enabled connector disables it and then erases its
// registration key, so the freed slot can be reused by a connector with a changed minor version.
// While the enabled connector is alive, the same slot with a different minor version is refused as
// duplicate_service.
TEST_F(
    RuntimeTest,
    ConstructDuplicateAfterEnabledConnectorDestroyedWithChangedMinorVersionReturnsValidServerConnector) {
    record_registration_key_metadata(
        "A destroyed enabled connector frees its registration key, so the same slot can be reused "
        "with a changed minor version; while the connector is alive the changed minor is refused "
        "as "
        "duplicate_service.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_minor_2{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 2U}};
    Service_interface const interface_minor_3{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 3U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration_minor_2{
        interface_minor_2, to_num_of_methods(2U), to_num_of_events(3U)};
    Server_service_interface_definition const configuration_minor_3{
        interface_minor_3, to_num_of_methods(2U), to_num_of_events(3U)};

    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);

    {
        Server_connector_callbacks_mock callbacks;
        auto first = runtime->make_server_connector(configuration_minor_2, instance,
                                                    create_server_callbacks(callbacks));
        ASSERT_TRUE(first.has_value());
        Enabled_server_connector::Uptr const first_enabled =
            Disabled_server_connector::enable(std::move(first).value());
        ASSERT_NE(nullptr, first_enabled.get());

        // Same slot, changed minor version -> still a duplicate while the first connector is
        // enabled.
        Server_connector_callbacks_mock callbacks_2;
        auto const duplicate = runtime->make_server_connector(configuration_minor_3, instance,
                                                              create_server_callbacks(callbacks_2));
        EXPECT_EQ(duplicate_service, duplicate);
    }  // first_enabled destroyed -> its registration key is erased

    Server_connector_callbacks_mock callbacks_3;
    auto const recreated = runtime->make_server_connector(configuration_minor_3, instance,
                                                          create_server_callbacks(callbacks_3));
    ASSERT_TRUE(recreated.has_value());
    EXPECT_NE(nullptr, recreated.value().get());
}

// Connector destruction is scoped to the destroyed connector's own registration slot: destroying
// the connector for major version 1 frees only that slot, while the still-alive connector for major
// version 2 keeps its slot occupied. Re-creating major version 1 succeeds; duplicating major
// version 2 is still refused. Major version is an identity component ("service identifier = service
// id + major").
TEST_F(RuntimeTest, DestroyingOneMajorSlotKeepsOtherMajorSlotOccupied) {
    record_registration_key_metadata(
        "Destroying a connector frees only its own major-version slot; a different major-version "
        "slot stays occupied and is still refused as duplicate.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_major_1{"TestInterface", Literal_tag{},
                                              Service_interface::Version{1U, 2U}};
    Service_interface const interface_major_2{"TestInterface", Literal_tag{},
                                              Service_interface::Version{2U, 2U}};
    Service_instance const instance{"TestInterface", Literal_tag{}};
    Server_service_interface_definition const configuration_major_1{
        interface_major_1, to_num_of_methods(2U), to_num_of_events(3U)};
    Server_service_interface_definition const configuration_major_2{
        interface_major_2, to_num_of_methods(2U), to_num_of_events(3U)};

    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);

    // The major-2 connector stays alive for the whole test; only the major-1 connector is
    // destroyed.
    Server_connector_callbacks_mock callbacks_major_2;
    auto const major_2 = runtime->make_server_connector(configuration_major_2, instance,
                                                        create_server_callbacks(callbacks_major_2));
    ASSERT_TRUE(major_2.has_value());

    {
        Server_connector_callbacks_mock callbacks_major_1;
        auto const major_1 = runtime->make_server_connector(
            configuration_major_1, instance, create_server_callbacks(callbacks_major_1));
        ASSERT_TRUE(major_1.has_value());

        // Both slots are occupied: duplicating major 2 is refused.
        Server_connector_callbacks_mock callbacks_duplicate;
        auto const duplicate_major_2 = runtime->make_server_connector(
            configuration_major_2, instance, create_server_callbacks(callbacks_duplicate));
        EXPECT_EQ(duplicate_service, duplicate_major_2);
    }  // major_1 destroyed -> only the major-1 registration key is erased

    // The major-1 slot is free again.
    Server_connector_callbacks_mock callbacks_major_1_again;
    auto const recreated_major_1 = runtime->make_server_connector(
        configuration_major_1, instance, create_server_callbacks(callbacks_major_1_again));
    ASSERT_TRUE(recreated_major_1.has_value());
    EXPECT_NE(nullptr, recreated_major_1.value().get());

    // The major-2 slot is still occupied.
    Server_connector_callbacks_mock callbacks_duplicate_again;
    auto const duplicate_major_2_again = runtime->make_server_connector(
        configuration_major_2, instance, create_server_callbacks(callbacks_duplicate_again));
    EXPECT_EQ(duplicate_service, duplicate_major_2_again);
}

// Connector destruction is scoped to the destroyed connector's own registration slot: destroying
// the connector for instance A frees only that slot, while the still-alive connector for instance B
// keeps its slot occupied. Re-creating instance A succeeds; duplicating instance B is still
// refused. The service instance is the primary identity component.
TEST_F(RuntimeTest, DestroyingOneInstanceSlotKeepsOtherInstanceSlotOccupied) {
    record_registration_key_metadata(
        "Destroying a connector frees only its own instance slot; a different instance slot stays "
        "occupied and is still refused as duplicate.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface{"TestInterface", Literal_tag{},
                                      Service_interface::Version{1U, 2U}};
    Service_instance const instance_a{"TestInstanceA", Literal_tag{}};
    Service_instance const instance_b{"TestInstanceB", Literal_tag{}};
    Server_service_interface_definition const configuration{interface, to_num_of_methods(2U),
                                                            to_num_of_events(3U)};

    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);

    // The instance-B connector stays alive for the whole test; only the instance-A connector is
    // destroyed.
    Server_connector_callbacks_mock callbacks_instance_b;
    auto const instance_b_connector = runtime->make_server_connector(
        configuration, instance_b, create_server_callbacks(callbacks_instance_b));
    ASSERT_TRUE(instance_b_connector.has_value());

    {
        Server_connector_callbacks_mock callbacks_instance_a;
        auto const instance_a_connector = runtime->make_server_connector(
            configuration, instance_a, create_server_callbacks(callbacks_instance_a));
        ASSERT_TRUE(instance_a_connector.has_value());

        // Both slots are occupied: duplicating instance B is refused.
        Server_connector_callbacks_mock callbacks_duplicate;
        auto const duplicate_instance_b = runtime->make_server_connector(
            configuration, instance_b, create_server_callbacks(callbacks_duplicate));
        EXPECT_EQ(duplicate_service, duplicate_instance_b);
    }  // instance_a_connector destroyed -> only the instance-A registration key is erased

    // The instance-A slot is free again.
    Server_connector_callbacks_mock callbacks_instance_a_again;
    auto const recreated_instance_a = runtime->make_server_connector(
        configuration, instance_a, create_server_callbacks(callbacks_instance_a_again));
    ASSERT_TRUE(recreated_instance_a.has_value());
    EXPECT_NE(nullptr, recreated_instance_a.value().get());

    // The instance-B slot is still occupied.
    Server_connector_callbacks_mock callbacks_duplicate_again;
    auto const duplicate_instance_b_again = runtime->make_server_connector(
        configuration, instance_b, create_server_callbacks(callbacks_duplicate_again));
    EXPECT_EQ(duplicate_service, duplicate_instance_b_again);
}

// Connector destruction is scoped to the destroyed connector's own registration slot: destroying
// the connector for service id A frees only that slot, while the still-alive connector for service
// id B (same instance and version) keeps its slot occupied. Re-creating service id A succeeds;
// duplicating service id B is still refused. Service id is the second identity component ("service
// identifier = service id + major"), completing the slot-isolation matrix alongside the major and
// instance cases above.
TEST_F(RuntimeTest, DestroyingOneServiceIdSlotKeepsOtherServiceIdSlotOccupied) {
    record_registration_key_metadata(
        "Destroying a connector frees only its own service-id slot; a different service-id slot "
        "stays occupied and is still refused as duplicate.");
    Runtime::Uptr runtime = create_runtime();

    Service_interface const interface_a{"TestInterfaceA", Literal_tag{},
                                        Service_interface::Version{1U, 2U}};
    Service_interface const interface_b{"TestInterfaceB", Literal_tag{},
                                        Service_interface::Version{1U, 2U}};
    Service_instance const instance{"TestInstance", Literal_tag{}};
    Server_service_interface_definition const configuration_a{interface_a, to_num_of_methods(2U),
                                                              to_num_of_events(3U)};
    Server_service_interface_definition const configuration_b{interface_b, to_num_of_methods(2U),
                                                              to_num_of_events(3U)};

    score::Result<Disabled_server_connector::Uptr> const duplicate_service =
        score::MakeUnexpected(Construction_error::duplicate_service);

    // The service-id-B connector stays alive for the whole test; only the service-id-A connector is
    // destroyed.
    Server_connector_callbacks_mock callbacks_service_id_b;
    auto const service_id_b_connector = runtime->make_server_connector(
        configuration_b, instance, create_server_callbacks(callbacks_service_id_b));
    ASSERT_TRUE(service_id_b_connector.has_value());

    {
        Server_connector_callbacks_mock callbacks_service_id_a;
        auto const service_id_a_connector = runtime->make_server_connector(
            configuration_a, instance, create_server_callbacks(callbacks_service_id_a));
        ASSERT_TRUE(service_id_a_connector.has_value());

        // Both slots are occupied: duplicating service id B is refused.
        Server_connector_callbacks_mock callbacks_duplicate;
        auto const duplicate_service_id_b = runtime->make_server_connector(
            configuration_b, instance, create_server_callbacks(callbacks_duplicate));
        EXPECT_EQ(duplicate_service, duplicate_service_id_b);
    }  // service_id_a_connector destroyed -> only the service-id-A registration key is erased

    // The service-id-A slot is free again.
    Server_connector_callbacks_mock callbacks_service_id_a_again;
    auto const recreated_service_id_a = runtime->make_server_connector(
        configuration_a, instance, create_server_callbacks(callbacks_service_id_a_again));
    ASSERT_TRUE(recreated_service_id_a.has_value());
    EXPECT_NE(nullptr, recreated_service_id_a.value().get());

    // The service-id-B slot is still occupied.
    Server_connector_callbacks_mock callbacks_duplicate_again;
    auto const duplicate_service_id_b_again = runtime->make_server_connector(
        configuration_b, instance, create_server_callbacks(callbacks_duplicate_again));
    EXPECT_EQ(duplicate_service, duplicate_service_id_b_again);
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

INSTANTIATE_TEST_SUITE_P(RequestsAfterBridgeCreation, RuntimeBridgeTest,
                         Combine(Values(0), Values(0, 1), Values(1, 10),
                                 Values(Destruction_order::requests_first,
                                        Destruction_order::bridges_first),
                                 Bool(), Bool()),
                         readable_test_names_bridge);

}  // namespace score::socom
