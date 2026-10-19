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
#include <cstdint>
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
    Event_config const event{"an_event", Mw_com_event_id{1U}, 1024U};

    EXPECT_EQ(sample_size(event), kSample_prefix_size + 1024U);
}

TEST_F(Gateway_ipc_binding_mw_com_test, sample_size_of_an_empty_event_is_the_prefix) {
    Event_config const event{"an_event", Mw_com_event_id{1U}, 0U};

    EXPECT_EQ(sample_size(event), kSample_prefix_size);
}

TEST_F(Gateway_ipc_binding_mw_com_test, sample_size_is_a_multiple_of_the_sample_alignment) {
    // A mw::com DataTypeMetaInfo::size must be an integer multiple of its alignment, so an odd
    // payload size is rounded up rather than rejected.
    Event_config const event{"an_event", Mw_com_event_id{1U}, 1U};

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
///          Default IDs as in mw_com_config.json.
Service_configs bridged_services(std::string instance_specifier, Role const role,
                                 std::vector<Event_config> events = {Event_config{
                                     "event_a", Mw_com_event_id{1U}, 128U}},
                                 std::size_t const max_sample_count = 4U,
                                 std::uint16_t const service_id = 6401U) {
    return Service_configs{Service_config{
        socom::Service_interface_identifier{std::string_view{"bridged_/test/ipc/BridgedService"},
                                            socom::Service_interface_identifier::Version{1U, 0U}},
        socom::Service_instance{std::string_view{"1"}}, std::move(instance_specifier), service_id,
        1U, role, std::move(events), max_sample_count}};
}

/// \brief True if the mw::com runtime knows the instance specifier
bool is_deployed(std::string const& instance_specifier) {
    auto specifier = score::mw::com::InstanceSpecifier::Create(std::string{instance_specifier});
    auto const identifiers = score::mw::com::runtime::ResolveInstanceIDs(specifier.value());
    return identifiers.has_value() && !identifiers.value().empty();
}

TEST_F(Gateway_ipc_binding_mw_com_test, create_server_adds_its_bridged_services_to_mw_com) {
    ASSERT_FALSE(is_deployed("bridged_ipc/added_by_the_server"));

    auto const server = create_server(
        *m_server_runtime, kSomeipd_specifier_second_pair,
        bridged_services("bridged_ipc/added_by_the_server", Role::provider,
                         {Event_config{"event_a", Mw_com_event_id{1U}, 128U}}, 4U, 0x7001U));

    ASSERT_NE(server, nullptr);
    EXPECT_TRUE(is_deployed("bridged_ipc/added_by_the_server"));
}

TEST_F(Gateway_ipc_binding_mw_com_test, create_client_adds_its_bridged_services_to_mw_com) {
    ASSERT_FALSE(is_deployed("bridged_ipc/added_by_the_client"));

    auto const client = create_client(
        *m_client_runtime, kSomeipd_specifier_second_pair,
        bridged_services("bridged_ipc/added_by_the_client", Role::consumer,
                         {Event_config{"event_a", Mw_com_event_id{1U}, 128U}}, 4U, 0x7002U));

    ASSERT_NE(client, nullptr);
    EXPECT_TRUE(is_deployed("bridged_ipc/added_by_the_client"));
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_bridged_service_without_events_is_rejected) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                                      bridged_services("bridged_ipc/bridged", Role::provider, {}));

    EXPECT_EQ(server, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_duplicate_event_name_is_rejected) {
    auto const server =
        create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::provider,
                                       {Event_config{"event_a", Mw_com_event_id{1U}, 128U},
                                        Event_config{"event_a", Mw_com_event_id{2U}, 128U}}));

    EXPECT_EQ(server, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_duplicate_event_id_is_rejected) {
    // mw::com terminates the process on a duplicate event ID.
    auto const server =
        create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::provider,
                                       {Event_config{"event_a", Mw_com_event_id{1U}, 128U},
                                        Event_config{"event_b", Mw_com_event_id{1U}, 128U}}));

    EXPECT_EQ(server, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_service_id_used_twice_is_rejected) {
    // SetBridgedEventSizes identifies the services by it.
    auto services = bridged_services("bridged_ipc/bridged", Role::consumer);
    services.push_back(bridged_services("bridged_ipc/shared_test", Role::consumer).front());

    auto const client =
        create_client(*m_client_runtime, kSomeipd_specifier_second_pair, std::move(services));

    EXPECT_EQ(client, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_consumer_without_sample_budget_is_rejected) {
    // Subscribe(0) would succeed but never hand a sample to the application.
    auto const client =
        create_client(*m_client_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::consumer,
                                       {Event_config{"event_a", Mw_com_event_id{1U}, 128U}}, 0U));

    EXPECT_EQ(client, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_sample_budget_beyond_the_sample_slots_is_rejected) {
    auto const client =
        create_client(*m_client_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::consumer,
                                       {Event_config{"event_a", Mw_com_event_id{1U}, 128U}},
                                       kNumber_of_sample_slots + 1U));

    EXPECT_EQ(client, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, an_event_missing_from_a_listed_deployment_is_rejected) {
    // Deployed in mw_com_config.json, so the binding doesn't add it.
    auto const client =
        create_client(*m_client_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::provider,
                                       {Event_config{"no_such_event", Mw_com_event_id{1U}, 128U}}));

    EXPECT_EQ(client, nullptr);
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

TEST_F(Gateway_ipc_binding_mw_com_test,
       the_client_rejects_more_events_than_set_bridged_event_sizes_carries) {
    std::vector<Event_config> events{};
    for (std::size_t index = 0U; index <= Bridged_event_sizes::max_size; ++index) {
        events.push_back(Event_config{"event_" + std::to_string(index),
                                      static_cast<Mw_com_event_id>(index), 128U});
    }

    auto const client =
        create_client(*m_client_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::consumer, std::move(events)));

    EXPECT_EQ(client, nullptr);
}

