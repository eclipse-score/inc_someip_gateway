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

#ifndef SCORE_SOCOM_PAYLOAD_HPP
#define SCORE_SOCOM_PAYLOAD_HPP

#include <cstddef>
#include <limits>
#include <score/move_only_function.hpp>
#include <score/span.hpp>

namespace score::socom {

/// \brief Sentinel value indicating that a payload is not associated with a shared memory slot.
constexpr std::size_t kNoSlotHandle = std::numeric_limits<std::size_t>::max();

namespace detail {
class Payload_impl final {
   public:
    /// \brief Alias for a data byte.
    using Byte = std::byte;

    /// \brief Alias for payload data.
    using Span = score::cpp::span<Byte const>;

    /// \brief Alias for writable payload data.
    using Writable_span = score::cpp::span<Byte>;

    /// \brief Called when Payload_impl and memory is released. Can own the memory if it is heap
    /// allocated.
    using Payload_destroyed = score::cpp::move_only_function<void(), 48>;

    Payload_impl() = default;

    /// \brief Construct new read-only instance.
    Payload_impl(Span data, std::size_t slot_handle, Payload_destroyed payload_destroyed) noexcept
        : m_data(data),
          m_slot_handle(slot_handle),
          m_payload_destroyed(std::move(payload_destroyed)) {}

    /// \brief Construct new writable instance.
    Payload_impl(Writable_span data, std::size_t slot_handle,
                 Payload_destroyed payload_destroyed) noexcept
        : m_data(data),
          m_writable_data(data.data()),
          m_slot_handle(slot_handle),
          m_payload_destroyed(std::move(payload_destroyed)) {}

    ~Payload_impl() { call_payload_destroyed(); }

    Payload_impl(Payload_impl const&) = delete;
    Payload_impl(Payload_impl&&) = default;
    Payload_impl& operator=(Payload_impl const&) = delete;
    Payload_impl& operator=(Payload_impl&& other) {
        if (this != &other) {
            call_payload_destroyed();
            m_data = std::move(other.m_data);
            m_writable_data = other.m_writable_data;
            m_slot_handle = other.m_slot_handle;
            m_payload_destroyed = std::move(other.m_payload_destroyed);
        }
        return *this;
    }

    /// \brief Retrieves the payload data.
    /// \return Span of payload data.
    [[nodiscard]] Span data() const noexcept { return m_data; }

    /// \brief Retrieves the writable payload data.
    /// \return Writable span over data(), empty if this instance is read-only.
    [[nodiscard]] Writable_span wdata() noexcept {
        if (m_writable_data == nullptr) {
            return {};
        }
        return Writable_span{m_writable_data, m_data.size()};
    }

    /// \brief Retrieves the slot handle associated with this payload.
    /// \return The slot handle, or kNoSlotHandle if not associated with a slot.
    [[nodiscard]] std::size_t get_slot_handle() const noexcept { return m_slot_handle; }

    /// \brief Reduces the data size.
    /// \param size The new data size. Must not exceed the current data size.
    /// \return True if successful, false if the new size exceeds the current size.
    bool shrink(std::size_t const size) noexcept {
        if (size > m_data.size()) {
            return false;
        }

        m_data = m_data.subspan(0, size);

        return true;
    }

    bool operator==(Payload_impl const& other) const noexcept;

    bool operator!=(Payload_impl const& other) const noexcept { return !(*this == other); }

   private:
    void call_payload_destroyed() noexcept {
        if (!m_payload_destroyed.empty()) {
            m_payload_destroyed();
        }
    }

    Span m_data;
    /// Start of m_data if the buffer is writable, nullptr for a read-only payload.
    Byte* m_writable_data{nullptr};
    std::size_t m_slot_handle{kNoSlotHandle};
    Payload_destroyed m_payload_destroyed;
};
}  // namespace detail

/// \brief Interface representing the Payload transferable by SOCom.
/// \details The payload itself must be representable by a continuous Span of bytes.
///
/// data() is read-only; a sender writes it via Writable_payload::wdata(). In the SOME/IP gateway,
/// data() holds the full SOME/IP message (header + payload).
class Payload {
   public:
    /// \brief Alias for a data byte.
    using Byte = detail::Payload_impl::Byte;

