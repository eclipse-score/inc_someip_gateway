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

#ifndef SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING
#define SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING

#include <cstddef>
#include <cstdint>
#include <score/span.hpp>
#include <string_view>
#include <type_traits>

#include "score/gateway_ipc_binding/fixed_size_container.hpp"
#include "score/socom/event.hpp"
#include "score/socom/method.hpp"
#include "score/socom/service_interface_identifier.hpp"

namespace score::gateway_ipc_binding {

/// \brief Maximum find service elements
inline constexpr std::size_t kMax_find_service_elements = 4U;

/// \brief Maximum bytes for serialized service id
inline constexpr std::size_t kMax_service_id_size = 64U;

/// \brief Maximum bytes for serialized instance id
inline constexpr std::size_t kMax_instance_id_size = 64U;
/// \brief Maximum bytes for client identifier string (including null terminator)
inline constexpr std::size_t kMax_client_identifier_size = 64U;
/// \brief Maximum shared memory path length (including null terminator)
/// \details Deliberately decoupled from the platform's NAME_MAX (511 on QNX, 255 on Linux):
/// real shared memory names built by make_shared_memory_path() are well under 100 bytes, and
/// tying this to NAME_MAX would make the Connect message both platform-dependent in size and
/// unnecessarily large (risking exceeding the maximum safely sendable message size on platforms
/// with a large NAME_MAX, notably QNX).
inline constexpr std::size_t kMax_shared_memory_path_size = 128U;

/// \brief Service id in fixed-size form
using Service_id = Fixed_string<kMax_service_id_size>;

/// \brief Service descriptor, POD-friendly representation of socom Service
struct Service {
    Service_id service_id;
    socom::Service_interface_identifier::Version version;

    socom::Service_interface_identifier to_socom_identifier() const noexcept;
};

/// \brief Compares two Service objects for equality
bool operator==(Service const& lhs, Service const& rhs);

/// \brief Hash function for Service to be used in unordered containers
struct Service_hash {
    std::size_t operator()(Service const& s) const noexcept;
};

/// \brief Instance identifier in fixed-size form
using Instance_id = Fixed_string<kMax_instance_id_size>;

/// \brief Peer identifier string, sent by the client during Connect
using Client_identifier = Fixed_string<kMax_client_identifier_size>;

Service make_service(score::socom::Service_interface_identifier const& interface) noexcept;

Instance_id make_instance_id(score::socom::Service_instance const& instance) noexcept;

/// \brief Method identifier
using Method_id = socom::Method_id;

/// \brief Event identifier
using Event_id = socom::Event_id;

/// \brief Handle to locate payload in shared memory
struct Shared_memory_handle {
    std::size_t slot_index;
    std::size_t used_bytes;
};

bool operator==(Shared_memory_handle const& lhs, Shared_memory_handle const& rhs) noexcept;

/// \brief Path to shared memory, in fixed-size form
using Shared_memory_path = Fixed_string<kMax_shared_memory_path_size>;

/// \brief Builds the shared memory object name for a service instance
///
/// A POSIX shared memory name carries at most a leading slash, so a service_type_name
/// spelled as a namespaced path (for example "/a/b/C") cannot be embedded verbatim: the
/// name would be read as a path, and both shm_open and the lock file mw::com derives from
/// it would fail. Every slash is therefore replaced by an underscore. Names without
/// slashes are unaffected.
///
/// \return The name, or fixed_size_container_too_small if it exceeds kMax_shared_memory_path_size
Result<Shared_memory_path> make_shared_memory_path(std::string_view service_type_name,
                                                   std::uint16_t service_id) noexcept;

/// \brief Builds the counterpart shared memory object name for a service instance
/// \see make_shared_memory_path
Result<Shared_memory_path> make_counterpart_shared_memory_path(std::string_view service_type_name,
                                                               std::uint16_t service_id) noexcept;

/// \brief Metadata needed to map and interpret peer shared memory
struct Shared_memory_metadata {
    Shared_memory_path path;
    std::size_t slot_size;
    std::size_t slot_count;
};

bool operator==(Shared_memory_metadata const& lhs, Shared_memory_metadata const& rhs) noexcept;

struct Service_instance {
    Service service;
    Instance_id instance_id;
};

bool operator==(Service_instance const& lhs, Service_instance const& rhs) noexcept;

using Find_service_elements = Fixed_size_container<Service_instance, kMax_find_service_elements>;

/// \brief Shared memory configuration for a single service instance,
/// sent by the client to the server in the Connect message.
struct Service_shared_memory_config {
    Service service;
    Instance_id instance_id;
    Shared_memory_metadata metadata;
};

bool operator==(Service_shared_memory_config const& lhs,
                Service_shared_memory_config const& rhs) noexcept;

/// \brief Container of shared memory configurations, one entry per service instance
/// that the server should be able to allocate shared memory for.
using Shared_memory_configs =
    Fixed_size_container<Service_shared_memory_config, kMax_find_service_elements>;

static_assert(std::is_trivially_copyable_v<Fixed_string<kMax_service_id_size>>);
static_assert(std::is_trivially_copyable_v<Client_identifier>);
static_assert(std::is_trivially_copyable_v<Service>);
static_assert(std::is_trivially_copyable_v<Shared_memory_handle>);
static_assert(std::is_trivially_copyable_v<Shared_memory_metadata>);
static_assert(std::is_trivially_copyable_v<Service_shared_memory_config>);

}  // namespace score::gateway_ipc_binding

#endif  // SCORE_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_GATEWAY_IPC_BINDING
