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
#include <new>
#include <score/span.hpp>
#include <type_traits>
#include <utility>

namespace score::socom {

namespace detail {
class Payload_user_data final {
   public:
    Payload_user_data() = default;

    template <typename T>
    explicit Payload_user_data(T&& user_data) noexcept {
        using User_data_type = std::remove_cv_t<std::remove_reference_t<T>>;
        static_assert(sizeof(User_data_type) <= kStorage_size,
                      "Payload user data must fit in the inline storage");
        static_assert(alignof(User_data_type) <= alignof(std::max_align_t),
                      "Payload user data alignment exceeds the inline storage alignment");
        static_assert(std::is_nothrow_move_constructible<User_data_type>::value,
                      "Payload user data must be nothrow move constructible");
        static_assert(std::is_nothrow_destructible<User_data_type>::value,
                      "Payload user data must be nothrow destructible");

        new (m_storage) User_data_type(std::forward<T>(user_data));
        m_ops = operations<User_data_type>();
    }

    ~Payload_user_data() { reset(); }

    Payload_user_data(Payload_user_data const&) = delete;
    Payload_user_data& operator=(Payload_user_data const&) = delete;

    Payload_user_data(Payload_user_data&& other) noexcept { move_from(other); }

    Payload_user_data& operator=(Payload_user_data&& other) noexcept {
        if (this != &other) {
            reset();
            move_from(other);
        }
        return *this;
    }

    template <typename T>
    [[nodiscard]] T* get() noexcept {
        using User_data_type = std::remove_cv_t<T>;
        if (m_ops != operations<User_data_type>()) {
            return nullptr;
        }
        return std::launder(reinterpret_cast<User_data_type*>(m_storage));
    }

    template <typename T>
    [[nodiscard]] T const* get() const noexcept {
        using User_data_type = std::remove_cv_t<T>;
        if (m_ops != operations<User_data_type>()) {
            return nullptr;
        }
        return std::launder(reinterpret_cast<User_data_type const*>(m_storage));
    }

   private:
    struct Operations {
        void (*destroy)(void*) noexcept;
        void (*move)(void*, void*) noexcept;
    };

    static constexpr std::size_t kStorage_size{128U};

    template <typename T>
    [[nodiscard]] static Operations const* operations() noexcept {
        static constexpr Operations ops{
            [](void* storage) noexcept { std::launder(static_cast<T*>(storage))->~T(); },
            [](void* destination, void* source) noexcept {
                new (destination) T(std::move(*std::launder(static_cast<T*>(source))));
                std::launder(static_cast<T*>(source))->~T();
            }};
        return &ops;
    }

    void reset() noexcept {
        if (m_ops != nullptr) {
            m_ops->destroy(m_storage);
            m_ops = nullptr;
        }
    }

    void move_from(Payload_user_data& other) noexcept {
        if (other.m_ops != nullptr) {
            other.m_ops->move(m_storage, other.m_storage);
            m_ops = other.m_ops;
            other.m_ops = nullptr;
        }
    }

    alignas(std::max_align_t) std::byte m_storage[kStorage_size]{};
    Operations const* m_ops{nullptr};
};

class Payload_impl final {
   public:
    /// \brief Alias for a data byte.
    using Byte = std::byte;

    /// \brief Alias for payload data.
    using Span = score::cpp::span<Byte const>;

    /// \brief Alias for writable payload data.
    using Writable_span = score::cpp::span<Byte>;

    Payload_impl() = default;

    /// \brief Construct new read-only instance.
    explicit Payload_impl(Span data) noexcept : m_data(data) {}

    /// \brief Construct new writable instance.
    explicit Payload_impl(Writable_span data) noexcept : m_data(data), m_writable_data(data.data()) {}

    template <typename T>
    Payload_impl(Span data, T&& user_data) noexcept
        : m_data(data), m_user_data(std::forward<T>(user_data)) {}

    template <typename T>
    Payload_impl(Writable_span data, T&& user_data) noexcept
        : m_data(data),
          m_writable_data(data.data()),
          m_user_data(std::forward<T>(user_data)) {}

    ~Payload_impl() = default;

    Payload_impl(Payload_impl const&) = delete;
    Payload_impl(Payload_impl&&) = default;
    Payload_impl& operator=(Payload_impl const&) = delete;
    Payload_impl& operator=(Payload_impl&&) = default;

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

    template <typename T>
    [[nodiscard]] T* get_user_data() noexcept {
        return m_user_data.get<T>();
    }

    template <typename T>
    [[nodiscard]] T const* get_user_data() const noexcept {
        return m_user_data.get<T>();
    }

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

   private:
    Span m_data;
    /// Start of m_data if the buffer is writable, nullptr for a read-only payload.
    Byte* m_writable_data{nullptr};
    Payload_user_data m_user_data;
};
}  // namespace detail

/// \brief Interface representing the Payload transferable by SOCom.
/// \details The payload itself must be representable by a continuous Span of bytes.
///
/// data() is read-only; a sender writes it via Writable_payload::wdata(). In the SOME/IP gateway,
/// data() holds the full SOME/IP message (header + payload). Optional user data is owned inline by
/// the payload and follows it through moves. It must fit in the 128-byte inline storage, be
/// max_align_t-aligned or less, and be nothrow move constructible and destructible.
class Payload {
   public:
    /// \brief Alias for a data byte.
    using Byte = detail::Payload_impl::Byte;

    /// \brief Alias for payload data.
    using Span = detail::Payload_impl::Span;

    /// \brief Alias for writable payload data.
    using Writable_span = detail::Payload_impl::Writable_span;

    Payload() = default;

    /// \brief Construct new instance.
    explicit Payload(Span data) noexcept : m_impl(data) {}

    template <typename T>
    Payload(Span data, T&& user_data) noexcept : m_impl(data, std::forward<T>(user_data)) {}

    ~Payload() = default;
    Payload(Payload const&) = delete;
    Payload(Payload&&) = default;
    Payload& operator=(Payload const&) = delete;
    Payload& operator=(Payload&&) = default;

    /// \brief Retrieves the payload data.
    /// \return Span of payload data.
    [[nodiscard]] Span data() const noexcept { return m_impl.data(); }

    /// \brief Retrieves user data stored with this payload.
    /// \return A pointer to the stored value, or nullptr if it has a different type or is absent.
    template <typename T>
    [[nodiscard]] T* get_user_data() noexcept {
        return m_impl.get_user_data<T>();
    }

    /// \brief Retrieves user data stored with this payload.
    /// \return A const pointer to the stored value, or nullptr if it has a different type or is
    ///         absent.
    template <typename T>
    [[nodiscard]] T const* get_user_data() const noexcept {
        return m_impl.get_user_data<T>();
    }

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
    explicit Writable_payload(Writable_span data) noexcept : Payload(detail::Payload_impl{data}) {}

    template <typename T>
    Writable_payload(Writable_span data, T&& user_data) noexcept
        : Payload(detail::Payload_impl{data, std::forward<T>(user_data)}) {}

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
