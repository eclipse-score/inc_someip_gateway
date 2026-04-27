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

#ifndef SRC_GATEWAY_IPC_BINDING_TEST_UTIL
#define SRC_GATEWAY_IPC_BINDING_TEST_UTIL

#include <gtest/gtest.h>

#include <cstddef>
#include <future>
#include <memory>
#include <score/gateway_ipc_binding/shared_memory_slot_manager.hpp>
#include <score/socom/callback_mocks.hpp>
#include <score/socom/client_connector.hpp>
#include <score/socom/runtime.hpp>
#include <string>

namespace score::gateway_ipc_binding {

Shared_memory_metadata make_metadata(std::string const& path, std::uint32_t slot_size,
                                     std::uint32_t slot_count);

std::string make_service_name();

template <typename T>
std::unique_ptr<T> create_mock_unique_ptr(T*& mock_ptr) {
    auto mock = std::make_unique<T>();
    mock_ptr = mock.get();
    return mock;
}

enum class Direction { Client_to_server, Server_to_client };

struct Client_connector_with_callbacks {
    std::promise<void> client_connected_promise;
    std::promise<void> client_disconnected_promise;
    socom::Service_state_change_callback_mock mock_service_state_change_cb;
    socom::Event_update_callback_mock mock_event_update_cb;
    socom::Event_payload_allocate_callback_mock mock_event_payload_allocate_cb;
    socom::Client_connector::Uptr connector;

    Client_connector_with_callbacks() = default;

    Client_connector_with_callbacks(socom::Runtime& runtime,
                                    socom::Server_service_interface_definition const& configuration,
                                    score::socom::Service_instance const& instance);

    void create_connector(socom::Runtime& runtime,
                          socom::Service_interface_definition const& interface,
                          score::socom::Service_instance const& instance);

    void expect_client_connected(socom::Server_service_interface_definition const& configuration);

    void subscribe_event(
        socom::Event_subscription_change_callback_mock& mock_event_subscription_change_cb,
        Event_id const& event_id);
};

struct Server_connector_with_callbacks {
    socom::Method_call_credentials_callback_mock mock_method_call_credentials_cb;
    socom::Event_subscription_change_callback_mock mock_event_subscription_change_cb;
    socom::Event_request_update_callback_mock mock_event_request_update_cb;
    socom::Method_call_payload_allocate_callback_mock mock_method_payload_allocate_cb;
    socom::Enabled_server_connector::Uptr connector;

    Server_connector_with_callbacks(socom::Runtime& runtime,
                                    socom::Server_service_interface_definition const& configuration,
                                    score::socom::Service_instance const& instance);

    void create_connector(socom::Runtime& runtime,
                          socom::Server_service_interface_definition const& interface,
                          score::socom::Service_instance const& instance);
};

inline auto matches_service_definition(socom::Server_service_interface_definition const& expected) {
    auto const expected_definition = static_cast<socom::Service_interface_definition>(expected);
    return testing::Truly([expected_definition](auto const& actual) {
        auto const actual_definition = static_cast<socom::Service_interface_definition>(actual);
        return actual_definition == expected_definition;
    });
}

inline socom::Writable_payload create_payload(
    socom::Enabled_server_connector& connector, Event_id const& event_id,
    std::vector<std::byte> const& expected_payload) {
    auto payload_handle = connector.allocate_event_payload(event_id);
    assert(payload_handle);
    auto wdata = payload_handle->wdata();
    assert(wdata.size() >= expected_payload.size());
    std::copy(expected_payload.begin(), expected_payload.end(), wdata.data());
    return std::move(*payload_handle);
}

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_TEST_UTIL
