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

#pragma once

#include <atomic>
#include <memory>

#include "src/someipd/i_internal_ipc.h"
#include "src/someipd/i_network_stack.h"
#include "src/someipd/someipd_config.h"

namespace score::someip_gateway::someipd {

/// Bridge abstraction that decouples the routing logic from both the SOME/IP
/// network stack and the internal IPC framework.
///
/// Abstraction:GatewayRouting
/// Implementor: IInternalIpc and INetworkStack (Abstract interfaces)
/// Concrete Implementor (A/B): MwcomAdapter and VsomeipAdapter.
/// Delegation: network_stack_->RequestService(...) or internal_ipc_->SendToGatewayd(...), ... etc
/// Client: main.cpp instantiates GatewayRouting() and calls Run()

class GatewayRouting {
   public:
    GatewayRouting(std::unique_ptr<INetworkStack> network_stack,
                   std::unique_ptr<IInternalIpc> internal_ipc, SomeipDConfig config);

    /// Run the routing loop. Blocks until shutdown_requested becomes true.
    void Run(std::atomic<bool>& shutdown_requested);

   private:
    void SetupSubscriptions();
    void SetupOfferings();
    void ProcessMessages(std::atomic<bool>& shutdown_requested);
    InstanceId LookupInstanceId(ServiceId service_id) const;

    SomeipDConfig config_;
    std::unique_ptr<IInternalIpc> internal_ipc_;
    std::unique_ptr<INetworkStack> network_stack_;
};

}  // namespace score::someip_gateway::someipd
