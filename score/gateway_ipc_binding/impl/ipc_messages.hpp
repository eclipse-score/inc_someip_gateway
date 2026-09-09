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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_IPC_MESSAGES
#define SRC_GATEWAY_IPC_BINDING_SRC_IPC_MESSAGES

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "score/gateway_ipc_binding/gateway_ipc_binding.hpp"

/// \file
/// \brief Wire format of the IPC protocol: message type tags and payload structs.
///
/// These types describe the bytes exchanged between the client and the server binding and are
/// therefore an implementation detail of this library, not part of its public API. They are kept
/// out of the public headers so that the protocol can evolve without breaking dependents.
/// The value types the payloads are built from (Service, Instance_id, Shared_memory_metadata,
/// ...) do belong to the public API and live in gateway_ipc_binding.hpp.

namespace score::gateway_ipc_binding {

/// \brief Conservative upper bound for a single (framed) IPC message.
/// \details score::message_passing's QNX qnx_dispatch backend hardcodes its resmgr message
/// buffer to 2088 bytes, independent of any configured protocol size, so a larger message
/// fails to send with EMSGSIZE at the OS level on QNX. Message_frame<T> is statically checked
/// against this bound so that growing one of the kMax_* constants in gateway_ipc_binding.hpp
/// can't silently reintroduce a message that can never be delivered.
// See also https://github.com/eclipse-score/communication/issues/848
inline constexpr std::size_t kMax_safe_message_size = 2000U;

/// \brief Message type identifiers for IPC framing
enum class Message_type : std::uint8_t {
    Connect = 1,
    Connect_reply = 2,
    Request_service = 3,
    Offer_service = 4,
    Connect_service = 5,
    Connect_service_reply = 6,
    Call_method = 7,
    Call_method_handle = 8,
    Call_method_reply = 9,
    Cancel_method_call = 10,
    Subscribe_event = 11,
    Subscribe_event_reply = 12,
    Event_update = 13,
    Event_update_request = 14,
    Payload_consumed = 15,
};

/// \brief Handle to identify a connected service/instance pair
///
/// It must be unique across all connections.
/// For a new connection an old value must *NOT* be reused, even if the previous connection with
/// that handle has been closed.
using Remote_handle = std::uint64_t;

/// \brief Method invocation identifier
using Method_invocation = std::uint64_t;

#define DECLARE_MESSAGE_TYPE(msg_type) static constexpr Message_type type = msg_type

/// \brief Payload has been fully processed and can be released
struct Payload_consumed {
    DECLARE_MESSAGE_TYPE(Message_type::Payload_consumed);
    Remote_handle required_id;
    Shared_memory_handle handle;
};

/// \brief Initial IPC connection request
struct Connect {
    DECLARE_MESSAGE_TYPE(Message_type::Connect);

    Find_service_elements find_service_elements;
    /// \brief Shared memory configuration for each service instance that the server is expected
    /// to allocate. Sent by the client so the server needs no upfront configuration.
    Shared_memory_configs shared_memory_configs;
    Client_identifier identifier;
};

/// \brief Initial IPC connection acknowledgement
struct Connect_reply {
    DECLARE_MESSAGE_TYPE(Message_type::Connect_reply);
    bool status;
};

/// \brief Request to use or stop using a service
struct Request_service {
    DECLARE_MESSAGE_TYPE(Message_type::Request_service);
    Service service_id;
    Instance_id instance_id;
    bool in_use;
};

/// \brief Announce offered or withdrawn service
struct Offer_service {
    DECLARE_MESSAGE_TYPE(Message_type::Offer_service);
    Service service_id;
    Instance_id instance_id;
    bool offered;
};

/// \brief Request setup/teardown of service connection
struct Connect_service {
    DECLARE_MESSAGE_TYPE(Message_type::Connect_service);
    Service service_id;
    Instance_id instance_id;
    Remote_handle required_id;
    // Memory for method calls
    Shared_memory_metadata metadata;
    bool in_use;
};

/// \brief Response to Connect_service
struct Connect_service_reply {
    DECLARE_MESSAGE_TYPE(Message_type::Connect_service_reply);
    Remote_handle required_id;
    Remote_handle provided_id;
    // Memory for event updates and method replies
    Shared_memory_metadata metadata;
    std::uint16_t num_methods;
    std::uint16_t num_events;
};

/// \brief Method invocation request
struct Call_method {
    Remote_handle provided_id;
    Method_id method_id;
    bool fire_and_forget;
    Shared_memory_handle payload;
};

/// \brief Identifier for an active method invocation
struct Call_method_handle {
    Remote_handle required_id;
    Method_invocation invocation_id;
};

/// \brief Reply to method invocation
struct Call_method_reply {
    Remote_handle required_id;
    Method_invocation invocation_id;
    Shared_memory_handle payload;
};

/// \brief Cancel an active method invocation
struct Cancel_method_call {
    Remote_handle provided_id;
    Method_id method_id;
    Method_invocation invocation_id;
};

/// \brief Event subscription request
struct Subscribe_event {
    DECLARE_MESSAGE_TYPE(Message_type::Subscribe_event);
    Remote_handle provided_id;
    Event_id event_id;
    bool subscribe;
};

/// \brief Event subscription acknowledgement (accept/reject)
struct Subscribe_event_reply {
    Remote_handle required_id;
    Event_id event_id;
    bool subscribed;
};

/// \brief Event payload update
struct Event_update {
    DECLARE_MESSAGE_TYPE(Message_type::Event_update);
    Remote_handle required_id;
    Event_id event_id;
    Shared_memory_handle payload;
};

/// \brief Request latest event update (field pull)
struct Event_update_request {
    DECLARE_MESSAGE_TYPE(Message_type::Event_update_request);
    Remote_handle provided_id;
    Event_id event_id;
};

static_assert(std::is_trivially_copyable_v<Payload_consumed>);
static_assert(std::is_trivially_copyable_v<Connect>);
static_assert(std::is_trivially_copyable_v<Connect_reply>);
static_assert(std::is_trivially_copyable_v<Request_service>);
static_assert(std::is_trivially_copyable_v<Offer_service>);
static_assert(std::is_trivially_copyable_v<Connect_service>);
static_assert(std::is_trivially_copyable_v<Connect_service_reply>);
static_assert(std::is_trivially_copyable_v<Call_method>);
static_assert(std::is_trivially_copyable_v<Call_method_handle>);
static_assert(std::is_trivially_copyable_v<Call_method_reply>);
static_assert(std::is_trivially_copyable_v<Cancel_method_call>);
static_assert(std::is_trivially_copyable_v<Subscribe_event>);
static_assert(std::is_trivially_copyable_v<Subscribe_event_reply>);
static_assert(std::is_trivially_copyable_v<Event_update>);
static_assert(std::is_trivially_copyable_v<Event_update_request>);

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_IPC_MESSAGES
