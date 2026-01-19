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

#ifndef SRC_GATEWAYD_LOCAL_SERVICE_INSTANCE
#define SRC_GATEWAYD_LOCAL_SERVICE_INSTANCE

#include <memory>
#include <vector>

#include "score/mw/com/types.h"
#include "src/gatewayd/gatewayd_config_generated.h"
#include "src/network_service/interfaces/message_transfer.h"

namespace score::someip_gateway::gatewayd {

/// \brief Instance of a locally available service
/// \details This class represents a service instance that is provided by an application
///          running on the same ECU. It manages the communication between the local service
///          and the someipd daemon, which handles the actual SOME/IP network protocol.
///          The class uses an IPC proxy for local communication with the service providing
///          application and coordinates with the SOME/IP message skeleton to forward messages to
///          the someipd daemon, ultimately making the local service accessible to remote ECUs.
class LocalServiceInstance {
   public:
    /// \brief Constructs a LocalServiceInstance
    /// \param service_instance_config Configuration for this service instance
    /// \param ipc_proxy Generic proxy for IPC communication with the local service
    /// \param someip_message_skeletons Map of SomeipMessage skeletons for message transfer to the
    /// someipd daemon
    /// \details This constructor initializes a local service instance with the necessary
    ///          components to forward local service messages to the someipd daemon, which
    ///          then handles the SOME/IP network communication with remote ECUs.
    LocalServiceInstance(
        std::shared_ptr<const config::ServiceInstance> service_instance_config,
        score::mw::com::GenericProxy&& ipc_proxy,
        // TODO: Decouple this via an interface
        std::map<uint16_t,
                 network_service::interfaces::message_transfer::SomeipMessageTransferSkeleton>&&
            someip_message_skeletons);

    /// \brief Asynchronously creates a local service instance
    /// \param service_instance_config Configuration for the service instance to create
    /// \param someip_message_skeletons Map of SomeipMessage skeletons for message transfer to the
    /// someipd daemon
    /// \param instances Vector to store the created local service instance
    /// \return Result containing a FindServiceHandle on success, or an error on failure
    /// \details This static factory method asynchronously searches for and creates a local
    ///          service instance. It performs service discovery on the local ECU and, when
    ///          found, constructs a LocalServiceInstance object and adds it to the instances
    ///          vector. The returned FindServiceHandle can be used to manage the asynchronous
    ///          operation.
    static Result<mw::com::FindServiceHandle> CreateAsyncLocalService(
        std::shared_ptr<const config::ServiceInstance> service_instance_config,
        std::map<uint16_t,
                 network_service::interfaces::message_transfer::SomeipMessageTransferSkeleton>&&
            someip_message_skeletons,
        std::vector<std::unique_ptr<LocalServiceInstance>>& instances);

    LocalServiceInstance(const LocalServiceInstance&) = delete;
    LocalServiceInstance& operator=(const LocalServiceInstance&) = delete;
    LocalServiceInstance(LocalServiceInstance&&) = delete;
    LocalServiceInstance& operator=(LocalServiceInstance&&) = delete;

   private:
    /// Configuration for this service instance
    std::shared_ptr<const config::ServiceInstance> service_instance_config_;
    /// Generic proxy for IPC communication with the local service providing application
    score::mw::com::GenericProxy ipc_proxy_;
    /// Map of SomeipMessageTransferSkeletons with event/method ID as key. One skeleton for each
    /// service instance element to forward to the someipd daemon.
    std::map<uint16_t, network_service::interfaces::message_transfer::SomeipMessageTransferSkeleton>
        someip_message_skeletons_;
};
}  // namespace score::someip_gateway::gatewayd

#endif  // SRC_GATEWAYD_LOCAL_SERVICE_INSTANCE