/// \brief SOCom definition of the bridged test service
socom::Server_service_interface_definition const kBridged_definition{
    socom::Service_interface_identifier{std::string_view{"bridged_/test/ipc/BridgedService"},
                                        socom::Service_interface_identifier::Version{1U, 0U}},
    socom::to_num_of_methods(0U), socom::to_num_of_events(2U)};
socom::Service_instance const kBridged_instance{std::string_view{"1"}};

std::vector<Event_config> both_events(std::size_t const max_payload_size) {
    return {Event_config{"event_a", Mw_com_event_id{1U}, max_payload_size},
            Event_config{"event_b", Mw_com_event_id{2U}, max_payload_size}};
}

/// \brief Sample size of an offered event, 0 if not offered
std::size_t offered_sample_size(std::string const& instance_specifier = "bridged_ipc/bridged",
                                std::string const& event_name = "event_a") {
    auto specifier = score::mw::com::InstanceSpecifier::Create(std::string{instance_specifier});
    auto const handles = score::mw::com::GenericProxy::FindService(specifier.value());
    if (!handles.has_value() || handles.value().empty()) {
        return 0U;
    }
    auto const proxy = score::mw::com::GenericProxy::Create(handles.value().front());
    if (!proxy.has_value()) {
        return 0U;
    }
    auto const events = proxy.value().GetEvents();
    auto const event = events.find(event_name);
    return (event == events.cend()) ? 0U : event->second.GetDataTypeSizeInfo().Size();
}

