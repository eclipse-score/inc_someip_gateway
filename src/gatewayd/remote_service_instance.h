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

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "score/mw/com/types.h"
#include "score/socom/client_connector.hpp"
#include "src/config/mw_someip_config_generated.h"

struct score_com_serializer;

namespace score::socom {
class Runtime;
}  // namespace score::socom

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
    /// \brief Creates a RemoteServiceInstance
    /// \param service_instance_config Configuration for this service instance
    /// \param service_type_config Configuration for the service type of this instance
    /// \param ipc_skeleton IPC skeleton for forwarding events to local consumer applications
    /// \param socom_runtime SOCom runtime used to create the client connector
    /// \return Result containing the created instance on success, or an error on failure
    /// \details This factory method creates a remote service instance with the necessary
    ///          components to receive messages from the someipd daemon and forward them to
    ///          local applications via IPC.
    static Result<std::unique_ptr<RemoteServiceInstance>> Create(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        score::mw::com::GenericSkeleton&& ipc_skeleton, socom::Runtime& socom_runtime);

    /// \brief Asynchronously creates a remote service instance
    /// \param service_instance_config Configuration for the service instance to create
    /// \param service_type_config Configuration for the service type of the instance to create
    /// \param socom_runtime SOCom runtime used to create the client connector
    /// \param instances Reference to the vector to store the created remote service instance
    /// \return Result containing an Error on failure.
    /// \details This static factory method asynchronously searches for and creates a remote
    ///          service instance. It performs service discovery for services offered via SOME/IP
    ///          from remote ECUs and, when found, constructs a RemoteServiceInstance object and
    ///          adds it to the instances vector. The returned FindServiceHandle can be used to
    ///          manage the asynchronous operation.
    static Result<void> CreateAsyncRemoteService(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        socom::Runtime& socom_runtime,
        std::vector<std::unique_ptr<RemoteServiceInstance>>& instances);

    RemoteServiceInstance(const RemoteServiceInstance&) = delete;
    RemoteServiceInstance& operator=(const RemoteServiceInstance&) = delete;
    RemoteServiceInstance(RemoteServiceInstance&&) = delete;
    RemoteServiceInstance& operator=(RemoteServiceInstance&&) = delete;

   private:
    struct EventContext {
        const mw_someip_config::Event* config;
        const ::score_com_serializer* serializer;
        score::mw::com::GenericSkeletonEvent* ipc_event;
    };

    /// \brief Private constructor
    RemoteServiceInstance(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        score::mw::com::GenericSkeleton&& ipc_skeleton,
        socom::Client_connector::Uptr client_connector,
        std::unordered_map<std::uint16_t, EventContext> event_contexts);

    void forward_event(socom::Event_id event_id, socom::Payload payload);

    /// Configuration for this service instance
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config_;
    /// Configuration for the service type of this instance
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config_;
    /// IPC skeleton for forwarding events to local consumer applications
    score::mw::com::GenericSkeleton ipc_skeleton_;
    /// SOCom client connector for receiving event updates from the someipd daemon.
    /// Declared last so it is destroyed first, ensuring no callbacks fire after ipc_skeleton_
    /// or service_type_config_ are gone.
    socom::Client_connector::Uptr client_connector_;

    std::unordered_map<std::uint16_t, EventContext> event_contexts_;
};

}  // namespace score::someip_gateway::gatewayd

#endif  // SRC_GATEWAYD_REMOTE_SERVICE_INSTANCE
