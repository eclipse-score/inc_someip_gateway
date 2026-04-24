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

#include <cassert>
#include <score/gateway_ipc_binding/shared_memory_slot_manager.hpp>
#include <utility>

#include "score/gateway_ipc_binding/error.hpp"
#include "score/memory/shared/shared_memory_factory.h"

namespace score::gateway_ipc_binding {

namespace {

class Shared_memory_slot_manager_impl final : public Shared_memory_slot_manager {
   public:
    Shared_memory_slot_manager_impl(
        std::size_t slot_size, std::size_t slot_count,
        std::shared_ptr<score::memory::shared::ISharedMemoryResource> shared_memory,
        void* base_address)
        : m_slot_size(slot_size),
          m_slot_count(slot_count),
          m_shared_memory(std::move(shared_memory)),
          m_base_address(base_address) {
        assert(m_slot_size > 0);
        assert(m_slot_count > 0);
        assert(m_shared_memory != nullptr);
        assert(m_base_address != nullptr);

        m_slots = std::make_unique<Slot_metadata[]>(m_slot_count);
        for (std::size_t i = 0; i < m_slot_count; ++i) {
            m_slots[i].reference_count.store(0, std::memory_order_relaxed);
            m_slots[i].offset = i * m_slot_size;
        }
    }

    ~Shared_memory_slot_manager_impl() noexcept override {
        m_shared_memory->UnlinkFilesystemEntry();
    }

    Result<Shared_memory_slot_guard> allocate_slot() noexcept override {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (std::size_t i = 0; i < m_slot_count; ++i) {
            std::uint32_t expected = 0;
            if (m_slots[i].reference_count.compare_exchange_strong(
                    expected, 1, std::memory_order_acquire, std::memory_order_relaxed)) {
                return Shared_memory_slot_manager::create_slot_guard(*this, i);
            }
        }

        return MakeUnexpected(Shared_memory_manager_error::runtime_error_no_available_slots);
    }

    Result<void> add_consumer(Slot_handle handle) noexcept override {
        if (handle >= m_slot_count) {
            return MakeUnexpected(Shared_memory_manager_error::logic_error_invalid_slot_handle);
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        std::uint32_t current = m_slots[handle].reference_count.load(std::memory_order_acquire);
        if (current == 0) {
            return MakeUnexpected(Shared_memory_manager_error::runtime_error_slot_not_allocated);
        }

        m_slots[handle].reference_count.fetch_add(1, std::memory_order_release);
        return {};
    }

    Result<void> release_slot(Slot_handle handle) noexcept override {
        if (handle >= m_slot_count) {
            return MakeUnexpected(Shared_memory_manager_error::logic_error_invalid_slot_handle);
        }

        std::lock_guard<std::mutex> lock(m_mutex);

        std::uint32_t current = m_slots[handle].reference_count.load(std::memory_order_acquire);
        if (current == 0) {
            return MakeUnexpected(Shared_memory_manager_error::runtime_error_slot_not_allocated);
        }

        std::uint32_t new_count =
            m_slots[handle].reference_count.fetch_sub(1, std::memory_order_release);
        assert(new_count > 0);
        return {};
    }

    std::size_t get_slot_count() const noexcept override { return m_slot_count; }

    std::size_t get_slot_size() const noexcept override { return m_slot_size; }

    std::size_t get_allocated_slot_count() const noexcept override {
        std::lock_guard<std::mutex> lock(m_mutex);

        std::size_t count = 0;
        for (std::size_t i = 0; i < m_slot_count; ++i) {
            if (m_slots[i].reference_count.load(std::memory_order_acquire) > 0) {
                ++count;
            }
        }
        return count;
    }

    std::size_t get_reference_count(Slot_handle handle) const noexcept override {
        if (handle >= m_slot_count) {
            return 0;
        }

        return m_slots[handle].reference_count.load(std::memory_order_acquire);
    }

    Result<score::cpp::span<Byte>> get_memory(Slot_handle handle) const noexcept override {
        if (handle >= m_slot_count) {
            return MakeUnexpected(Shared_memory_manager_error::logic_error_invalid_slot_handle);
        }

        if (get_reference_count(handle) == 0) {
            return MakeUnexpected(Shared_memory_manager_error::runtime_error_slot_not_allocated);
        }

        void* memory =
            static_cast<void*>(static_cast<char*>(m_base_address) + m_slots[handle].offset);
        return score::cpp::span<Byte>(static_cast<Byte*>(memory), m_slot_size);
    }

    [[nodiscard]] std::string get_path() const noexcept override {
        assert(m_shared_memory->getPath() != nullptr);
        return *m_shared_memory->getPath();
    }

   private:
    struct Slot_metadata {
        std::atomic<std::uint32_t> reference_count{0};
        std::size_t offset{0};
    };

    std::size_t m_slot_size;
    std::size_t m_slot_count;
    std::shared_ptr<score::memory::shared::ISharedMemoryResource> m_shared_memory;
    void* m_base_address;
    std::unique_ptr<Slot_metadata[]> m_slots;
    mutable std::mutex m_mutex;
};

/// \brief Payload backed by a read-only view of a shared memory slot.
///
/// Calls the supplied destruction callback when the last reference is released,
/// allowing the caller to send a Payload_consumed notification.
class Read_only_shared_memory_payload final : public socom::Payload {
   public:
    Read_only_shared_memory_payload(
        void const* base, Slot_handle slot_index, std::size_t slot_size, std::uint32_t used_bytes,
        Read_only_shared_memory_slot_manager::On_payload_destruction_callback callback) noexcept
        : m_data(static_cast<Byte const*>(base) + slot_index * slot_size,
                 static_cast<std::ptrdiff_t>(
                     std::min(static_cast<std::size_t>(used_bytes), slot_size))),
          m_slot_index(slot_index),
          m_callback(std::move(callback)) {
        assert(m_callback);
    }

