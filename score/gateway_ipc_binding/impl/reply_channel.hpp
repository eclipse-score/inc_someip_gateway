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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_REPLY_CHANNEL
#define SRC_GATEWAY_IPC_BINDING_SRC_REPLY_CHANNEL

#include <cstdint>
#include <score/span.hpp>

#include "score/gateway_ipc_binding/gateway_ipc_binding.hpp"
#include "score/result/result.h"

namespace score::gateway_ipc_binding {

class Reply_channel {
   public:
    /// \brief Virtual destructor
    virtual ~Reply_channel() = default;

    /// \brief Tear down this connection after an unrecoverable protocol error (for example a
    /// handshake message that could not be sent), instead of leaving the peer waiting forever.
    /// \details Default is a no-op; overridden where the concrete channel can actually act on it.
    virtual void request_disconnect() noexcept = 0;

    /// \brief Send a message through this connection
    /// \param msg Message to send
    /// \return Success or error
    template <typename Msg_frame>
    Result<void> send(Msg_frame const& msg) noexcept {
        static_assert(std::is_trivially_copyable_v<Msg_frame>,
                      "Msg_frame must be trivially copyable for send");
        static_assert(sizeof(Msg_frame) <= kMax_safe_message_size,
                      "Msg_frame exceeds kMax_safe_message_size and may not be sendable on all "
                      "message_passing backends (e.g. QNX)");

        return send(score::cpp::span<std::uint8_t const>(
            reinterpret_cast<std::uint8_t const*>(&msg), sizeof(Msg_frame)));
    }

   protected:
    Reply_channel() = default;
    Reply_channel(Reply_channel const&) = delete;
    Reply_channel& operator=(Reply_channel const&) = delete;
    Reply_channel(Reply_channel&&) = delete;
    Reply_channel& operator=(Reply_channel&&) = delete;

    /// \brief Send a message through this connection
    /// \param data Message payload to send
    /// \return Success or error
    virtual Result<void> send(score::cpp::span<std::uint8_t const> data) noexcept = 0;
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_REPLY_CHANNEL
