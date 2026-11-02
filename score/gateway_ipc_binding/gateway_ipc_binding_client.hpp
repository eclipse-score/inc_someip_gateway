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

#ifndef SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_CLIENT
#define SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_CLIENT

namespace score::gateway_ipc_binding {

/// \brief Client-side endpoint for the Gateway IPC Binding.
class Gateway_ipc_binding_client {
   public:
    virtual ~Gateway_ipc_binding_client() = default;

    /// \brief Returns whether the peer accepted the client connection.
    virtual bool is_connected() const noexcept = 0;

   protected:
    Gateway_ipc_binding_client() = default;
    Gateway_ipc_binding_client(Gateway_ipc_binding_client const&) = delete;
    Gateway_ipc_binding_client& operator=(Gateway_ipc_binding_client const&) = delete;
    Gateway_ipc_binding_client(Gateway_ipc_binding_client&&) = delete;
    Gateway_ipc_binding_client& operator=(Gateway_ipc_binding_client&&) = delete;
};

}  // namespace score::gateway_ipc_binding

#endif  // SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_CLIENT