    ~Read_only_shared_memory_payload() noexcept override {
        assert(m_callback);
        m_callback();
    }

    Read_only_shared_memory_payload(Read_only_shared_memory_payload const&) = delete;
    Read_only_shared_memory_payload& operator=(Read_only_shared_memory_payload const&) = delete;
    Read_only_shared_memory_payload(Read_only_shared_memory_payload&&) = delete;
    Read_only_shared_memory_payload& operator=(Read_only_shared_memory_payload&&) = delete;

    [[nodiscard]] Span data() const noexcept override { return m_data; }
    [[nodiscard]] Span header() const noexcept override { return {}; }
    [[nodiscard]] Writable_span header() noexcept override { return {}; }
    [[nodiscard]] std::size_t get_slot_handle() const noexcept override { return m_slot_index; }

   private:
    Span m_data;
    Slot_handle m_slot_index;
    Read_only_shared_memory_slot_manager::On_payload_destruction_callback m_callback;
};

class Read_only_shared_memory_slot_manager_impl final
    : public Read_only_shared_memory_slot_manager {
   public:
    Read_only_shared_memory_slot_manager_impl(
        std::shared_ptr<score::memory::shared::ISharedMemoryResource> shared_memory,
        std::size_t slot_size, std::size_t slot_count) noexcept
        : m_shared_memory(std::move(shared_memory)),
          m_base_address(m_shared_memory->getUsableBaseAddress()),
          m_slot_size(slot_size),
          m_slot_count(slot_count) {
        assert(m_shared_memory != nullptr);
        assert(m_base_address != nullptr);
        assert(m_slot_size > 0);
        assert(m_slot_count > 0);
    }

    ~Read_only_shared_memory_slot_manager_impl() noexcept override {
        m_shared_memory->UnlinkFilesystemEntry();
    }

    socom::Payload::Uptr get_payload(
        Shared_memory_handle handle,
        On_payload_destruction_callback callback) const noexcept override {
        if (handle.slot_index >= m_slot_count) {
            return nullptr;
        }

        return std::make_unique<Read_only_shared_memory_payload>(
            m_base_address, handle.slot_index, m_slot_size, handle.used_bytes, std::move(callback));
    }

   private:
    std::shared_ptr<score::memory::shared::ISharedMemoryResource> m_shared_memory;
    void const* m_base_address;
    std::size_t m_slot_size;
    std::size_t m_slot_count;
};

Result<std::unique_ptr<Shared_memory_slot_manager>> create_shared_memory_slot_manager(
    std::string const& path, std::size_t slot_count, std::size_t slot_size) noexcept {
    // Validate parameters
    if (slot_size == 0) {
        return MakeUnexpected(Shared_memory_manager_error::logic_error_invalid_slot_size);
    }

    if (slot_count == 0) {
        return MakeUnexpected(Shared_memory_manager_error::logic_error_invalid_slot_count);
    }

    auto total_size = slot_count * slot_size;

    // Create shared memory
    score::memory::shared::SharedMemoryFactory factory;
    factory.RemoveStaleArtefacts(path);

    score::memory::shared::SharedMemoryFactory::InitializeCallback init_callback =
        [](std::shared_ptr<score::memory::shared::ISharedMemoryResource> /* resource */) {
            // No special initialization needed
        };

    auto shared_memory = factory.Create(path, std::move(init_callback), total_size);

    if (!shared_memory) {
        return MakeUnexpected(
            Shared_memory_manager_error::runtime_error_shared_memory_allocation_failed);
    }

    // Allocate the entire memory block
    auto base_address = shared_memory->allocate(total_size);

    if (!base_address) {
        return MakeUnexpected(
            Shared_memory_manager_error::runtime_error_shared_memory_allocation_failed);
    }

    return std::make_unique<Shared_memory_slot_manager_impl>(
        slot_size, slot_count, std::move(shared_memory), base_address);
}

class Shared_memory_manager_factory_impl final : public Shared_memory_manager_factory {
   public:
    explicit Shared_memory_manager_factory_impl(Shared_memory_configuration configuration) noexcept
        : m_configuration(std::move(configuration)) {}

