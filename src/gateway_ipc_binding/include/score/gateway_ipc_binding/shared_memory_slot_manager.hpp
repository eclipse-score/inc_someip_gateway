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

#ifndef SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_SHARED_MEMORY_SLOT_MANAGER
#define SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_SHARED_MEMORY_SLOT_MANAGER

#include <score/result/result.h>

#include <cstddef>
#include <map>
#include <memory>
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/socom/payload.hpp>
#include <score/socom/service_interface_identifier.hpp>
#include <score/span.hpp>
#include <string>

namespace score {
namespace memory {
namespace shared {
class ISharedMemoryResource;
}  // namespace shared
}  // namespace memory
}  // namespace score

namespace score::gateway_ipc_binding {

/// \brief Handle identifying a shared memory slot
using Slot_handle = std::size_t;

/// \brief Byte used for shared memory payloads
using Byte = std::byte;

class Shared_memory_slot_manager;

/// \brief RAII wrapper for automatic slot release
///
/// This class provides automatic slot cleanup using RAII pattern.
/// When the guard goes out of scope, it automatically releases the slot.
/// A guard always represents a valid slot until it has been moved or destroyed.
///
/// Example usage:
/// \code
///   auto manager_result = Shared_memory_slot_manager::create("/my_shm", 256, 4096);
///   if (!manager_result) {
///     return;
///   }
///   auto& manager = **manager_result;
///   auto guard_opt = manager.allocate_slot();
///   if (guard_opt) {
///     auto& guard = *guard_opt;
///     // Use guard.get_memory()
///     auto shared_guard = guard.share();  // Share with another consumer
///     // Slot automatically released when guard goes out of scope
///   }
/// \endcode
class Shared_memory_slot_guard {
   private:
    /// \brief Invalid state constructor
    Shared_memory_slot_guard() noexcept;

    /// \brief Private constructor with pre-allocated slot
    ///
    /// Takes ownership of an already allocated slot. Only accessible by
    /// Shared_memory_slot_manager and for internal use.
    ///
    /// \param manager Reference to the slot manager
    /// \param handle Pre-allocated slot handle
    /// \param memory Pointer to the slot's memory region
    /// \param size Size of the slot in bytes
    Shared_memory_slot_guard(Shared_memory_slot_manager& manager, Slot_handle handle) noexcept;

    friend class Shared_memory_slot_manager;

   public:
    Shared_memory_slot_guard(Shared_memory_slot_guard const&) = delete;
    Shared_memory_slot_guard& operator=(Shared_memory_slot_guard const&) = delete;

    /// \brief Move constructor
    Shared_memory_slot_guard(Shared_memory_slot_guard&& other) noexcept;

    /// \brief Move assignment operator
    Shared_memory_slot_guard& operator=(Shared_memory_slot_guard&& other) noexcept;

    /// \brief Destructor
    ///
    /// Automatically releases the slot if valid.
    ~Shared_memory_slot_guard() noexcept;

    /// \brief Share the slot with another consumer
    ///
    /// Increments the reference count and returns a new guard that manages
    /// a reference to the same slot. The returned guard will automatically
    /// release its reference when destroyed.
    ///
    /// \return A new Shared_memory_slot_guard managing a reference to this slot,
    ///         or an invalid guard if this guard does not hold a valid slot
    [[nodiscard]] Shared_memory_slot_guard share() noexcept;

    /// \brief Check if this guard holds a valid slot
    ///
    /// \return true if a valid slot is held
    [[nodiscard]] bool has_slot() const noexcept;

    /// \brief Get the slot handle
    ///
    /// \return Slot handle, or an error if no valid slot is held
    [[nodiscard]] Result<Slot_handle> get_handle() const noexcept;

    /// \brief Get the memory span for this slot
    ///
    /// \return Span of slot memory, or empty span if no valid slot
    [[nodiscard]] score::cpp::span<Byte> get_memory() const noexcept;

    /// \brief Release the slot early (before destructor)
    ///
    /// After calling this, the guard no longer holds a valid slot.
    void reset() noexcept;