    /// \brief Alias for payload data.
    using Span = detail::Payload_impl::Span;

    /// \brief Alias for writable payload data.
    using Writable_span = detail::Payload_impl::Writable_span;

    /// \brief Called when Payload and memory is released. Can own the memory if it is heap
    /// allocated.
    using Payload_destroyed = detail::Payload_impl::Payload_destroyed;

    Payload() = default;

    /// \brief Construct new instance.
    Payload(Span data, std::size_t slot_handle, Payload_destroyed payload_destroyed) noexcept
        : m_impl(data, slot_handle, std::move(payload_destroyed)) {}

    ~Payload() = default;
    Payload(Payload const&) = delete;
    Payload(Payload&&) = default;
    Payload& operator=(Payload const&) = delete;
    Payload& operator=(Payload&&) = default;

    /// \brief Retrieves the payload data.
    /// \return Span of payload data.
    [[nodiscard]] Span data() const noexcept { return m_impl.data(); }

    /// \brief Retrieves the slot handle associated with this payload.
    /// \return The slot handle, or kNoSlotHandle if not associated with a slot.
    [[nodiscard]] std::size_t get_slot_handle() const noexcept { return m_impl.get_slot_handle(); }

    /// \brief Operator == for Payload.
    /// \param lhs Left-hand side of operator.
    /// \param rhs Right-hand side of operator.
    /// \return True in case of equality, otherwise false.
    [[nodiscard]]
    bool operator==(Payload const& other) const noexcept {
        return m_impl == other.m_impl;
    }

    /// \brief Operator != for Payload.
    /// \param lhs Left-hand side of operator.
    /// \param rhs Right-hand side of operator.
    /// \return True in case of inequality, otherwise false.
    [[nodiscard]]
    bool operator!=(Payload const& other) const noexcept {
        return !(*this == other);
    }

   protected:
    explicit Payload(detail::Payload_impl impl) noexcept : m_impl(std::move(impl)) {}

    detail::Payload_impl m_impl;
};

/// \brief Writable payload, which can be allocated by the recipient for zero copy operations.
/// \details The payload itself must be representable by a continuous Span of bytes.
///
/// The recipient is responsible for allocating enough data for the sender.
/// After the data has been written to, the recipient can move the Writable_payload into a Payload
/// and send it to the sender.
///
/// In the SOME/IP gateway, the sender writes the full SOME/IP message (header + payload) via
/// wdata(). This keeps both in contiguous memory, as needed by algorithms like the one for E2E.
class Writable_payload : public Payload {
   public:
    /// \brief Construct new instance.
    Writable_payload(Writable_span data, std::size_t slot_handle,
                     Payload_destroyed payload_destroyed) noexcept
        : Payload(detail::Payload_impl{data, slot_handle, std::move(payload_destroyed)}) {}

    /// \brief Retrieves the writable payload data.
    /// \return Span of payload data, empty if a read-only Payload was move-assigned into this.
    [[nodiscard]] Writable_span wdata() noexcept { return m_impl.wdata(); }

    /// \brief Reduces the size of the payload data.
    /// \details Allows the payload to report a smaller size than its allocated capacity.
    /// This is useful for zero-copy scenarios where a pre-allocated buffer is partially filled.
    /// \param size The new data size. Must not exceed the current data size.
    /// \return True if successful, false if the new size exceeds the current size.
    bool shrink(std::size_t size) noexcept { return m_impl.shrink(size); }
};

/// \brief An empty payload instance, which may be used as default value for the payload parameter.
/// \return A pointer to a Payload object.
extern Payload empty_payload();

}  // namespace score::socom

#endif  // SCORE_SOCOM_PAYLOAD_HPP
