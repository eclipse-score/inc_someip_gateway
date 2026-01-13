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

#ifndef SRC_GATEWAYD_REMOTE_SERVICE_INSTANCE
#define SRC_GATEWAYD_REMOTE_SERVICE_INSTANCE

#include <memory>
#include <vector>

#include "score/mw/com/types.h"
#include "src/gatewayd/gatewayd_config_generated.h"
#include "src/network_service/interfaces/message_transfer.h"
#include "tests/performance_benchmarks/echo_service.h"

namespace score::someip_gateway::gatewayd {

/// \brief Instance of a remotely available service
/// \details This class represents a service instance that is provided by an application
///          running on a different ECU and offered via SOME/IP. It manages the communication
///          between the someipd daemon and local applications that consume the remote service.
///          The class receives messages from the someipd daemon via the SOME/IP message proxy
///          and forwards them to local consumer applications through an IPC skeleton, making
///          remote services accessible to applications on this ECU.
class RemoteServiceInstance {
   public:
    /// \brief Constructs a RemoteServiceInstance
    /// \param service_instance_config Configuration for this service instance
    /// \param ipc_skeleton IPC skeleton for communication with local consumer applications
    /// \param someip_message_proxy Proxy for receiving messages from the someipd daemon
    /// \details This constructor initializes a remote service instance with the necessary
    ///          components to receive messages from the someipd daemon and forward them to
    ///          local applications via IPC.
    RemoteServiceInstance(std::shared_ptr<const config::ServiceInstance> service_instance_config,
                          // TODO: Use something generic (template)?
                          echo_service::EchoResponseSkeleton&& ipc_skeleton,
                          network_service::interfaces::message_transfer::SomeipMessageTransferProxy
                              someip_message_proxy);

    /// \brief Asynchronously creates a remote service instance
    /// \param service_instance_config Configuration for the service instance to create
    /// \param instances Vector to store the created remote service instance
    /// \return Result containing a FindServiceHandle on success, or an error on failure
    /// \details This static factory method asynchronously searches for and creates a remote
    ///          service instance. It performs service discovery for services offered via SOME/IP
    ///          from remote ECUs and, when found, constructs a RemoteServiceInstance object and
    ///          adds it to the instances vector. The returned FindServiceHandle can be used to
    ///          manage the asynchronous operation.
    static Result<mw::com::FindServiceHandle> CreateAsyncRemoteService(
        std::shared_ptr<const config::ServiceInstance> service_instance_config,
        std::vector<std::unique_ptr<RemoteServiceInstance>>& instances);

    RemoteServiceInstance(const RemoteServiceInstance&) = delete;
    RemoteServiceInstance& operator=(const RemoteServiceInstance&) = delete;
    RemoteServiceInstance(RemoteServiceInstance&&) = delete;
    RemoteServiceInstance& operator=(RemoteServiceInstance&&) = delete;

   private:
    /// Configuration for this service instance
    std::shared_ptr<const config::ServiceInstance> service_instance_config_;
    /// IPC skeleton for forwarding messages to local consumer applications
    echo_service::EchoResponseSkeleton ipc_skeleton_;
    /// Proxy for receiving messages from the someipd daemon
    network_service::interfaces::message_transfer::SomeipMessageTransferProxy someip_message_proxy_;
};
}  // namespace score::someip_gateway::gatewayd

#endif  // SRC_GATEWAYD_REMOTE_SERVICE_INSTANCE
