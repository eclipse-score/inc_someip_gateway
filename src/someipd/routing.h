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

#ifndef SRC_SOMEIPD_ROUTING
#define SRC_SOMEIPD_ROUTING

#include <atomic>
#include <memory>
#include <thread>
#include <vsomeip/vsomeip.hpp>

#include "src/common/types.h"
#include "src/config/mw_someip_config_generated.h"
#include "src/network_service/interfaces/message_transfer.h"

namespace score::someipd {

using score::someip::EventId;
using score::someip::InstanceId;
using score::someip::ServiceId;

using score::someip_gateway::network_service::interfaces::message_transfer::
    SomeipMessageTransferProxy;
using score::someip_gateway::network_service::interfaces::message_transfer::
    SomeipMessageTransferSkeleton;

/// Bridges SOME/IP messages between the vsomeip stack and the IPC layer.
///
/// Owns a vsomeip application instance, registers event subscriptions and
/// service offerings from configuration, and dispatches incoming SOME/IP
/// messages to the IPC skeleton while forwarding IPC-originated messages
/// onto the SOME/IP network via the proxy.
class Routing {
   public:
    /// @param config      Parsed SOME/IP gateway configuration (services, events, instances).
    /// @param ipc_proxy   Client-side IPC handle for forwarding messages to remote consumers.
    /// @param ipc_skeleton Server-side IPC handle for receiving messages from local producers.
    Routing(std::shared_ptr<const score::mw_someip_config::Root> config,
            SomeipMessageTransferProxy ipc_proxy, SomeipMessageTransferSkeleton ipc_skeleton);

    /// Initialises the vsomeip application and registers all handlers.
    /// @return true on success, false if vsomeip initialisation fails.
    bool Init();

    /// Runs the routing loop, blocking until @p shutdown_requested is set to true.
    void Run(std::atomic<bool>& shutdown_requested);

   private:
    void SetupSubscriptions();
    void SetupOfferings();
    void ProcessMessages(std::atomic<bool>& shutdown_requested);
    InstanceId LookupInstanceId(ServiceId service_id) const;

    std::shared_ptr<const score::mw_someip_config::Root> config_;
    std::shared_ptr<vsomeip::application> application_{};
    std::shared_ptr<vsomeip::payload> payload_{};
    std::thread processing_thread_{};

    // TODO: Replace with SOCOM implementation
    SomeipMessageTransferProxy ipc_proxy_;
    SomeipMessageTransferSkeleton ipc_skeleton_;
};

}  // namespace score::someipd

#endif  // SRC_SOMEIPD_ROUTING
