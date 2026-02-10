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

#ifndef SOCOM_SOCOM_MOCKS_HPP
#define SOCOM_SOCOM_MOCKS_HPP

#include "gmock/gmock.h"
#include "score/socom/callback_mocks.hpp"
#include "score/socom/client_connector.hpp"
#include "score/socom/event.hpp"
#include "score/socom/server_connector.hpp"

namespace ac {

struct Client_connector_callbacks_naggy_mock {
    MOCK_METHOD(void, on_service_state_change,
                (::score::socom::Client_connector const&, ::score::socom::Service_state,
                 ::score::socom::Server_service_interface_configuration const&));
    MOCK_METHOD(void, on_event_update,
                (::score::socom::Client_connector const&, ::score::socom::Event_id,
                 ::score::socom::Payload::Sptr const&));
    MOCK_METHOD(void, on_requested_event_update,
                (::score::socom::Client_connector const&, ::score::socom::Event_id,
                 ::score::socom::Payload::Sptr const&));
    MOCK_METHOD(score::Result<std::unique_ptr<::score::socom::Writable_payload>>,
                on_event_payload_allocate,
                (::score::socom::Client_connector const&, ::score::socom::Event_id));
};

struct Server_connector_callbacks_naggy_mock {
    MOCK_METHOD(::score::socom::Method_invocation::Uptr, on_method_call,
                (::score::socom::Enabled_server_connector&, ::score::socom::Method_id,
                 ::score::socom::Payload::Sptr const&, ::score::socom::Method_call_reply_data_opt));
    MOCK_METHOD(void, on_event_subscription_change,
                (::score::socom::Enabled_server_connector&, ::score::socom::Event_id,
                 ::score::socom::Event_state));
    MOCK_METHOD(void, on_event_update_request,
                (::score::socom::Enabled_server_connector&, ::score::socom::Event_id));
    MOCK_METHOD(score::Result<std::unique_ptr<::score::socom::Writable_payload>>,
                on_method_payload_allocate,
                (::score::socom::Enabled_server_connector&, ::score::socom::Method_id));
};

struct Server_connector_credentials_callbacks_naggy_mock {
    MOCK_METHOD(::score::socom::Method_invocation::Uptr, on_method_call,
                (::score::socom::Enabled_server_connector&, ::score::socom::Method_id,
                 ::score::socom::Payload::Sptr const&, ::score::socom::Method_call_reply_data_opt,
                 ::score::socom::Posix_credentials const&));
    MOCK_METHOD(void, on_event_subscription_change,
                (::score::socom::Enabled_server_connector&, ::score::socom::Event_id,
                 ::score::socom::Event_state));
    MOCK_METHOD(void, on_event_update_request,
                (::score::socom::Enabled_server_connector&, ::score::socom::Event_id));
    MOCK_METHOD(score::Result<std::unique_ptr<::score::socom::Writable_payload>>,
                on_method_payload_allocate,
                (::score::socom::Enabled_server_connector&, ::score::socom::Method_id));
};

using Server_connector_callbacks_mock =
    ::testing::StrictMock<Server_connector_callbacks_naggy_mock>;
using Server_connector_credentials_callbacks_mock =
    ::testing::StrictMock<Server_connector_credentials_callbacks_naggy_mock>;
using Client_connector_callbacks_mock =
    ::testing::StrictMock<Client_connector_callbacks_naggy_mock>;

/// \brief Creates server callbacks, which will call mock
/// \param[in] mock Mock object to be wrapped in a server callback object
/// \return server callback object which forwards calls to the given mock object
::score::socom::Disabled_server_connector::Callbacks create_server_callbacks(
    Server_connector_callbacks_mock& mock);

/// \brief Creates server callbacks, which will call mock
/// \param[in] mock Mock object to be wrapped in a server callback object
/// \return server callback object which forwards calls to the given mock object
::score::socom::Disabled_server_connector::Callbacks create_server_callbacks(
    Server_connector_credentials_callbacks_mock& mock);

/// \brief Creates client callbacks, which will call mock
/// \param[in] mock Mock object to be wrapped in a client callback object
/// \return client callback object which forwards calls to the given mock object
::score::socom::Client_connector::Callbacks create_client_callbacks(
    Client_connector_callbacks_mock& mock);

}  // namespace ac

#endif
