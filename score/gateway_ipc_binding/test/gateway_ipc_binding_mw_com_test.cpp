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

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "score/gateway_ipc_binding/impl/mw_com/someipd_service.hpp"
#include "score/mw/com/runtime.h"
#include "score/socom/runtime.hpp"
#include "util.hpp"

namespace score::gateway_ipc_binding::mw_com {
namespace {

using namespace std::chrono_literals;

/// \brief Instance specifier used by most tests, present in the test mw_com_config.json
constexpr char const* kSomeipd_specifier = "someipd/daemon";
/// \brief Second instance specifier, so that tests needing an isolated pair do not interfere
constexpr char const* kSomeipd_specifier_second_pair = "someipd/daemon_second_pair";

/// \brief mw::com service discovery is asynchronous, so state changes have to be awaited
constexpr auto kDiscovery_timeout = 10s;
constexpr auto kPoll_interval = 5ms;

/// \brief Wait until the client reports the expected connection state
/// \return The observed state, so that the caller can assert on it with a useful message
bool wait_for_connected(Gateway_ipc_binding_client const& client, bool const expected) {
    auto const deadline = std::chrono::steady_clock::now() + kDiscovery_timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (client.is_connected() == expected) {
            return expected;
        }
        std::this_thread::sleep_for(kPoll_interval);
    }
    return client.is_connected();
}

class Gateway_ipc_binding_mw_com_test : public ::testing::Test {
   protected:
    socom::Runtime::Uptr m_client_runtime = score::socom::create_runtime();
    socom::Runtime::Uptr m_server_runtime = score::socom::create_runtime();
};

TEST_F(Gateway_ipc_binding_mw_com_test, sample_size_is_prefix_plus_payload) {
    Event_config const event{"an_event", 1024U};

    EXPECT_EQ(sample_size(event), kSample_prefix_size + 1024U);
}

TEST_F(Gateway_ipc_binding_mw_com_test, sample_size_of_an_empty_event_is_the_prefix) {
    Event_config const event{"an_event", 0U};

    EXPECT_EQ(sample_size(event), kSample_prefix_size);
}

TEST_F(Gateway_ipc_binding_mw_com_test, sample_size_is_a_multiple_of_the_sample_alignment) {
    // A mw::com DataTypeMetaInfo::size must be an integer multiple of its alignment, so an odd
    // payload size is rounded up rather than rejected.
    Event_config const event{"an_event", 1U};

    EXPECT_EQ(sample_size(event) % kSample_alignment, 0U);
    EXPECT_EQ(sample_size(event), kSample_prefix_size + kSample_alignment);
}

TEST_F(Gateway_ipc_binding_mw_com_test, create_server_succeeds) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier_second_pair);

    EXPECT_NE(server, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, create_server_without_instance_specifier_fails) {
    auto const server = create_server(*m_server_runtime, "");

    EXPECT_EQ(server, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, create_client_succeeds_before_the_server_exists) {
    auto const client = create_client(*m_client_runtime, kSomeipd_specifier_second_pair);

    ASSERT_NE(client, nullptr);
    // Nothing offers the instance, so the peer must not be reported as connected.
    EXPECT_FALSE(client->is_connected());
}

TEST_F(Gateway_ipc_binding_mw_com_test, create_client_with_invalid_instance_specifier_fails) {
    // A specifier that is not in mw_com_config.json cannot be resolved.
    auto const client = create_client(*m_client_runtime, "no/such/instance");

    EXPECT_EQ(client, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, start_on_an_unconfigured_instance_specifier_fails) {
    // create_server() only checks that a specifier was given; a specifier that mw::com cannot
    // resolve is reported by start().
    auto const server = create_server(*m_server_runtime, "no/such/instance");
    ASSERT_NE(server, nullptr);

    EXPECT_FALSE(server->start().has_value());
}

TEST_F(Gateway_ipc_binding_mw_com_test, starting_the_server_twice_fails) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier_second_pair);
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    EXPECT_FALSE(server->start().has_value());
}