    /// \brief Release ownership of the slot without releasing it to the manager
    ///
    /// Transfers ownership of the slot to the caller. The guard will no longer
    /// manage the slot, and the slot will NOT be released back to the manager's
    /// pool. This is similar to std::unique_ptr::release().
    ///
    /// The caller becomes responsible for managing the slot's lifecycle and
    /// calling release_slot() on the manager when done.
    ///
    /// \return The slot handle, or an error if no valid slot is held
    [[nodiscard]] Result<Slot_handle> release() noexcept;

   private:
    Shared_memory_slot_manager* m_manager;
    Slot_handle m_handle;
};

/// \brief Manages fixed-size slots of shared memory with reference counting
///
/// This class allocates a large chunk of shared memory and divides it into
/// fixed-size slots. Each slot can be allocated and shared among multiple
/// consumers using reference counting. When all consumers release a slot,
/// it becomes available for reuse.
///
/// Thread-safe: All operations are protected by internal mutex.
///
/// Example usage:
/// \code
///   auto manager_result = Shared_memory_slot_manager::create("/my_shm", 256, 4096);
///   if (!manager_result) {
///     return;
///   }
///   auto& manager = **manager_result;
///   auto guard_opt = manager.allocate_slot();
///   if (guard_opt) {
///     auto& guard = *guard_opt;
///     // Write data to guard.get_memory()
///     auto shared_guard = guard.share();  // Share with another consumer
///   }  // Guards automatically release slots when destroyed
/// \endcode
class Shared_memory_slot_manager {
   public:
    using Uptr = std::unique_ptr<Shared_memory_slot_manager>;

    Shared_memory_slot_manager() = default;

    Shared_memory_slot_manager(Shared_memory_slot_manager const&) = delete;
    Shared_memory_slot_manager(Shared_memory_slot_manager&&) = delete;
    Shared_memory_slot_manager& operator=(Shared_memory_slot_manager const&) = delete;
    Shared_memory_slot_manager& operator=(Shared_memory_slot_manager&&) = delete;

    /// \brief Destructor
    ///
    /// Cleans up shared memory resources.
    virtual ~Shared_memory_slot_manager() noexcept = default;

    /// \brief Allocate a slot from the pool
    ///
    /// Finds the first available slot, marks it as allocated with reference
    /// count 1, and returns a guard managing the slot.
    ///
    /// \return Guard managing allocated slot if successful, or an error if no slots available
    [[nodiscard]] virtual Result<Shared_memory_slot_guard> allocate_slot() noexcept = 0;

    /// \brief Add a consumer to an allocated slot
    ///
    /// Increments the reference count for the slot. Use this when sharing
    /// a slot with additional consumers. Each add_consumer() call must be
    /// matched with a corresponding release_slot() call.
    ///
    /// \param handle Slot handle obtained from allocate_slot()
    /// \return Result<void> - success if consumer added, error if handle is invalid or slot is not
    /// allocated
    [[nodiscard]] virtual Result<void> add_consumer(Slot_handle handle) noexcept = 0;

    /// \brief Release a slot
    ///
    /// Decrements the reference count for the slot. When the count reaches
    /// zero, the slot is marked as free and becomes available for reuse.
    ///
    /// \param handle Slot handle to release
    /// \return Result<void> - success if slot released, error if handle is invalid or slot is not
    /// allocated
    [[nodiscard]] virtual Result<void> release_slot(Slot_handle handle) noexcept = 0;

    /// \brief Get the number of slots managed by this instance
    ///
    /// \return Total number of slots
    [[nodiscard]] virtual std::size_t get_slot_count() const noexcept = 0;

    /// \brief Get the size of each slot in bytes
    ///
    /// \return Slot size in bytes
    [[nodiscard]] virtual std::size_t get_slot_size() const noexcept = 0;

    /// \brief Get the number of currently allocated slots
    ///
    /// \return Number of slots with reference count > 0
    [[nodiscard]] virtual std::size_t get_allocated_slot_count() const noexcept = 0;

