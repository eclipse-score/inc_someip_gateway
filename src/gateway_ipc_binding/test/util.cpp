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

#include "util.hpp"

#include <gmock/gmock.h>

#include "test_constants.hpp"

using ::testing::_;
using ::testing::AtMost;

namespace score::gateway_ipc_binding {

namespace {

std::string get_test_run_id() {
    static const std::string run_id = [] {
        std::ostringstream ss;
        ss << ":" << ::getpid();
        ss << ":" << std::chrono::steady_clock::now().time_since_epoch().count();
        return std::to_string(std::hash<std::string>{}(ss.str()));
    }();
    return run_id;
}

std::string make_unique_name(std::string const& base) {
    static std::atomic<std::uint64_t> sequence{0U};
    return base + "_" + get_test_run_id() + "_" +
           std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed));
}

}  // namespace

Shared_memory_metadata make_metadata(std::string const& path, std::uint32_t slot_size,
                                     std::uint32_t slot_count) {
    Shared_memory_metadata metadata{};
    // with linux-sandbox make_unique_name() is not really necessary for uniqueness, but it adds an
    // extra layer of safety
    EXPECT_TRUE(fill_fixed_string(metadata.path, make_unique_name(path)));
    metadata.slot_size = slot_size;
    metadata.slot_count = slot_count;
    return metadata;
}

std::string make_service_name() {
    static std::atomic<int> counter{0};
    // looks like Unix Domain Sockets are not properly sandboxed
    return "gateway_ipc_binding_test_service_" + get_test_run_id() + "_" +
           std::to_string(++counter);
}

Client_connector_with_callbacks::Client_connector_with_callbacks(
    socom::Runtime& runtime, socom::Server_service_interface_definition const& configuration,
    score::socom::Service_instance const& instance) {
    expect_client_connected(configuration);
    create_connector(runtime, configuration, instance);

    EXPECT_EQ(client_connected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

void Client_connector_with_callbacks::create_connector(
    socom::Runtime& runtime, socom::Service_interface_definition const& interface,
    score::socom::Service_instance const& instance) {
    score::socom::Client_connector::Callbacks callbacks{
        mock_service_state_change_cb.as_function(), mock_event_update_cb.as_function(),
        mock_event_update_cb.as_function(), mock_event_payload_allocate_cb.as_function()};
    auto connector_result =
        runtime.make_client_connector(interface, instance, std::move(callbacks));
    assert(connector_result);
    connector = std::move(connector_result.value());
    assert(connector);
}

void Client_connector_with_callbacks::expect_client_connected(
    socom::Server_service_interface_definition const& configuration) {
    client_connected_promise = std::promise<void>();
    client_disconnected_promise = std::promise<void>();

    EXPECT_CALL(mock_service_state_change_cb,
                Call(_, score::socom::Service_state::available, configuration))
        .Times(1)
        .WillOnce([this](auto&, auto, auto) { client_connected_promise.set_value(); });

    EXPECT_CALL(mock_service_state_change_cb,
                Call(_, score::socom::Service_state::not_available, configuration))
        // Callback call is racy with IPC message exchange at test teardown
        .Times(AtMost(1))
        .WillOnce([this](auto&, auto, auto) { client_disconnected_promise.set_value(); });
}

void Client_connector_with_callbacks::subscribe_event(
    socom::Event_subscription_change_callback_mock& mock_event_subscription_change_cb,
    Event_id const& event_id) {
    std::promise<void> event_subscription_change_promise;
    EXPECT_CALL(mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::subscribed))
        .WillOnce([&event_subscription_change_promise](auto&, auto, auto) {
            event_subscription_change_promise.set_value();
        });

    // Callback call depends on whether the client or server are destroyed first
    EXPECT_CALL(mock_event_subscription_change_cb,
                Call(_, event_id, socom::Event_state::unsubscribed))
        .Times(AtMost(1));

    auto const subscribe_result =
        connector->subscribe_event(event_id, score::socom::Event_mode::update);
    ASSERT_TRUE(subscribe_result);

    EXPECT_EQ(event_subscription_change_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

Server_connector_with_callbacks::Server_connector_with_callbacks(
    socom::Runtime& runtime, socom::Server_service_interface_definition const& configuration,
    score::socom::Service_instance const& instance) {
    create_connector(runtime, configuration, instance);
}

void Server_connector_with_callbacks::create_connector(
    socom::Runtime& runtime, socom::Server_service_interface_definition const& interface,
    score::socom::Service_instance const& instance) {
    socom::Disabled_server_connector::Callbacks callbacks{
        mock_method_call_credentials_cb.as_function(),
        mock_event_subscription_change_cb.as_function(), mock_event_request_update_cb.as_function(),
        mock_method_payload_allocate_cb.as_function()};

    auto server_connector_result =
        runtime.make_server_connector(interface, instance, std::move(callbacks));
    assert(server_connector_result);

    connector =
        score::socom::Disabled_server_connector::enable(std::move(server_connector_result).value());
    assert(connector);
}

}  // namespace score::gateway_ipc_binding