TEST_F(Gateway_ipc_binding_mw_com_test, client_reports_connected_once_the_server_is_started) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier);
    ASSERT_NE(server, nullptr);

    auto const client = create_client(*m_client_runtime, kSomeipd_specifier, {}, "test_client");
    ASSERT_NE(client, nullptr);
    EXPECT_FALSE(client->is_connected());

    ASSERT_TRUE(server->start().has_value());

    EXPECT_TRUE(wait_for_connected(*client, true));
}

TEST_F(Gateway_ipc_binding_mw_com_test, client_reports_connected_when_the_server_started_first) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier);
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto const client = create_client(*m_client_runtime, kSomeipd_specifier);
    ASSERT_NE(client, nullptr);

    EXPECT_TRUE(wait_for_connected(*client, true));
}

TEST_F(Gateway_ipc_binding_mw_com_test, add_service_configuration_extends_mw_com_runtime) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier);
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    constexpr std::string_view configuration{R"({
        "serviceTypes": [{
            "serviceTypeName": "/test/ipc/DynamicAddonService",
            "version": {"major": 1, "minor": 0},
            "bindings": [{"binding": "SHM", "serviceId": 6499}]
        }],
        "serviceInstances": [{
            "instanceSpecifier": "ipc/dynamic_addon",
            "serviceTypeName": "/test/ipc/DynamicAddonService",
            "version": {"major": 1, "minor": 0},
            "instances": [{"instanceId": 99, "asil-level": "QM", "binding": "SHM"}]
        }]
    })"};

    auto const client = create_client(*m_client_runtime, kSomeipd_specifier, {}, {}, configuration);
    ASSERT_NE(client, nullptr);
    ASSERT_TRUE(wait_for_connected(*client, true));

    auto specifier = score::mw::com::InstanceSpecifier::Create(std::string{"ipc/dynamic_addon"});
    ASSERT_TRUE(specifier.has_value());
    auto const identifiers = score::mw::com::runtime::ResolveInstanceIDs(specifier.value());
    ASSERT_TRUE(identifiers.has_value());
    EXPECT_EQ(identifiers.value().size(), 1U);
}

TEST_F(Gateway_ipc_binding_mw_com_test, add_service_configuration_rejects_malformed_json) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier_second_pair);
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto const client = create_client(*m_client_runtime, kSomeipd_specifier_second_pair, {}, {},
                                      std::string_view{"not json"});
    ASSERT_NE(client, nullptr);
    EXPECT_TRUE(wait_for_connected(*client, true));
}

TEST_F(Gateway_ipc_binding_mw_com_test, client_reports_disconnected_when_the_server_goes_away) {
    auto server = create_server(*m_server_runtime, kSomeipd_specifier);
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto const client = create_client(*m_client_runtime, kSomeipd_specifier);
    ASSERT_NE(client, nullptr);
    ASSERT_TRUE(wait_for_connected(*client, true));

    server.reset();

    EXPECT_FALSE(wait_for_connected(*client, false));
}

TEST_F(Gateway_ipc_binding_mw_com_test, client_reconnects_when_the_server_comes_back) {
    auto const client = create_client(*m_client_runtime, kSomeipd_specifier);
    ASSERT_NE(client, nullptr);

    {
        auto const server = create_server(*m_server_runtime, kSomeipd_specifier);
        ASSERT_NE(server, nullptr);
        ASSERT_TRUE(server->start().has_value());
        ASSERT_TRUE(wait_for_connected(*client, true));
    }
    ASSERT_FALSE(wait_for_connected(*client, false));

    auto const restarted_server = create_server(*m_server_runtime, kSomeipd_specifier);
    ASSERT_NE(restarted_server, nullptr);
    ASSERT_TRUE(restarted_server->start().has_value());

    EXPECT_TRUE(wait_for_connected(*client, true));
}

/// \brief One bridged service configuration, by default a valid one
/// \details Individual tests spoil exactly one aspect of it, so that what is under test is
///          obvious from the call site.
Service_configs bridged_services(std::string instance_specifier, Role const role,
                                 std::vector<Event_config> events = {Event_config{"event_a", 128U}},
                                 std::size_t const max_sample_count = 4U) {
    return Service_configs{Service_config{
        socom::Service_interface_identifier{std::string_view{"bridged_/test/ipc/BridgedService"},
                                            socom::Service_interface_identifier::Version{1U, 0U}},
        socom::Service_instance{std::string_view{"1"}}, std::move(instance_specifier), role,
        std::move(events), max_sample_count}};
}

