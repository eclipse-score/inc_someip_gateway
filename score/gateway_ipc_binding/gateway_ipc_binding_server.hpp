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

#ifndef SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_SERVER
#define SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_SERVER

#include "score/result/result.h"

namespace score::gateway_ipc_binding {

/// \brief Server-side endpoint for the Gateway IPC Binding.
class Gateway_ipc_binding_server {
   public:
    virtual ~Gateway_ipc_binding_server() = default;

    /// \brief Start accepting connections from gateway clients.
    /// \return Success or an error if the endpoint cannot be started.
    virtual Result<void> start() noexcept = 0;

   protected:
    Gateway_ipc_binding_server() = default;
    Gateway_ipc_binding_server(Gateway_ipc_binding_server const&) = delete;
    Gateway_ipc_binding_server& operator=(Gateway_ipc_binding_server const&) = delete;
    Gateway_ipc_binding_server(Gateway_ipc_binding_server&&) = delete;
    Gateway_ipc_binding_server& operator=(Gateway_ipc_binding_server&&) = delete;
};

}  // namespace score::gateway_ipc_binding

#endif  // SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_SERVER
