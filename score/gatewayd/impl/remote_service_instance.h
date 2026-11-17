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

#ifndef IMPL_GATEWAYD_REMOTE_SERVICE_INSTANCE
#define IMPL_GATEWAYD_REMOTE_SERVICE_INSTANCE

#include <memory>
#include <unordered_map>
#include <vector>

#include "score/config/mw_someip_config_generated.h"
#include "score/mw/com/types.h"
#include "score/socom/client_connector.hpp"

struct score_com_serializer;

namespace score::socom {
class Runtime;
}  // namespace score::socom

namespace score::someip_gateway::gatewayd {

/// \brief Bridges a remotely available SOME/IP service from someipd to local IPC consumers.
/// \details Receives SOME/IP event notifications via a SOCom client connector and publishes
///          deserialized samples through a local GenericSkeleton:
///          - On Service_state::available, subscribes (in Event_mode::update) only to configured
///            events that exist on the IPC skeleton and have a valid serializer.
///          - Requires inbound frames of at least 16 bytes (someip::kSomeipFullHeaderSize), strips
///            header bytes [0..15], deserializes bytes [16..) into an allocated LoLa sample slot,
///            and publishes via GenericSkeletonEvent::Send().
///          - Drops frames shorter than 16 bytes, updates for unknown or skipped socom::Event_id
///            values, failed sample allocations, and failed deserializations without calling
///            Send().
///          - Destroys the SOCom client connector and calls StopOfferService() on destruction.
class RemoteServiceInstance {
   public:
    /// \brief Synchronously offers an IPC skeleton and connects a SOCom client connector.
    /// \param service_instance_config Service instance configuration (must not be null).
    /// \param service_type_config Service type configuration (must not be null; service_type_name,
    ///        a non-empty events vector, and each event entry/name must not be null).
    /// \param ipc_skeleton IPC skeleton offered to local consumer applications.
    /// \param socom_runtime SOCom runtime used to create the client connector.
    /// \retval std::unique_ptr<RemoteServiceInstance> Created instance. Events missing from
    ///         ipc_skeleton or lacking a serializer are logged and skipped while preserving
    ///         positional socom::Event_id indices (0..N-1).
    /// \retval ComErrc::kInvalidConfiguration Required configuration pointers or strings are null,
    ///         or events() is null or empty (events()->empty()).
    /// \retval ComErrc::kBindingFailure Offering ipc_skeleton failed.
    /// \retval socom::Error::runtime_error_request_rejected SOCom client connector creation failed
    ///         (the skeleton offer is withdrawn via StopOfferService()).
    static Result<std::unique_ptr<RemoteServiceInstance>> Create(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        score::mw::com::GenericSkeleton&& ipc_skeleton, socom::Runtime& socom_runtime);

    /// \brief Synchronously creates a GenericSkeleton, offers it, and appends a
    ///        RemoteServiceInstance to instances.
    /// \details Executes synchronously despite the Async prefix in its name.
    /// \param service_instance_config Service instance configuration with a valid non-null
    ///        instance_specifier.
    /// \param service_type_config Service type configuration with a non-null, non-empty events
    ///        vector where every event entry/name is non-null and has a valid serializer.
    /// \param socom_runtime SOCom runtime used to create the client connector.
    /// \param instances Output vector to which the created instance is appended on success.
    /// \retval void Instance created and appended to instances.
    /// \retval ComErrc::kInvalidConfiguration Null configuration fields, null or empty events()
    ///         vector (events()->empty()), invalid instance_specifier, missing event serializer, or
    ///         GenericSkeleton::Create failure.
    /// \retval ComErrc::kBindingFailure RemoteServiceInstance::Create failed.
    static Result<void> CreateAsyncRemoteService(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        socom::Runtime& socom_runtime,
        std::vector<std::unique_ptr<RemoteServiceInstance>>& instances);

    /// \brief Destroys the SOCom client connector and stops offering the IPC skeleton.
    ~RemoteServiceInstance();

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
        std::unordered_map<socom::Event_id, EventContext> event_contexts,
        socom::Client_connector::Uptr client_connector);

    void forward_event(socom::Event_id event_id, socom::Payload payload);

    /// Configuration for this service instance
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config_;
    /// Configuration for the service type of this instance
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config_;
    /// IPC skeleton for forwarding events to local consumer applications
    score::mw::com::GenericSkeleton ipc_skeleton_;
    /// Per-event forwarding contexts (must outlive client_connector_ callbacks)
    std::unordered_map<socom::Event_id, EventContext> event_contexts_;
    /// SOCom client connector for receiving event updates from the someipd daemon
    socom::Client_connector::Uptr client_connector_;
};

}  // namespace score::someip_gateway::gatewayd

#endif  // IMPL_GATEWAYD_REMOTE_SERVICE_INSTANCE