TEST_F(Gateway_ipc_binding_mw_com_test, start_fails_when_a_bridged_service_is_not_deployed) {
    auto const server =
        create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/no_such_instance", Role::provider));
    ASSERT_NE(server, nullptr);

    // SomeipdService itself is fine, but a bridge that can never carry data is a startup error,
    // not something to discover later from missing traffic.
    EXPECT_FALSE(server->start().has_value());
}

TEST_F(Gateway_ipc_binding_mw_com_test,
       create_client_fails_when_a_bridged_service_is_not_deployed) {
    auto const client =
        create_client(*m_client_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/no_such_instance", Role::consumer));

    EXPECT_EQ(client, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_bridged_service_without_events_is_rejected) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                                      bridged_services("bridged_ipc/bridged", Role::provider, {}));
    ASSERT_NE(server, nullptr);

    EXPECT_FALSE(server->start().has_value());
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_duplicate_event_name_is_rejected) {
    auto const server = create_server(
        *m_server_runtime, kSomeipd_specifier_second_pair,
        bridged_services("bridged_ipc/bridged", Role::provider,
                         {Event_config{"event_a", 128U}, Event_config{"event_a", 128U}}));
    ASSERT_NE(server, nullptr);

    EXPECT_FALSE(server->start().has_value());
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_consumer_without_sample_budget_is_rejected) {
    // Subscribe(0) would succeed but never hand a sample to the application.
    auto const client = create_client(*m_client_runtime, kSomeipd_specifier_second_pair,
                                      bridged_services("bridged_ipc/bridged", Role::consumer,
                                                       {Event_config{"event_a", 128U}}, 0U));

    EXPECT_EQ(client, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, an_event_missing_from_the_deployment_is_rejected) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                                      bridged_services("bridged_ipc/bridged", Role::provider,
                                                       {Event_config{"no_such_event", 128U}}));
    ASSERT_NE(server, nullptr);

    EXPECT_FALSE(server->start().has_value());
}

