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

#ifndef SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_CLIENT
#define SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_CLIENT

#include <score/message_passing/i_client_connection.h>
#include <score/result/result.h>

#include <memory>
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/gateway_ipc_binding/shared_memory_slot_manager.hpp>
#include <score/memory.hpp>
#include <score/socom/runtime.hpp>
#include <string_view>

namespace score::gateway_ipc_binding {

/// \brief Client-side transport endpoint for Gateway IPC Binding
/// \details Owns one outgoing `score::message_passing` connection and forwards all
///          Gateway IPC binding protocol handling into the shared binding base.
///
///          The concrete implementation starts the underlying IPC connection during
///          construction. When the transport reaches `kReady`, it sends `Connect`
///          automatically. The instance reports connected state only after a positive
///          `Connect_reply` has been received.
///
///          After that initial handshake the client can both consume remote services and
///          expose local services through the same shared bridge logic used by the server.
class Gateway_ipc_binding_client {
   public:
    /// \brief Create the client-side IPC binding endpoint
    /// \param runtime SOCom runtime used to register the binding as a service bridge
    /// \param connection pre-created client transport connection
    /// \param slot_manager factory for per-service writable and read-only shared memory managers
    /// \param find_service_elements Service elements to advertise for finding services
    /// \param identifier Optional string identifying this client peer to the server
    /// \return Unique pointer to the created client
    static std::unique_ptr<Gateway_ipc_binding_client> create(
        score::socom::Runtime& runtime,
        score::cpp::pmr::unique_ptr<score::message_passing::IClientConnection> connection,
        score::gateway_ipc_binding::Shared_memory_manager_factory::Uptr slot_manager,
        Find_service_elements find_service_elements = {},
        std::string_view identifier = {}) noexcept;

    /// \brief Virtual destructor
    virtual ~Gateway_ipc_binding_client() = default;

    /// \brief Returns true after `Connect_reply{status=true}` has been received
    virtual bool is_connected() const noexcept = 0;

   protected:
    Gateway_ipc_binding_client() = default;
    Gateway_ipc_binding_client(Gateway_ipc_binding_client const&) = delete;
    Gateway_ipc_binding_client& operator=(Gateway_ipc_binding_client const&) = delete;
    Gateway_ipc_binding_client(Gateway_ipc_binding_client&&) = delete;
    Gateway_ipc_binding_client& operator=(Gateway_ipc_binding_client&&) = delete;
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_CLIENT
