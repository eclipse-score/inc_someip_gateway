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

#ifndef SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_SERVER
#define SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_SERVER

#include <score/message_passing/i_server_factory.h>
#include <score/message_passing/server_types.h>
#include <score/message_passing/service_protocol_config.h>
#include <score/result/result.h>

#include <cstddef>
#include <memory>
#include <score/callback.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/gateway_ipc_binding/shared_memory_slot_manager.hpp>
#include <score/socom/runtime.hpp>
#include <string>
#include <unordered_map>

namespace score::gateway_ipc_binding {

/// \brief Client identifier type
using Client_id = std::size_t;
/// \brief Information about a connected client
struct Client_info {
    /// \brief Identifier string supplied by the client in its Connect message (empty if none)
    std::string identifier;
    /// \brief Transport-level client identity (PID, UID, GID)
    score::message_passing::ClientIdentity client_identity;
};
/// \brief Server-side transport endpoint for Gateway IPC Binding
/// \details Owns a `score::message_passing` server that accepts multiple incoming IPC
///          connections and forwards their protocol traffic into the shared binding base.
///
///          The concrete implementation assigns a `Client_id` to each accepted connection,
///          keeps a reply channel for each client, and uses the shared binding logic to bridge
///          service discovery, per-service connection establishment, and event forwarding.
class Gateway_ipc_binding_server {
   public:
    using On_find_service_change =
        score::cpp::move_only_function<void(Client_id, Find_service_elements const&, bool)>;

    /// \brief Create the server-side IPC binding endpoint
    /// \param runtime SOCom runtime used to register the binding as a service bridge
    /// \param server pre-created message_passing server
    /// \param slot_manager factory for per-service writable and read-only shared memory managers
    /// \param on_find_service_change callback invoked on connect/disconnect find-service updates
    /// \return Unique pointer to the created server
    static std::unique_ptr<Gateway_ipc_binding_server> create(
        score::socom::Runtime& runtime,
        score::cpp::pmr::unique_ptr<score::message_passing::IServer> server,
        score::gateway_ipc_binding::Shared_memory_manager_factory::Uptr slot_manager,
        On_find_service_change on_find_service_change) noexcept;

    /// \brief Virtual destructor
    virtual ~Gateway_ipc_binding_server() = default;

    /// \brief Start listening for incoming IPC connections
    /// \return Success or error if the transport could not start or is already listening
    virtual Result<void> start() noexcept = 0;

    /// \brief Returns information about all currently connected clients
    /// \details Maps each connected client's `Client_id` to a `Client_info` containing the
    ///          identifier string it sent in the `Connect` message (empty if none was supplied)
    ///          and the transport-level `ClientIdentity` (PID, UID, GID).
    /// \return Map from Client_id to Client_info
    virtual std::unordered_map<Client_id, Client_info> get_client_identifiers() const noexcept = 0;

   protected:
    Gateway_ipc_binding_server() = default;
    Gateway_ipc_binding_server(Gateway_ipc_binding_server const&) = delete;
    Gateway_ipc_binding_server& operator=(Gateway_ipc_binding_server const&) = delete;
    Gateway_ipc_binding_server(Gateway_ipc_binding_server&&) = delete;
    Gateway_ipc_binding_server& operator=(Gateway_ipc_binding_server&&) = delete;
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_SERVER