TEST_F(Gateway_ipc_binding_mw_com_test, someipd_service_is_offered_before_the_bridged_services) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier,
                                      bridged_services("bridged_ipc/bridged", Role::provider));
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto const client =
        create_client(*m_client_runtime, kSomeipd_specifier,
                      bridged_services("bridged_ipc/bridged", Role::consumer), "test_client");
    ASSERT_NE(client, nullptr);

    // Peer liveness is independent of the bridged services: nothing offers the bridged service
    // locally, yet the peer's binding is up and accepting.
    EXPECT_TRUE(wait_for_connected(*client, true));
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_service_type_without_the_bridged_prefix_is_rejected) {
    auto services = bridged_services("bridged_ipc/bridged", Role::consumer);
    services.front().interface =
        socom::Service_interface_identifier{std::string_view{"/test/ipc/BridgedService"},
                                            socom::Service_interface_identifier::Version{1U, 0U}};

    auto const client =
        create_client(*m_client_runtime, kSomeipd_specifier_second_pair, std::move(services));

    EXPECT_EQ(client, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test,
       an_instance_specifier_without_the_bridged_prefix_is_rejected) {
    auto const client = create_client(*m_client_runtime, kSomeipd_specifier_second_pair,
                                      bridged_services("ipc/bridged", Role::consumer));

    EXPECT_EQ(client, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test,
       the_client_rejects_more_events_than_set_bridged_event_sizes_carries) {
    std::vector<Event_config> events{};
    for (std::size_t index = 0U; index <= Bridged_event_sizes::max_size; ++index) {
        events.push_back(Event_config{"event_" + std::to_string(index), 128U});
    }

    auto const client =
        create_client(*m_client_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::consumer, std::move(events)));

    EXPECT_EQ(client, nullptr);
}

/// \brief SOCom view of the bridged test service, as the applications on either side see it
socom::Server_service_interface_definition const kBridged_definition{
    socom::Service_interface_identifier{std::string_view{"bridged_/test/ipc/BridgedService"},
                                        socom::Service_interface_identifier::Version{1U, 0U}},
    socom::to_num_of_methods(0U), socom::to_num_of_events(2U)};
socom::Service_instance const kBridged_instance{std::string_view{"1"}};

/// \brief Both events of the bridged test service with the same size
std::vector<Event_config> both_events(std::size_t const max_payload_size) {
    return {Event_config{"event_a", max_payload_size}, Event_config{"event_b", max_payload_size}};
}

/// \brief Sample size of event_a of the offered bridged test service, 0 while it is not offered
std::size_t offered_sample_size() {
    auto specifier = score::mw::com::InstanceSpecifier::Create(std::string{"bridged_ipc/bridged"});
    auto const handles = score::mw::com::GenericProxy::FindService(specifier.value());
    if (!handles.has_value() || handles.value().empty()) {
        return 0U;
    }
    auto const proxy = score::mw::com::GenericProxy::Create(handles.value().front());
    if (!proxy.has_value()) {
        return 0U;
    }
    auto const events = proxy.value().GetEvents();
    auto const event = events.find("event_a");
    return (event == events.cend()) ? 0U : event->second.GetDataTypeSizeInfo().Size();
}

TEST_F(Gateway_ipc_binding_mw_com_test,
       set_bridged_event_sizes_accepts_only_the_first_valid_sizes) {
    auto const server =
        create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::provider, both_events(0U)));
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    // Called directly, as gatewayd would, to see what someipd answers.
    auto specifier =
        score::mw::com::InstanceSpecifier::Create(std::string{kSomeipd_specifier_second_pair});
    auto const handles = Someipd_service_proxy::FindService(specifier.value());
    ASSERT_TRUE(handles.has_value());
    ASSERT_FALSE(handles.value().empty());
    auto proxy = Someipd_service_proxy::Create(handles.value().front());
    ASSERT_TRUE(proxy.has_value());

    // The returned pointer holds the only call slot, so it is dropped before the next call.
    auto const call = [&proxy](Bridged_event_sizes const& sizes) -> std::optional<bool> {
        auto const result = proxy.value().set_bridged_event_sizes(sizes);
        if (!result.has_value()) {
            return std::nullopt;
        }
        return *result.value();
    };

    EXPECT_EQ(call(Bridged_event_sizes{}), std::optional<bool>{false});
    EXPECT_EQ(call(Bridged_event_sizes{8232U}), std::optional<bool>{false});
    EXPECT_EQ(call(Bridged_event_sizes{8232U, 8232U}), std::optional<bool>{true});
    EXPECT_EQ(call(Bridged_event_sizes{8232U, 8232U}), std::optional<bool>{true});
    EXPECT_EQ(call(Bridged_event_sizes{9000U, 8232U}), std::optional<bool>{false});
}