    Result<Shared_memory_slot_manager::Uptr> create(
        score::socom::Service_interface_identifier const& interface,
        score::socom::Service_instance const& instance) noexcept override {
        auto interface_it = m_configuration.find(interface);
        if (interface_it == m_configuration.end()) {
            return MakeUnexpected(
                Shared_memory_manager_error::logic_error_no_configuration_for_interface);
        }
        auto instance_it = interface_it->second.find(instance);
        if (instance_it == interface_it->second.end()) {
            return MakeUnexpected(
                Shared_memory_manager_error::logic_error_no_configuration_for_instance);
        }

        return create_shared_memory_slot_manager(fixed_string_to_string(instance_it->second.path),
                                                 instance_it->second.slot_count,
                                                 instance_it->second.slot_size);
    }

    Result<Read_only_shared_memory_slot_manager::Uptr> open(
        Shared_memory_metadata const& metadata) noexcept override {
        std::string const path = fixed_string_to_string(metadata.path);
        auto shm = score::memory::shared::SharedMemoryFactory::Open(path, /*is_read_write=*/false);
        if (shm == nullptr) {
            return MakeUnexpected(
                Shared_memory_manager_error::runtime_error_shared_memory_allocation_failed);
        }
        return std::make_unique<Read_only_shared_memory_slot_manager_impl>(
            std::move(shm), metadata.slot_size, metadata.slot_count);
    }

   private:
    Shared_memory_configuration m_configuration;
};

}  // namespace

Shared_memory_slot_guard::Shared_memory_slot_guard() noexcept : m_manager(nullptr), m_handle(0) {}

Shared_memory_slot_guard::Shared_memory_slot_guard(Shared_memory_slot_manager& manager,
                                                   Slot_handle handle) noexcept
    : m_manager(&manager), m_handle(handle) {}

Shared_memory_slot_guard::Shared_memory_slot_guard(Shared_memory_slot_guard&& other) noexcept
    : m_manager(other.m_manager), m_handle(other.m_handle) {
    other.m_manager = nullptr;
}

Shared_memory_slot_guard& Shared_memory_slot_guard::operator=(
    Shared_memory_slot_guard&& other) noexcept {
    if (this != &other) {
        // Release current slot if any
        reset();

        // Take ownership of other's slot
        m_manager = other.m_manager;
        m_handle = other.m_handle;

        // Clear other
        other.m_manager = nullptr;
    }
    return *this;
}

Shared_memory_slot_guard::~Shared_memory_slot_guard() noexcept { reset(); }

Shared_memory_slot_guard Shared_memory_slot_guard::share() noexcept {
    assert(has_slot() && "share() called on guard without a valid slot");
    if (!has_slot()) {
        // If adding consumer failed, return an empty guard (no slot)
        return Shared_memory_slot_guard{};
    }

    // Try to add a consumer
    auto guard = m_manager->add_consumer(m_handle).and_then([this]() {
        return Result<Shared_memory_slot_guard>{Shared_memory_slot_guard(*m_manager, m_handle)};
    });

    assert(guard && "Failed to add consumer in share()");
    return std::move(*guard);
}

bool Shared_memory_slot_guard::has_slot() const noexcept { return m_manager != nullptr; }

Result<Slot_handle> Shared_memory_slot_guard::get_handle() const noexcept {
    if (!has_slot()) {
        return MakeUnexpected(Shared_memory_manager_error::runtime_error_slot_not_allocated);
    }
    return m_handle;
}

score::cpp::span<Byte> Shared_memory_slot_guard::get_memory() const noexcept {
    if (!has_slot()) {
        return {};
    }

    return m_manager->get_memory(m_handle).value_or(score::cpp::span<Byte>{});
}

void Shared_memory_slot_guard::reset() noexcept {
    if (has_slot()) {
        static_cast<void>(m_manager->release_slot(m_handle));
        m_manager = nullptr;
    }
}

Result<Slot_handle> Shared_memory_slot_guard::release() noexcept {
    if (!has_slot()) {
        return MakeUnexpected(Shared_memory_manager_error::runtime_error_slot_not_allocated);
    }

    // Transfer ownership to caller - don't release to manager
    m_manager = nullptr;
    return m_handle;
}

Shared_memory_slot_guard Shared_memory_slot_manager::create_slot_guard(
    Shared_memory_slot_manager& manager, Slot_handle handle) noexcept {
    return Shared_memory_slot_guard(manager, handle);
}

Shared_memory_manager_factory::Uptr Shared_memory_manager_factory::create(
    Shared_memory_configuration configuration) noexcept {
    return std::make_unique<Shared_memory_manager_factory_impl>(std::move(configuration));
}

}  // namespace score::gateway_ipc_binding
