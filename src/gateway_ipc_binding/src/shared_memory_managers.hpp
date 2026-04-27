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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_SHARED_MEMORY_MANAGERS
#define SRC_GATEWAY_IPC_BINDING_SRC_SHARED_MEMORY_MANAGERS

#include <cassert>
#include <optional>
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/gateway_ipc_binding/shared_memory_slot_manager.hpp>
#include <unordered_map>

#include "key.hpp"

namespace score::gateway_ipc_binding {
class Shared_memory_managers {
    Shared_memory_manager_factory::Sptr m_slot_manager_factory;
    Keys* m_keys;

    struct Shared_memory_allocation {
        std::optional<socom::Payload> payload;
        std::size_t pending_consumers{0U};
    };

    std::unordered_map<Key_t, Shared_memory_slot_manager::Uptr> m_slot_managers;
    std::unordered_map<Key_t, std::unordered_map<Slot_handle, Shared_memory_allocation>>
        m_shared_memory_allocations;

   public:
    Shared_memory_managers(Shared_memory_manager_factory::Sptr slot_manager_factory, Keys& keys)
        : m_slot_manager_factory(std::move(slot_manager_factory)), m_keys(&keys) {}

    Shared_memory_slot_manager& get_shared_memory_slot_manager(Key_t const& key) noexcept {
        auto it = m_slot_managers.find(key);
        if (it != m_slot_managers.end()) {
            return *(it->second);
        }

        auto const interface_instance_opt = m_keys->get(key);
        assert(interface_instance_opt.has_value() && "Interface and instance should exist for key");
        auto const& [interface, instance] = interface_instance_opt.value();

        // Create new slot manager for this service instance
        auto slot_manager_result = m_slot_manager_factory->create(
            interface.get().to_socom_identifier(),
            socom::Service_instance{fixed_string_to_string(instance.get())});
        assert(slot_manager_result && "Failed to create shared memory slot manager");
        auto& slot_manager = **slot_manager_result;
        m_slot_managers.emplace(key, std::move(*slot_manager_result));
        return slot_manager;
    }

    Shared_memory_metadata get_shared_memory_metadata(
        Shared_memory_slot_manager const& slot_manager) noexcept {
        auto result = fixed_string_from_string<Shared_memory_path>(slot_manager.get_path());
        assert(result && "Path should fit into fixed-size metadata path");

        return Shared_memory_metadata{*result, static_cast<uint32_t>(slot_manager.get_slot_size()),
                                      static_cast<uint32_t>(slot_manager.get_slot_count())};
    }

    Shared_memory_metadata get_shared_memory_metadata(Key_t const& key) noexcept {
        auto& slot_manager = get_shared_memory_slot_manager(key);
        return get_shared_memory_metadata(slot_manager);
    }

    void insert_allocation(Key_t const& key, socom::Payload payload, std::size_t consumer_count) {
        if (consumer_count == 0U) {
            return;
        }

        auto& allocations_by_handle = m_shared_memory_allocations[key];
        auto const slot_handle = payload.get_slot_handle();
        auto& allocation = allocations_by_handle[slot_handle];
        if (!allocation.payload.has_value()) {
            allocation.payload = std::move(payload);
        }
        allocation.pending_consumers += consumer_count;
    }

    void payload_consumed(Key_t const& key, Payload_consumed const& msg) {
        auto allocations_it = m_shared_memory_allocations.find(key);
        if (allocations_it == m_shared_memory_allocations.end()) {
            return;
        }

        auto& allocations_by_handle = allocations_it->second;
        auto allocation_it = allocations_by_handle.find(msg.handle.slot_index);
        if (allocation_it == allocations_by_handle.end()) {
            return;
        }

        auto& allocation = allocation_it->second;
        if (allocation.pending_consumers > 1U) {
            --allocation.pending_consumers;
            return;
        }

        allocations_by_handle.erase(allocation_it);

        if (allocations_by_handle.empty()) {
            m_shared_memory_allocations.erase(allocations_it);
        }
    }
};

class Read_only_memory_managers {
    Shared_memory_manager_factory::Sptr m_slot_manager_factory;
    std::unordered_map<Shared_memory_path, Read_only_shared_memory_slot_manager::Uptr,
                       Fixed_size_container_hash>
        m_read_only_slot_managers;

   public:
    Read_only_memory_managers(Shared_memory_manager_factory::Sptr slot_manager_factory)
        : m_slot_manager_factory{std::move(slot_manager_factory)} {}

    Read_only_shared_memory_slot_manager& get_read_only_shared_memory_slot_manager(
        Shared_memory_metadata const& metadata) noexcept {
        auto it = m_read_only_slot_managers.find(metadata.path);
        if (it != m_read_only_slot_managers.end()) {
            return *(it->second);
        }

        // Create new read-only slot manager for this service instance
        auto slot_manager_result = m_slot_manager_factory->open(metadata);
        assert(slot_manager_result && "Failed to create read-only shared memory slot manager");
        auto& slot_manager = **slot_manager_result;
        m_read_only_slot_managers.emplace(metadata.path, std::move(*slot_manager_result));
        return slot_manager;
    }
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_SHARED_MEMORY_MANAGERS
