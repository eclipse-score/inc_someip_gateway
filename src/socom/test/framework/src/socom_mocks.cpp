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

#include "socom_mocks.hpp"

using ::score::socom::Client_connector;
using ::score::socom::Disabled_server_connector;

namespace ac {

Disabled_server_connector::Callbacks create_server_callbacks(
    Server_connector_callbacks_mock& mock) {
    return Disabled_server_connector::Callbacks{
        [&mock](auto& connector, auto mid, auto const& payload, auto reply_callback, auto cred,
                auto reply_payload_allocation_callback) {
            return mock.on_method_call(connector, mid, payload, reply_callback);
        },
        [&mock](auto& connector, auto eid, auto state) {
            mock.on_event_subscription_change(connector, eid, state);
        },
        [&mock](auto& connector, auto eid) { mock.on_event_update_request(connector, eid); }};
}

Disabled_server_connector::Callbacks create_server_callbacks(
    Server_connector_credentials_callbacks_mock& mock) {
    return Disabled_server_connector::Callbacks{
        [&mock](auto& connector, auto mid, auto const& payload, auto reply_callback,
                auto const& credentials, auto const& reply_payload_allocation_callback) {
            return mock.on_method_call(connector, mid, payload, reply_callback, credentials,
                                       reply_payload_allocation_callback);
        },
        [&mock](auto& connector, auto eid, auto state) {
            mock.on_event_subscription_change(connector, eid, state);
        },
        [&mock](auto& connector, auto eid) { mock.on_event_update_request(connector, eid); }};
}

Client_connector::Callbacks create_client_callbacks(Client_connector_callbacks_mock& mock) {
    return Client_connector::Callbacks{
        [&mock](auto const& connector, auto state, auto const& configuration) {
            mock.on_service_state_change(connector, state, configuration);
        },
        [&mock](auto const& connector, auto id, auto const& payload) {
            mock.on_event_update(connector, id, payload);
        },
        [&mock](auto const& connector, auto id, auto const& payload) {
            mock.on_requested_event_update(connector, id, payload);
        },
        [&mock](auto const& connector, auto id) {
            return mock.on_event_payload_allocate(connector, id);
        }};
}

}  // namespace ac
