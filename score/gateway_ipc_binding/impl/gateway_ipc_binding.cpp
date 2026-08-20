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

#include "score/gateway_ipc_binding/gateway_ipc_binding.hpp"

#include <algorithm>
#include <cassert>
#include <string>
#include <string_view>

namespace score::gateway_ipc_binding {

socom::Service_interface_identifier Service::to_socom_identifier() const noexcept {
    return socom::Service_interface_identifier{fixed_string_to_string(service_id),
                                               {version.major, version.minor}};
}

bool operator==(Service const& lhs, Service const& rhs) {
    return lhs.service_id == rhs.service_id && lhs.version.major == rhs.version.major &&
           lhs.version.minor == rhs.version.minor;
}

std::size_t Service_hash::operator()(Service const& s) const noexcept {
    std::size_t h1 = Fixed_size_container_hash{}(s.service_id);
    std::size_t h2 = std::hash<std::uint16_t>{}(s.version.major);
    std::size_t h3 = std::hash<std::uint16_t>{}(s.version.minor);
    // maybe there is a better way to combine these hashes
    return (h1 ^ (h2 << 1)) ^ (h3 << 2);
}

Service make_service(score::socom::Service_interface_identifier const& interface) noexcept {
    Service service{};
    service.version.major = interface.version.major;
    service.version.minor = interface.version.minor;
    auto const result = fixed_string_from_string<Service_id>(interface.id.string_view());
    assert(result && "Service id exceeds fixed size");
    service.service_id = *result;
    return service;
}

Instance_id make_instance_id(score::socom::Service_instance const& instance) noexcept {
    auto const result = fixed_string_from_string<Instance_id>(instance.id.string_view());
    assert(result && "Instance id exceeds fixed size");
    return *result;
}

namespace {

/// \brief Makes a service_type_name usable inside a POSIX shared memory object name
///
/// Leading slashes are dropped rather than substituted: the caller prepends its own, and
/// a leading slash carries no meaning of its own in a shm name. Substituting them would
/// yield "/_name" and, for the counterpart prefix, a stray "counterpart__name".
std::string flatten_service_type_name(std::string_view service_type_name) {
    auto const first_kept = service_type_name.find_first_not_of('/');
    service_type_name.remove_prefix(std::min(first_kept, service_type_name.size()));

    std::string flattened{service_type_name};
    std::replace(flattened.begin(), flattened.end(), '/', '_');
    return flattened;
}

Result<Shared_memory_path> make_path(std::string_view prefix, std::string_view service_type_name,
                                     std::uint16_t service_id) noexcept {
    std::string path{prefix};
    path.append(flatten_service_type_name(service_type_name))
        .append("_")
        .append(std::to_string(service_id));
    return fixed_string_from_string<Shared_memory_path>(path);
}

}  // namespace

Result<Shared_memory_path> make_shared_memory_path(std::string_view service_type_name,
                                                   std::uint16_t service_id) noexcept {
    return make_path("/", service_type_name, service_id);
}

Result<Shared_memory_path> make_counterpart_shared_memory_path(std::string_view service_type_name,
                                                               std::uint16_t service_id) noexcept {
    return make_path("/counterpart_", service_type_name, service_id);
}

bool operator==(Shared_memory_handle const& lhs, Shared_memory_handle const& rhs) noexcept {
    return lhs.slot_index == rhs.slot_index && lhs.used_bytes == rhs.used_bytes;
}

bool operator==(Shared_memory_metadata const& lhs, Shared_memory_metadata const& rhs) noexcept {
    return lhs.slot_count == rhs.slot_count && lhs.slot_size == rhs.slot_size &&
           lhs.path.size == rhs.path.size &&
           std::equal(lhs.path.data.data(), lhs.path.data.data() + lhs.path.size,
                      rhs.path.data.data(), rhs.path.data.data() + rhs.path.size);
}

bool operator==(Service_instance const& lhs, Service_instance const& rhs) noexcept {
    return lhs.service == rhs.service && lhs.instance_id == rhs.instance_id;
}

bool operator==(Service_shared_memory_config const& lhs,
                Service_shared_memory_config const& rhs) noexcept {
    return lhs.service == rhs.service && lhs.instance_id == rhs.instance_id &&
           lhs.metadata == rhs.metadata;
}

}  // namespace score::gateway_ipc_binding
