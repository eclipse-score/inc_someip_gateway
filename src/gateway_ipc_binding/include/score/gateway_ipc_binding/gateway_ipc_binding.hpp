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

#ifndef SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING
#define SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING

#include <cstddef>
#include <cstdint>
#include <score/gateway_ipc_binding/fixed_size_container.hpp>
#include <score/socom/event.hpp>
#include <score/socom/method.hpp>
#include <score/socom/service_interface_identifier.hpp>
#include <score/span.hpp>
#include <type_traits>

namespace score::gateway_ipc_binding {

/// \brief Maximum find service elements
inline constexpr std::size_t kMax_find_service_elements = 16U;

/// \brief Maximum bytes for serialized service id
inline constexpr std::size_t kMax_service_id_size = 64U;

/// \brief Maximum bytes for serialized instance id
inline constexpr std::size_t kMax_instance_id_size = 64U;
/// \brief Maximum bytes for client identifier string (including null terminator)
inline constexpr std::size_t kMax_client_identifier_size = 64U;
/// \brief Maximum shared memory path length (including null terminator)
inline constexpr std::size_t kMax_shared_memory_path_size = NAME_MAX;

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

/// \brief Service id in fixed-size form
using Service_id = Fixed_string<kMax_service_id_size>;

/// \brief Service descriptor, POD-friendly representation of socom Service
struct Service {
    Service_id service_id;
    socom::Service_interface_identifier::Version version;

    socom::Service_interface_identifier to_socom_identifier() const noexcept;
};

/// \brief Compares two Service objects for equality
bool operator==(Service const& lhs, Service const& rhs);

/// \brief Hash function for Service to be used in unordered containers
struct Service_hash {
    std::size_t operator()(Service const& s) const noexcept;
};

/// \brief Instance identifier in fixed-size form
using Instance_id = Fixed_string<kMax_instance_id_size>;

/// \brief Peer identifier string, sent by the client during Connect
using Client_identifier = Fixed_string<kMax_client_identifier_size>;

Service make_service(score::socom::Service_interface_identifier const& interface) noexcept;

Instance_id make_instance_id(score::socom::Service_instance const& instance) noexcept;

/// \brief Handle to identify a connected service/instance pair
///
/// It must be unique across all connections.
/// For a new connection an old value must *NOT* be reused, even if the previous connection with
/// that handle has been closed.
using Remote_handle = std::uint64_t;

/// \brief Method identifier
using Method_id = socom::Method_id;

/// \brief Event identifier
using Event_id = socom::Event_id;

/// \brief Method invocation identifier
using Method_invocation = std::uint64_t;

/// \brief Handle to locate payload in shared memory
struct Shared_memory_handle {
    std::uint32_t slot_index;
    std::uint32_t used_bytes;
};

bool operator==(Shared_memory_handle const& lhs, Shared_memory_handle const& rhs) noexcept;

/// \brief Path to shared memory, in fixed-size form
using Shared_memory_path = Fixed_string<kMax_shared_memory_path_size>;

/// \brief Metadata needed to map and interpret peer shared memory
struct Shared_memory_metadata {
    Shared_memory_path path;
    std::uint32_t slot_size;
    std::uint32_t slot_count;
};

bool operator==(Shared_memory_metadata const& lhs, Shared_memory_metadata const& rhs) noexcept;

struct Service_instance {
    Service service;
    Instance_id instance_id;
};

bool operator==(Service_instance const& lhs, Service_instance const& rhs) noexcept;

using Find_service_elements = Fixed_size_container<Service_instance, kMax_find_service_elements>;

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
    Shared_memory_metadata metadata;
    bool in_use;
};

/// \brief Response to Connect_service
struct Connect_service_reply {
    DECLARE_MESSAGE_TYPE(Message_type::Connect_service_reply);
    Remote_handle required_id;
    Remote_handle provided_id;
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

static_assert(std::is_trivially_copyable_v<Fixed_string<kMax_service_id_size>>);
static_assert(std::is_trivially_copyable_v<Client_identifier>);
static_assert(std::is_trivially_copyable_v<Service>);
static_assert(std::is_trivially_copyable_v<Shared_memory_handle>);
static_assert(std::is_trivially_copyable_v<Shared_memory_metadata>);
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

#endif  // SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING
