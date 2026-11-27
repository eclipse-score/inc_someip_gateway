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

#ifndef IMPL_GATEWAYD_LOCAL_SERVICE_INSTANCE
#define IMPL_GATEWAYD_LOCAL_SERVICE_INSTANCE

#include <memory>
#include <vector>

#include "score/config/mw_someip_config_generated.h"
#include "score/mw/com/types.h"
#include "score/socom/server_connector.hpp"

namespace score::socom {
class Runtime;
}  // namespace score::socom

struct score_com_serializer;

namespace score::someip_gateway::gatewayd {

/// \brief Bridges a locally provided mw::com service instance toward someipd.
/// \details Consumes local events via GenericProxy and forwards framed SOME/IP notifications
///          through an enabled SOCom server connector:
///          - Frames a 16-byte big-endian SOME/IP header at payload bytes [0..15]:
///            - [0..1]: uint16_t service_id
///            - [2..3]: uint16_t event_id (SOME/IP Method ID)
///            - [4..7]: uint32_t Length = 8 + serialized_payload_size
///            - [8..9]: uint16_t Client ID = 0xFFFF
///            - [10..11]: uint16_t Session ID = 0x0000
///            - [12]: uint8_t Protocol Version = 0x01
///            - [13]: uint8_t Interface Version = service_version_major
///            - [14]: uint8_t Message Type = 0x02 (NOTIFICATION)
///            - [15]: uint8_t Return Code = 0x00 (E_OK)
///            - [16..16 + serialized_payload_size): serialized payload bytes.
///          - Drops samples without calling update_event() when the sample pointer is null,
///            payload allocation fails, wdata().size() < 16, serialization fails, or
///            payload.shrink(16 + serialized_payload_size) fails.
///          - Unsets receive handlers and unsubscribes active proxy events on destruction.
class LocalServiceInstance {
   public:
    /// \brief Synchronously creates and enables a LocalServiceInstance for an existing proxy.
    /// \param service_instance_config Service instance configuration (must not be null).
    /// \param service_type_config Service type configuration (must not be null; service_type_name,
    ///        events, and each event entry/name must not be null; an empty non-null events vector
    ///        is valid and creates an instance with 0 events).
    /// \param ipc_proxy Generic proxy connected to the local service provider.
    /// \param socom_runtime SOCom runtime used to create the server connector.
    /// \retval std::unique_ptr<LocalServiceInstance> Created instance. Events missing from
    ///         ipc_proxy or lacking a serializer are logged and skipped while preserving
    ///         positional socom::Event_id indices (0..N-1). Valid events register a receive
    ///         handler and subscribe with someip::kMaxSampleCount (10): if SetReceiveHandler()
    ///         fails, Create() logs the error and skips Subscribe() on that event (Subscribe() is
    ///         called 0 times on that event) without failing Create(); if Subscribe() fails after
    ///         SetReceiveHandler() succeeded, UnsetReceiveHandler() is called immediately on that
    ///         event before logging and continuing without failing Create().
    /// \retval ComErrc::kInvalidConfiguration Required configuration pointers (including a null
    ///         events() vector or null event element/name) or strings are null.
    /// \retval socom::Error::runtime_error_request_rejected SOCom server connector creation failed.
    static Result<std::unique_ptr<LocalServiceInstance>> Create(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        score::mw::com::GenericProxy&& ipc_proxy, socom::Runtime& socom_runtime);

    /// \brief Starts asynchronous discovery for a local service and creates at most one instance.
    /// \param service_instance_config Service instance configuration with a valid non-null
    ///        instance_specifier.
    /// \param service_type_config Service type configuration with non-null service_type_name,
    ///        events, and event entries.
    /// \param socom_runtime SOCom runtime passed to Create() once discovered.
    /// \param instances Output vector receiving the created instance (thread-safe append).
    /// \retval mw::com::FindServiceHandle Active discovery handle. Empty handle notifications are
    ///         ignored. Once an instance is created from the first valid notification, duplicate
    ///         callbacks are ignored and StopFindService() is called.
    /// \retval ComErrc::kInvalidConfiguration Null configuration fields or invalid
    ///         instance_specifier.
    /// \retval ComErrc::kFindServiceHandlerFailure Starting mw::com discovery failed (or other
    ///         error propagated from GenericProxy::StartFindService).
    static Result<mw::com::FindServiceHandle> CreateAsyncLocalServices(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        socom::Runtime& socom_runtime,
        std::vector<std::unique_ptr<LocalServiceInstance>>& instances);

    /// \brief Unsets receive handlers and unsubscribes active proxy events.
    ~LocalServiceInstance();

    LocalServiceInstance(const LocalServiceInstance&) = delete;
    LocalServiceInstance& operator=(const LocalServiceInstance&) = delete;
    LocalServiceInstance(LocalServiceInstance&&) = delete;
    LocalServiceInstance& operator=(LocalServiceInstance&&) = delete;

   private:
    struct EventContext {
        const mw_someip_config::Event* config;
        const ::score_com_serializer* serializer;
        socom::Event_id socom_event_id;
        score::mw::com::GenericProxyEvent* ipc_event;
        bool subscribed{false};
    };

    /// \brief Private constructor
    LocalServiceInstance(
        std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config,
        std::shared_ptr<const mw_someip_config::ServiceType> service_type_config,
        score::socom::Enabled_server_connector::Uptr server_connector,
        score::mw::com::GenericProxy&& ipc_proxy);

    static score::cpp::move_only_function<void(score::mw::com::SamplePtr<void>)>
    create_process_sample_callback(LocalServiceInstance* instance_ptr,
                                   EventContext const& event_context);

    /// Configuration for this service instance
    std::shared_ptr<const mw_someip_config::ServiceInstance> service_instance_config_;
    /// Configuration for the service type of this instance
    std::shared_ptr<const mw_someip_config::ServiceType> service_type_config_;
    /// SOCom server connector for handling communication with the someipd daemon
    score::socom::Enabled_server_connector::Uptr server_connector_;
    /// Per-event forwarding contexts (outlive ipc_proxy_ callbacks during teardown)
    std::vector<EventContext> event_contexts_;
    /// Generic proxy for IPC communication with the local service providing application
    score::mw::com::GenericProxy ipc_proxy_;
};
}  // namespace score::someip_gateway::gatewayd

#endif  // IMPL_GATEWAYD_LOCAL_SERVICE_INSTANCE
