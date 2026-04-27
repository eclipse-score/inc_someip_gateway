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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_CONNECTIONS
#define SRC_GATEWAY_IPC_BINDING_SRC_CONNECTIONS

#include <cassert>
#include <score/gateway_ipc_binding/error.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <unordered_map>
#include <utility>

#include "reply_channel.hpp"

namespace score::gateway_ipc_binding {

class Connections {
    std::unordered_map<Client_id, std::reference_wrapper<Reply_channel>> m_connections;

   public:
    template <typename Msg_type>
    Result<void> send(Client_id const& client_id, Msg_type const& msg) const noexcept {
        static_assert(std::is_trivially_copyable_v<Msg_type>,
                      "Msg_type must be trivially copyable for send");

        auto connection_it = m_connections.find(client_id);
        if (connection_it == m_connections.end()) {
            return MakeUnexpected(Bidirectional_channel_error::runtime_error_send_failed,
                                  "Client ID not found in connections map");
        }
        return connection_it->second.get().send(msg);
    }

    template <typename Msg_type>
    Result<void> send_to_all(Msg_type const& msg) const noexcept {
        static_assert(std::is_trivially_copyable_v<Msg_type>,
                      "Msg_type must be trivially copyable for send_to_all");

        Result<void> last_failure = {};
        for (auto& entry : m_connections) {
            auto send_result = entry.second.get().send(msg);
            if (!send_result) {
                last_failure = send_result;
            }
        }
        return last_failure;
    }

    void add_client(Client_id const& client_id, Reply_channel& reply_channel) {
        auto const insert_result = m_connections.emplace(client_id, reply_channel);
        assert(insert_result.second && "Client ID already exists in connections map");
    }

    void remove_client(Client_id const& client_id) { m_connections.erase(client_id); }

    Reply_channel* get_reply_channel(Client_id const& client_id) noexcept {
        auto it = m_connections.find(client_id);
        if (it != m_connections.end()) {
            return &it->second.get();
        }
        return nullptr;
    }
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_CONNECTIONS
