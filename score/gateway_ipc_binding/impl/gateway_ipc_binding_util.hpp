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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_GATEWAY_IPC_BINDING_UTIL
#define SRC_GATEWAY_IPC_BINDING_SRC_GATEWAY_IPC_BINDING_UTIL

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include "score/gateway_ipc_binding/gateway_ipc_binding.hpp"

namespace score::gateway_ipc_binding {

/// \brief Message frame header for all IPC messages
struct Message_frame_header {
    Message_type type;
    std::uint16_t payload_size;
};

// \brief IPC message with type and payload
template <typename T>
struct Message_frame {
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    Message_frame_header header{T::type, sizeof(T)};
    T payload;
};

inline Message_type get_message_type(std::uint8_t data) noexcept {
    static_assert(sizeof(Message_type) == sizeof(std::uint8_t), "Message_type must be 1 byte");
    return static_cast<Message_type>(data);
}

/// \brief Check if the incoming message matches the expected type and can be safely converted to
/// Msg_type
/// \tparam Msg_type Expected message type to check and cast to
/// \param data Incoming message data (including framing)
/// \return Copy of the payload if type and size are valid, std::nullopt otherwise
/// \details Extracts via memcpy rather than reinterpret_cast: message_passing transports are not
/// guaranteed to deliver the payload at an address aligned for Msg_type (observed on QNX, where
/// the framing byte shifts the payload off of Message_frame<Msg_type>'s alignment), and punning
/// through a misaligned pointer is undefined behavior.
template <typename Msg_type>
std::optional<Msg_type> check_and_convert(score::cpp::span<std::uint8_t const> data) noexcept {
    static_assert(std::is_trivially_copyable_v<Message_frame<Msg_type>>,
                  "Message_frame<Msg_type> must be trivially copyable");
    static_assert(
        sizeof(Message_frame<Msg_type>) <= kMax_safe_message_size,
        "Message_frame<Msg_type> exceeds kMax_safe_message_size and may not be sendable on all "
        "message_passing backends (e.g. QNX)");

    if (data.empty()) {
        return std::nullopt;  // Invalid frame - too small
    }

    auto const type = get_message_type(data[0]);

    if (type != Msg_type::type) {
        return std::nullopt;  // We only handle the specified message type at this scope
    }

    if (data.size() < sizeof(Message_frame<Msg_type>)) {
        return std::nullopt;  // Invalid frame - insufficient data
    }

    Message_frame_header header{};
    std::memcpy(&header, data.data(), sizeof(header));

    if (header.payload_size != sizeof(Msg_type)) {
        return std::nullopt;  // Invalid payload size
    }

    Msg_type payload{};
    std::memcpy(&payload, data.data() + offsetof(Message_frame<Msg_type>, payload),
                sizeof(payload));
    return payload;
}

struct Service_counts {
    std::uint16_t num_methods{0U};
    std::uint16_t num_events{0U};
};

template <typename T>
class Id_generator {
    T m_next_id;

   public:
    Id_generator(T start = T{}) : m_next_id(std::move(start)) {}

    T get_next_id() noexcept { return m_next_id++; }
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_GATEWAY_IPC_BINDING_UTIL
