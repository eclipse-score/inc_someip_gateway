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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_KEY
#define SRC_GATEWAY_IPC_BINDING_SRC_KEY

#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/socom/service_interface_definition.hpp>
#include <unordered_map>

#include "gateway_ipc_binding_util.hpp"

namespace score::gateway_ipc_binding {

using Key_t = std::size_t;

/// \brief Mapping of service/instance pairs to unique keys and back.
///
/// A service is uniquely identified by the combination of its interface and instance, which are
/// both strings. To efficiently manage and look up services, we assign a unique Key_t to each
/// service/instance pair.
///
/// The keys are then used in IPC messages and internal data structures to refer to services without
/// needing to copy large strings around.
class Keys {
    using Instance_to_key_map = std::unordered_map<Instance_id, Key_t, Fixed_size_container_hash>;
    using Service_to_instance_key_map =
        std::unordered_map<Service, Instance_to_key_map, Service_hash>;

    Service_to_instance_key_map m_keys;
    Id_generator<Key_t> m_next_key{0};

   public:
    /// \brief Get the unique key for a given service and instance, creating a new key if it doesn't
    ///        exist yet.
    /// \param service The service interface identifier.
    /// \param instance The service instance identifier.
    /// \return The unique key associated with the service/instance pair.
    Key_t const& get(Service const& service, Instance_id const& instance) {
        auto const service_it = m_keys.find(service);
        if (std::end(m_keys) != service_it) {
            auto const instance_it = service_it->second.find(instance);
            if (std::end(service_it->second) != instance_it) {
                return instance_it->second;
            }
        }

        return m_keys[service][instance] = m_next_key.get_next_id();
    }

    /// \brief Get the unique key for a given service and instance, creating a new key if it doesn't
    ///        exist yet.
    /// \param configuration The service interface identifier.
    /// \param instance The service instance identifier.
    /// \return The unique key associated with the service/instance pair.
    Key_t const& get(score::socom::Service_interface_identifier const& configuration,
                     score::socom::Service_instance const& instance) {
        return get(make_service(configuration), make_instance_id(instance));
    }

    /// \brief Get the unique key for a given service and instance, creating a new key if it doesn't
    ///        exist yet.
    /// \param configuration The service interface definition.
    /// \param instance The service instance identifier.
    /// \return The unique key associated with the service/instance pair.
    Key_t const& get(score::socom::Service_interface_definition const& configuration,
                     score::socom::Service_instance const& instance) {
        return get(configuration.interface, instance);
    }

    /// \brief Get the service and instance associated with a given key.
    /// \param key The unique key for a service/instance pair.
    /// \return An optional containing the service and instance if the key exists, or std::nullopt
    ///         if the key is not found.
    std::optional<std::tuple<std::reference_wrapper<Service const>,
                             std::reference_wrapper<Instance_id const>>>
    get(Key_t const& key) const {
        for (const auto& [service, instance_map] : m_keys) {
            for (const auto& [instance, instance_key] : instance_map) {
                if (instance_key == key) {
                    return {{std::cref(service), std::cref(instance)}};
                }
            }
        }
        return std::nullopt;
    }
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_KEY
