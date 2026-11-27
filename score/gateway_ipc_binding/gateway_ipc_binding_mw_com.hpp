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

#ifndef SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_MW_COM
#define SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_MW_COM

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "score/gateway_ipc_binding/gateway_ipc_binding_client.hpp"
#include "score/gateway_ipc_binding/gateway_ipc_binding_server.hpp"
#include "score/socom/runtime.hpp"
#include "score/socom/service_interface_identifier.hpp"
#include "score/span.hpp"

/// \brief `mw::com` (LoLa) based implementation of the Gateway IPC Binding.
///
/// \details See `score/gateway_ipc_binding/docs/architecture/index.rst`.
///
/// \note    Method calls on bridged services are not implemented: `GenericProxy` and
///          `GenericSkeleton` are event only. So are requested event updates, because LoLa has no
///          pull API on the proxy side. Both are logged and ignored.
namespace score::gateway_ipc_binding::mw_com {

/// \brief Bytes reserved at the front of every event sample for binding metadata.
inline constexpr std::size_t kSample_prefix_size = 8U;

/// \brief Alignment requested for every event sample.
inline constexpr std::size_t kSample_alignment = 8U;

/// \brief numberOfSampleSlots of every bridged event.
inline constexpr std::size_t kNumber_of_sample_slots = 20U;

/// \brief Which side of a bridged service this peer implements.
enum class Role : std::uint8_t {
    /// This peer consumes the service locally via SOCom and offers it via a GenericSkeleton.
    provider,
    /// This peer receives the service via a GenericProxy and offers it locally via SOCom.
    consumer,
};

/// \brief mw::com event ID. A separate type, so it can't be confused with a size.
enum class Mw_com_event_id : std::uint16_t {};

/// \brief One event of a bridged service.
/// \details The position in Service_config::events is the socom::Event_id.
struct Event_config {
    /// \brief mw::com event name.
    std::string name;
    /// \brief MUST be unique within the service.
    Mw_com_event_id id;
    /// \brief Largest payload this event can carry. For SOME/IP this includes the header.
    /// \details On a consumer, this sets the minimum accepted peer sample size (or 0 in someipd
    ///          when any peer sample size is accepted). On a provider in someipd,
    ///          AddServiceConfiguration overwrites this value with gatewayd's event size.
    std::size_t max_payload_size;
};

/// \brief One bridged service instance.
struct Service_config {
    /// \brief Bridged SOCom service type name.
    socom::Service_interface_identifier interface;
    socom::Service_instance instance;
    /// \brief mw::com InstanceSpecifier for this bridged service.
    std::string instance_specifier;
    /// \brief mw::com service ID. MUST be unique per bridged service on the host.
    std::uint16_t service_id;
    /// \brief mw::com instance ID.
    std::uint16_t instance_id;
    Role role;
    /// \brief Events in socom::Event_id order.
    std::vector<Event_config> events;
    /// \brief max_sample_count passed to GenericProxyEvent::Subscribe on the consumer side.
    std::size_t max_sample_count;
};

/// \brief Bridged service instances. Owning value type, unbounded in size.
using Service_configs = std::vector<Service_config>;

/// \brief Sets up the bridged services of one AddServiceConfiguration call in someipd.
/// \details Applies received event payload sizes to `Role::provider` events (requiring a 1:1
///          match) and creates all bridged service bindings atomically:
///          - Rejects invalid service entries, missing/extra/duplicate event sizes, or any
///            `service_id` or `instance_specifier` already owned by an accepted configuration.
///          - Rejects a second invocation within the same `Gateway_config_handler` call.
/// \return True if all services were set up; false on any error (rolling back bindings from this
///         call).
using Add_bridged_services = std::function<bool(Service_configs const& services)>;

/// \brief Handles the gateway configuration that one gatewayd sends with AddServiceConfiguration.
/// \details Runs on an mw::com message-passing thread, serialized one call at a time:
///          - MUST NOT block waiting for an mw::com reply.
///          - Reconnecting calls with identical `gateway_config` bytes and the same set of
///            `event_sizes` entries (in any order) are accepted immediately without invoking this
///            handler.
/// \param gateway_config Bytes that gatewayd passed to create_client().
/// \param add_bridged_services Callable to set up the call's bridged services (at most once).
/// \return True to accept the call; false to destroy any bindings created during the call.
using Gateway_config_handler =
    std::function<bool(score::cpp::span<std::byte const> gateway_config,
                       Add_bridged_services const& add_bridged_services)>;

/// \brief Sample size derived from an Event_config (`DataTypeMetaInfo::size`).
/// \details Rounds `kSample_prefix_size + event.max_payload_size` up to `kSample_alignment` (8).
///          Saturates to `std::numeric_limits<std::size_t>::max()` if the rounded size would
///          overflow `std::size_t`.
/// \param event Event configuration to size.
/// \return Byte size of one sample slot (saturated on `std::size_t` overflow).
std::size_t sample_size(Event_config const& event) noexcept;

/// \brief Create the mw::com backed binding behind the client interface.
/// \details Consumes SomeipdService and reports it through is_connected(). Service discovery for
///          SomeipdService is started during construction, so is_connected() may still be false
///          when this function returns and becomes true asynchronously.
///
///          Every bridged service in `services` is set up eagerly: a `Role::provider` entry creates
///          the `GenericSkeleton` and the SOCom client connector, and a `Role::consumer` entry
///          creates the SOCom server connector and starts service discovery. Their availability is
///          independent of is_connected() and is signalled through the normal SOCom service state.
///          Services that are not in the mw::com configuration are added to it first.
///
///          is_connected() is true once the peer has accepted `gateway_config` and the event
///          sizes.
/// \param runtime SOCom runtime used to create the connectors
/// \param someipd_service_specifier mw::com InstanceSpecifier of the SomeipdService instance that
///        the peer provides. Must be unique per someipd on a host, and must be the
///        same value that the peer passes to create_server().
/// \param services Bridged service instances, fixed for the process lifetime
/// \param identifier Optional string used for logging only
/// \param gateway_config Sent to the peer with AddServiceConfiguration. The binding doesn't
///        read it.
/// \return Nullptr if `someipd_service_specifier` is invalid or undeployed, if service discovery
///         for SomeipdService could not be started, if any configured service fails validation,
///         deployment, setup, or discovery start, or if `gateway_config` (> 64 KiB) or consumer
///         event sizes (> 1024 events) exceed AddServiceConfiguration capacity.
std::unique_ptr<Gateway_ipc_binding_client> create_client(
    score::socom::Runtime& runtime, std::string someipd_service_specifier,
    Service_configs services = {}, std::string_view identifier = {},
    score::cpp::span<std::byte const> gateway_config = {}) noexcept;

/// \brief Create the mw::com backed server binding.
/// \details Provides SomeipdService from Gateway_ipc_binding_server::start() on. Each gatewayd
///          sends its `gateway_config` and consumer event sizes with AddServiceConfiguration, and
///          `handler` sets up the bridged services via `add_bridged_services`. Accepted services
///          stay until the server is destroyed. Consumer service discovery starts on a worker
///          thread after the call is accepted; if that fails, it is logged and the service stays
///          unavailable. Undeployed services are added to the mw::com configuration first.
/// \param runtime SOCom runtime used to create the connectors
/// \param someipd_service_specifier mw::com InstanceSpecifier of the SomeipdService instance to
///        provide, see create_client()
/// \param handler MUST NOT be empty
/// \return Nullptr if `someipd_service_specifier` is empty or `handler` is empty. Errors creating
///         or offering the SomeipdService skeleton (e.g., invalid or undeployed
///         `someipd_service_specifier`) are reported by `start()`; calling `start()` again after it
///         has succeeded returns `Mw_com_binding_error::logic_error_already_started`.
std::unique_ptr<Gateway_ipc_binding_server> create_server(score::socom::Runtime& runtime,
                                                          std::string someipd_service_specifier,
                                                          Gateway_config_handler handler) noexcept;

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_MW_COM
