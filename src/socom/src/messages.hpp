/********************************************************************************
 * Copyright (c) 2025 Elektrobit Automotive GmbH
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

#ifndef SCORE_SOCOM_MESSAGES_HPP
#define SCORE_SOCOM_MESSAGES_HPP

#include <score/socom/posix_credentials.hpp>

#include "endpoint.hpp"
#include "score/socom/client_connector.hpp"
#include "score/socom/event.hpp"

namespace score {
namespace socom {
namespace message {

struct Request_disconnect {
    using Return_type = void;
};

struct Service_state_change {
    using Return_type = void;
    Service_state const state;
    Server_service_interface_configuration const& configuration;
};

struct Connect_return {
    using Return_type = void;
    Server_connector_endpoint const endpoint;
    Service_state_change const service_state;
};

struct Connect {
    using Return_type = score::Result<message::Connect_return>;
    Client_connector_endpoint& endpoint;
};

struct Call_method {
    using Return_type = score::Result<Method_invocation::Uptr>;
    Method_id const id;
    Payload::Sptr payload;
    Method_reply_callback const on_method_reply;
    Posix_credentials const& credentials;
};

struct Posix_credentials {
    using Return_type = score::Result<::score::socom::Posix_credentials>;
};

struct Subscribe_event {
    using Return_type = score::Result<Blank>;
    Event_id const id;
    Event_mode const mode;
};

struct Unsubscribe_event {
    using Return_type = score::Result<Blank>;
    Event_id const id;
};

struct Request_event_update {
    using Return_type = score::Result<Blank>;
    Event_id const id;
};

struct Update_event {
    using Return_type = void;
    Event_id const id;
    Payload::Sptr payload;
};

struct Update_requested_event {
    using Return_type = void;
    Event_id const id;
    Payload::Sptr payload;
};

struct Ack_subscribed_event {
    using Return_type = void;
    Event_id const id;
    Event_state state;
};

}  // namespace message
}  // namespace socom
}  // namespace score

#endif  // SCORE_SOCOM_MESSAGES_HPP