TEST_F(Gateway_ipc_binding_mw_com_test,
       set_bridged_event_sizes_takes_exactly_one_size_per_event_once) {
    // The providers only offer while the network side's SOCom service is available.
    Server_connector_with_callbacks network{*m_server_runtime, kBridged_definition,
                                            kBridged_instance};
    auto const server = create_server(
        *m_server_runtime, kSomeipd_specifier_second_pair,
        bridged_services("bridged_ipc/keyed_sizes", Role::provider, both_events(0U), 4U, 0x7003U));
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    // Act as gatewayd.
    auto specifier =
        score::mw::com::InstanceSpecifier::Create(std::string{kSomeipd_specifier_second_pair});
    auto const handles = Someipd_service_proxy::FindService(specifier.value());
    ASSERT_TRUE(handles.has_value());
    ASSERT_FALSE(handles.value().empty());
    auto proxy = Someipd_service_proxy::Create(handles.value().front());
    ASSERT_TRUE(proxy.has_value());

    // Each reply holds the only call slot. Drop it before the next call.
    auto const call = [&proxy](Bridged_event_sizes const& sizes) -> std::optional<bool> {
        auto const result = proxy.value().set_bridged_event_sizes(sizes);
        if (!result.has_value()) {
            return std::nullopt;
        }
        return *result.value();
    };

    // event_a has ID 1, event_b has ID 2.
    EXPECT_EQ(call(Bridged_event_sizes{{0x7003U, 1U, 100U}}), std::optional<bool>{false});
    EXPECT_EQ(call(Bridged_event_sizes{{0x7003U, 1U, 100U}, {0x7003U, 3U, 5000U}}),
              std::optional<bool>{false});
    EXPECT_EQ(call(Bridged_event_sizes{{0x7003U, 1U, 100U}, {0x7003U, 1U, 5000U}}),
              std::optional<bool>{false});
    EXPECT_EQ(call(Bridged_event_sizes{{0x7003U, 2U, 5000U}, {0x7003U, 1U, 100U}}),
              std::optional<bool>{true});
    EXPECT_EQ(call(Bridged_event_sizes{{0x7003U, 1U, 100U}, {0x7003U, 2U, 5000U}}),
              std::optional<bool>{true});
    EXPECT_EQ(call(Bridged_event_sizes{{0x7003U, 1U, 100U}, {0x7003U, 2U, 9000U}}),
              std::optional<bool>{false});

    EXPECT_EQ(offered_sample_size("bridged_ipc/keyed_sizes", "event_a"),
              sample_size(Event_config{"event_a", Mw_com_event_id{1U}, 100U}));
    EXPECT_EQ(offered_sample_size("bridged_ipc/keyed_sizes", "event_b"),
              sample_size(Event_config{"event_b", Mw_com_event_id{2U}, 5000U}));
}

TEST_F(Gateway_ipc_binding_mw_com_test, gatewayd_sends_the_sizes_again_when_someipd_restarts) {
    // Belongs to the someipd runtime, which outlives both someipd instances.
    Server_connector_with_callbacks network{*m_server_runtime, kBridged_definition,
                                            kBridged_instance};
    // Start someipd first. Otherwise gatewayd finds a stale SomeipdService of the previous test.
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

    // The restarted someipd only offers once gatewayd resends the sizes.
    application.expect_client_connected(kBridged_definition);
    auto restarted_server =
        create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::provider, both_events(0U)));
    ASSERT_NE(restarted_server, nullptr);
    ASSERT_TRUE(restarted_server->start().has_value());
    ASSERT_EQ(application.client_connected_promise.get_future().wait_for(kDiscovery_timeout),
              std::future_status::ready);

    EXPECT_EQ(offered_sample_size(), kSample_prefix_size + 8232U);

    // Order matters, see TearDown().
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

    // A re-offer would show up as an empty handle container.
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

    // SomeipdService is already offered. The sizes are sent before create_client() returns.

    // Same sizes: accepted, providers kept.
    {
        auto const client = create_client(
            *m_client_runtime, kSomeipd_specifier_second_pair,
            bridged_services("bridged_ipc/bridged", Role::consumer, both_events(8232U)));
        ASSERT_NE(client, nullptr);
        ASSERT_TRUE(client->is_connected());
        Client_connector_with_callbacks application{*m_client_runtime, kBridged_definition,
                                                    kBridged_instance};
    }

    // Larger event_a: rejected.
    auto client =
        create_client(*m_client_runtime, kSomeipd_specifier_second_pair,
                      bridged_services("bridged_ipc/bridged", Role::consumer,
                                       {Event_config{"event_a", Mw_com_event_id{1U}, 9000U},
                                        Event_config{"event_b", Mw_com_event_id{2U}, 8232U}}));
    ASSERT_NE(client, nullptr);
    EXPECT_FALSE(client->is_connected());

    EXPECT_EQ(offered_sample_size(), kSample_prefix_size + 8232U);
    EXPECT_EQ(gone_reports.load(), 0);

    // Order matters, see TearDown().
    client.reset();
    server.reset();
    EXPECT_EQ(gone.get_future().wait_for(kDiscovery_timeout), std::future_status::ready);
    EXPECT_TRUE(score::mw::com::GenericProxy::StopFindService(find_handle.value()).has_value());
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
