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

#include "score/result/result.h"
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
    /// @brief Creates a Routing instance from the given configuration and IPC endpoints.
    ///
    /// Initialises a vsomeip application, registers it with the vsomeip runtime, and wires up
    /// the IPC proxy and skeleton. Subscriptions and service offerings are applied once Run()
    /// is called and the application has registered with the vsomeip routing daemon.
    ///
    /// @param config       SOME/IP gateway configuration describing the services, instances,
    ///                     events, and methods to subscribe to or offer on the network.
    /// @param ipc_proxy    IPC proxy used to receive outgoing messages from local publishers
    ///                     and forward them onto the SOME/IP network.
    /// @param ipc_skeleton IPC skeleton used to forward incoming SOME/IP events to local
    ///                     consumers.
    /// @return A fully initialised Routing instance, or an error if the vsomeip application
    ///         could not be created or initialised.
    static Result<Routing> Create(std::shared_ptr<const score::mw_someip_config::Root> config,
                                  SomeipMessageTransferProxy ipc_proxy,
                                  SomeipMessageTransferSkeleton ipc_skeleton);

    ~Routing() = default;

    Routing(const Routing&) = delete;
    Routing& operator=(const Routing&) = delete;
    Routing(Routing&&) noexcept;
    Routing& operator=(Routing&&) noexcept;

    /// Runs the routing loop, blocking until @p shutdown_requested is set to true.
    void Run(std::atomic<bool>& shutdown_requested);

   private:
    Routing(std::shared_ptr<const score::mw_someip_config::Root> config,
            SomeipMessageTransferProxy ipc_proxy, SomeipMessageTransferSkeleton ipc_skeleton);
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