    /// \brief Get the reference count for a specific slot
    ///
    /// \param handle Slot handle
    /// \return Reference count if valid, 0 if invalid handle or not allocated
    [[nodiscard]] virtual std::size_t get_reference_count(Slot_handle handle) const noexcept = 0;

    /// \brief Get the memory span for a specific slot
    ///
    /// \param handle Slot handle
    /// \return Span of slot memory if valid and allocated, or an error otherwise
    [[nodiscard]] virtual Result<score::cpp::span<Byte>> get_memory(
        Slot_handle handle) const noexcept = 0;

    /// \brief Returns the path to the shared memory
    [[nodiscard]] virtual std::string get_path() const noexcept = 0;

   protected:
    [[nodiscard]] static Shared_memory_slot_guard create_slot_guard(
        Shared_memory_slot_manager& manager, Slot_handle handle) noexcept;
};

/// \brief Read-only interface to shared memory slots
/// Service consumers use this to access shared memory of events.
class Read_only_shared_memory_slot_manager {
   public:
    using Uptr = std::unique_ptr<Read_only_shared_memory_slot_manager>;

    /// \brief Callback for notifying when a payload is destroyed, allowing cleanup of associated
    /// resources
    using On_payload_destruction_callback = std::function<void()>;

    Read_only_shared_memory_slot_manager() = default;

    virtual ~Read_only_shared_memory_slot_manager() = default;

    /// \brief Get a payload for the given shared memory handle
    /// This method returns a payload object that provides access to the shared memory region
    /// associated with the given handle. The payload object will call the provided callback when it
    /// is destroyed, allowing the caller to perform any necessary cleanup or reference count
    /// management.
    /// \param handle Shared memory handle identifying the slot
    /// \param callback Callback to be called when the returned payload is destroyed
    /// \return Result containing a shared pointer to the payload on success, or a nullptr on
    /// failure
    virtual socom::Payload::Uptr get_payload(
        Shared_memory_handle handle, On_payload_destruction_callback callback) const noexcept = 0;
};

/// \brief Factory for creating Shared_memory_slot_manager instances
///
/// The assumption is that each service instance has its own shared memory.
///
class Shared_memory_manager_factory {
   public:
    using Uptr = std::unique_ptr<Shared_memory_manager_factory>;
    using Sptr = std::shared_ptr<Shared_memory_manager_factory>;

    virtual ~Shared_memory_manager_factory() = default;

    using Shared_memory_configuration =
        std::map<score::socom::Service_interface_identifier,
                 std::map<score::socom::Service_instance, Shared_memory_metadata>>;

    /// \brief Create a concrete Shared_memory_manager_factory
    ///
    /// The returned factory creates writable slot managers for local service instances
    /// and opens read-only slot managers for peer service instances.
    ///
    /// \param configuration Configuration mapping service interfaces and instances to shared memory
    /// metadata
    /// \return unique_ptr to the concrete factory
    [[nodiscard]] static Uptr create(Shared_memory_configuration configuration) noexcept;

    /// \brief Create a Shared_memory_slot_manager with specified parameters
    /// \param interface Service interface for which the slot manager is being created
    /// \param instance Service instance for which the slot manager is being created
    /// \return Result containing a unique pointer to the created Shared_memory_slot_manager
    virtual Result<Shared_memory_slot_manager::Uptr> create(
        score::socom::Service_interface_identifier const& interface,
        score::socom::Service_instance const& instance) noexcept = 0;

    /// \brief Open a Shared_memory_slot_manager for existing read only shared memory
    /// \param metadata Metadata describing the shared memory to open
    /// \return Result containing a unique pointer to the opened
    /// Read_only_shared_memory_slot_manager on success, or an error on failure
    virtual Result<Read_only_shared_memory_slot_manager::Uptr> open(
        Shared_memory_metadata const& metadata) noexcept = 0;
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_SHARED_MEMORY_SLOT_MANAGER