TEST_F(Gateway_ipc_binding_mw_com_test, gatewayd_sends_the_sizes_again_when_someipd_restarts) {
    // Lives on the someipd runtime, which outlives both someipd instances.
    Server_connector_with_callbacks network{*m_server_runtime, kBridged_definition,
                                            kBridged_instance};
    // someipd starts first, so that gatewayd never finds a SomeipdService the previous test left
    // behind in LoLa's service discovery.
    auto server =
        create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::provider, both_events(0U)));
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());
    auto client =
        create_client(*m_client_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::consumer, both_events(8232U)));
    ASSERT_NE(client, nullptr);
    Client_connector_with_callbacks application{*m_client_runtime, kBridged_definition,
                                                kBridged_instance};

    server.reset();
    ASSERT_EQ(application.client_disconnected_promise.get_future().wait_for(kDiscovery_timeout),
              std::future_status::ready);
    ASSERT_FALSE(wait_for_connected(*client, false));

    // The restarted someipd has no sizes of its own. It only offers if gatewayd sends them again.
    application.expect_client_connected(kBridged_definition);
    auto restarted_server =
        create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::provider, both_events(0U)));
    ASSERT_NE(restarted_server, nullptr);
    ASSERT_TRUE(restarted_server->start().has_value());
    ASSERT_EQ(application.client_connected_promise.get_future().wait_for(kDiscovery_timeout),
              std::future_status::ready);

    EXPECT_EQ(offered_sample_size(), kSample_prefix_size + 8232U);

    // The consumer goes first, see the bidirectional fixture's TearDown(). Waiting until the
    // bridged service is reported gone leaves LoLa's service discovery clean for the next test.
    std::atomic<int> gone_reports{0};
    std::promise<void> gone{};
    auto specifier = score::mw::com::InstanceSpecifier::Create(std::string{"bridged_ipc/bridged"});
    auto const find_handle = score::mw::com::GenericProxy::StartFindService(
        [&gone_reports, &gone](auto handles, auto) noexcept {
            if (handles.empty() && (++gone_reports == 1)) {
                gone.set_value();
            }
        },
        specifier.value());
    ASSERT_TRUE(find_handle.has_value());
    client.reset();
    restarted_server.reset();
    EXPECT_EQ(gone.get_future().wait_for(kDiscovery_timeout), std::future_status::ready);
    EXPECT_TRUE(score::mw::com::GenericProxy::StopFindService(find_handle.value()).has_value());
}

TEST_F(Gateway_ipc_binding_mw_com_test, someipd_keeps_its_sizes_when_gatewayd_restarts) {
    auto server =
        create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::provider, both_events(0U)));
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());
    Server_connector_with_callbacks network{*m_server_runtime, kBridged_definition,
                                            kBridged_instance};
    {
        auto const client = create_client(
            *m_client_runtime, kSomeipd_specifier_second_pair,
            bridged_services("bridged_ipc/bridged", Role::consumer, both_events(8232U)));
        ASSERT_NE(client, nullptr);
        ASSERT_TRUE(wait_for_connected(*client, true));
    }

    // A stop and re-offer would show up here as an empty handle container.
    std::atomic<int> gone_reports{0};
    std::promise<void> gone{};
    auto specifier = score::mw::com::InstanceSpecifier::Create(std::string{"bridged_ipc/bridged"});
    auto const find_handle = score::mw::com::GenericProxy::StartFindService(
        [&gone_reports, &gone](auto handles, auto) noexcept {
            if (handles.empty() && (++gone_reports == 1)) {
                gone.set_value();
            }
        },
        specifier.value());
    ASSERT_TRUE(find_handle.has_value());

    // SomeipdService is offered already, so LoLa runs the find-service handler, and with it
    // SetBridgedEventSizes, before create_client() returns. No need to wait.

    // gatewayd restarts with the same sizes: someipd accepts them and keeps its providers.
    {
        auto const client = create_client(
            *m_client_runtime, kSomeipd_specifier_second_pair,
            bridged_services("bridged_ipc/bridged", Role::consumer, both_events(8232U)));
        ASSERT_NE(client, nullptr);
        ASSERT_TRUE(client->is_connected());
        Client_connector_with_callbacks application{*m_client_runtime, kBridged_definition,
                                                    kBridged_instance};
    }

    // gatewayd restarts with event_a larger than someipd provides it: someipd rejects the sizes.
    auto client = create_client(
        *m_client_runtime, kSomeipd_specifier_second_pair,
        bridged_services("bridged_ipc/bridged", Role::consumer,
                         {Event_config{"event_a", 9000U}, Event_config{"event_b", 8232U}}));
    ASSERT_NE(client, nullptr);
    EXPECT_FALSE(client->is_connected());

    EXPECT_EQ(offered_sample_size(), kSample_prefix_size + 8232U);
    EXPECT_EQ(gone_reports.load(), 0);

    // The consumer goes first, see the bidirectional fixture's TearDown(). Waiting until the
    // bridged service is reported gone leaves LoLa's service discovery clean for the next test.
    client.reset();
    server.reset();
    EXPECT_EQ(gone.get_future().wait_for(kDiscovery_timeout), std::future_status::ready);
    EXPECT_TRUE(score::mw::com::GenericProxy::StopFindService(find_handle.value()).has_value());
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
