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

#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include "score/socom/runtime.hpp"

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

TEST_F(Gateway_ipc_binding_mw_com_test, sample_size_is_prefix_plus_header_plus_payload) {
    Event_config const event{"an_event", 16U, 1024U};

    EXPECT_EQ(sample_size(event), kSample_prefix_size + 16U + 1024U);
}

TEST_F(Gateway_ipc_binding_mw_com_test, sample_size_of_a_headerless_empty_event_is_the_prefix) {
    Event_config const event{"an_event", 0U, 0U};

    EXPECT_EQ(sample_size(event), kSample_prefix_size);
}

TEST_F(Gateway_ipc_binding_mw_com_test, sample_size_is_a_multiple_of_the_sample_alignment) {
    // A mw::com DataTypeMetaInfo::size must be an integer multiple of its alignment, so an odd
    // payload size is rounded up rather than rejected.
    Event_config const event{"an_event", 16U, 1U};

    EXPECT_EQ(sample_size(event) % kSample_alignment, 0U);
    EXPECT_EQ(sample_size(event), kSample_prefix_size + 16U + kSample_alignment);
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
Service_configs bridged_services(std::string instance_specifier, Role const role,
                                 std::vector<Event_config> events = {Event_config{"event_a", 16U,
                                                                                  128U}},
                                 std::size_t const max_sample_count = 4U) {
    return Service_configs{Service_config{
        socom::Service_interface_identifier{std::string_view{"/test/ipc/BridgedService"},
                                            socom::Service_interface_identifier::Version{1U, 0U}},
        socom::Service_instance{std::string_view{"1"}}, std::move(instance_specifier), role,
        std::move(events), max_sample_count}};
}

TEST_F(Gateway_ipc_binding_mw_com_test, start_fails_when_a_bridged_service_is_not_deployed) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                                      bridged_services("ipc/no_such_instance", Role::provider));
    ASSERT_NE(server, nullptr);

    // SomeipdService itself is fine, but a bridge that can never carry data is a startup error,
    // not something to discover later from missing traffic.
    EXPECT_FALSE(server->start().has_value());
}

TEST_F(Gateway_ipc_binding_mw_com_test,
       create_client_fails_when_a_bridged_service_is_not_deployed) {
    auto const client = create_client(*m_client_runtime, kSomeipd_specifier_second_pair,
                                      bridged_services("ipc/no_such_instance", Role::consumer));

    EXPECT_EQ(client, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_bridged_service_without_events_is_rejected) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                                      bridged_services("ipc/bridged", Role::provider, {}));
    ASSERT_NE(server, nullptr);

    EXPECT_FALSE(server->start().has_value());
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_duplicate_event_name_is_rejected) {
    auto const server = create_server(
        *m_server_runtime, kSomeipd_specifier_second_pair,
        bridged_services("ipc/bridged", Role::provider,
                         {Event_config{"event_a", 16U, 128U}, Event_config{"event_a", 16U, 128U}}));
    ASSERT_NE(server, nullptr);

    EXPECT_FALSE(server->start().has_value());
}

TEST_F(Gateway_ipc_binding_mw_com_test, a_consumer_without_sample_budget_is_rejected) {
    // Subscribe(0) would succeed but never hand a sample to the application.
    auto const client = create_client(
        *m_client_runtime, kSomeipd_specifier_second_pair,
        bridged_services("ipc/bridged", Role::consumer, {Event_config{"event_a", 16U, 128U}}, 0U));

    EXPECT_EQ(client, nullptr);
}

TEST_F(Gateway_ipc_binding_mw_com_test, an_event_missing_from_the_deployment_is_rejected) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier_second_pair,
                                      bridged_services("ipc/bridged", Role::provider,
                                                       {Event_config{"no_such_event", 16U, 128U}}));
    ASSERT_NE(server, nullptr);

    EXPECT_FALSE(server->start().has_value());
}

TEST_F(Gateway_ipc_binding_mw_com_test, someipd_service_is_offered_before_the_bridged_services) {
    auto const server = create_server(*m_server_runtime, kSomeipd_specifier,
                                      bridged_services("ipc/bridged", Role::provider));
    ASSERT_NE(server, nullptr);
    ASSERT_TRUE(server->start().has_value());

    auto const client =
        create_client(*m_client_runtime, kSomeipd_specifier,
                      bridged_services("ipc/bridged", Role::consumer), "test_client");
    ASSERT_NE(client, nullptr);

    // Peer liveness is independent of the bridged services: nothing offers the bridged service
    // locally, yet the peer's binding is up and accepting.
    EXPECT_TRUE(wait_for_connected(*client, true));
}

}  // namespace
}  // namespace score::gateway_ipc_binding::mw_com
