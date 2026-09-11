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
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "score/gateway_ipc_binding/gateway_ipc_binding_client.hpp"
#include "score/gateway_ipc_binding/gateway_ipc_binding_server.hpp"
#include "score/socom/runtime.hpp"
#include "score/socom/service_interface_identifier.hpp"

/// \brief `mw::com` (LoLa) based implementation of the Gateway IPC Binding.
///
/// \details See `docs/mw_com_binding.rst`. This implementation provides the same public interfaces
///          `Gateway_ipc_binding_client` and `Gateway_ipc_binding_server` as the
///          `score::message_passing` based one, but is constructed through the factory functions
///          below instead of through the `create()` methods of those interfaces.
///
/// \note    Method calls on bridged services are not implemented: `GenericProxy` and
///          `GenericSkeleton` are event only. So are requested event updates, because LoLa has no
///          pull API on the proxy side. Both are logged and ignored.
namespace score::gateway_ipc_binding::mw_com {

/// \brief Bytes reserved at the front of every event sample for binding metadata.
inline constexpr std::size_t kSample_prefix_size = 8U;

/// \brief Alignment requested for every event sample.
inline constexpr std::size_t kSample_alignment = 8U;

/// \brief Which side of a bridged service this peer implements.
enum class Role : std::uint8_t {
    /// This peer consumes the service locally via SOCom and offers it via a GenericSkeleton.
    provider,
    /// This peer receives the service via a GenericProxy and offers it locally via SOCom.
    consumer,
};

/// \brief One event of a bridged service.
/// \details The position in Service_config::events is the socom::Event_id.
struct Event_config {
    /// \brief mw::com event name, must match mw_com_config.json.
    std::string name;
    /// \brief Reserved leading bytes, e.g. score::someip::kSomeipFullHeaderSize.
    std::size_t header_size;
    /// \brief Largest payload this event can carry.
    std::size_t max_payload_size;
};

/// \brief One bridged service instance.
struct Service_config {
    socom::Service_interface_identifier interface;
    socom::Service_instance instance;
    /// \brief mw::com InstanceSpecifier of the gatewayd/someipd instance for this bridged service.
    std::string instance_specifier;
    Role role;
    /// \brief Events in socom::Event_id order.
    std::vector<Event_config> events;
    /// \brief max_sample_count passed to GenericProxyEvent::Subscribe on the consumer side.
    std::size_t max_sample_count;
};

/// \brief Bridged service instances. Owning value type, unbounded in size.
using Service_configs = std::vector<Service_config>;

/// \brief Sample size derived from an Event_config, i.e. the DataTypeMetaInfo::size.
/// \details The layout is `kSample_prefix_size` bytes of binding metadata, followed by
///          `header_size` reserved bytes for the SOME/IP header, followed by up to
///          `max_payload_size` payload bytes. The result is rounded up to `kSample_alignment`,
///          because a mw::com `DataTypeMetaInfo::size` must be an integer multiple of its
///          alignment. The padding at the end is never used.
/// \param event Event configuration to size
/// \return Number of bytes one sample of this event occupies
std::size_t sample_size(Event_config const& event) noexcept;

/// \brief Create the mw::com backed binding behind the client interface.
/// \details Consumes SomeipdService and reports it through is_connected(). Service discovery for
///          SomeipdService is started during construction, so is_connected() may still be false
///          when this function returns and becomes true asynchronously.
///
///          Every bridged service in `services` is set up eagerly, as described in
///          `docs/mw_com_binding.rst`: a `Role::provider` entry creates the `GenericSkeleton` and
///          the SOCom client connector, a `Role::consumer` entry creates the SOCom server
///          connector and starts service discovery. Their availability is independent of
///          is_connected() and is signalled through the normal SOCom service state.
/// \param runtime SOCom runtime used to create the connectors
/// \param someipd_service_specifier mw::com InstanceSpecifier of the SomeipdService instance that
///        the peer provides. Must be unique per gatewayd/someipd pair on a host, and must be the
///        same value that the peer passes to create_server().
/// \param services Bridged service instances, see D4: this set is fixed for the process lifetime
/// \param identifier Optional string used for logging only
/// \return Nullptr if service discovery for SomeipdService could not be started or if any
///         configured service could not be set up
std::unique_ptr<Gateway_ipc_binding_client> create_client(
    score::socom::Runtime& runtime, std::string someipd_service_specifier,
    Service_configs services = {}, std::string_view identifier = {}) noexcept;

/// \brief Create the mw::com backed binding behind the server interface.
/// \details Provides SomeipdService. Setup is deferred to Gateway_ipc_binding_server::start(),
///          which offers SomeipdService first and only then sets up the bridged services, so that
///          a peer that sees SomeipdService can rely on this side accepting further setup.
/// \param runtime SOCom runtime used to create the connectors
/// \param someipd_service_specifier mw::com InstanceSpecifier of the SomeipdService instance to
///        provide, see create_client()
/// \param services Bridged service instances
/// \return Nullptr if `someipd_service_specifier` is empty. Errors that can only be detected once
///         mw::com is asked to create the instances, such as a specifier missing from
///         mw_com_config.json, are reported by start().
std::unique_ptr<Gateway_ipc_binding_server> create_server(score::socom::Runtime& runtime,
                                                          std::string someipd_service_specifier,
                                                          Service_configs services = {}) noexcept;

}  // namespace score::gateway_ipc_binding::mw_com

#endif  // SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING_MW_COM
