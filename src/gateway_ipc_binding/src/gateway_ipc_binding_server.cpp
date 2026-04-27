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

#include <score/message_passing/i_server.h>
#include <score/message_passing/i_server_connection.h>

#include <cassert>
#include <iostream>
#include <memory>
#include <mutex>
#include <score/gateway_ipc_binding/error.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <score/socom/runtime.hpp>
#include <utility>

#include "binding_base.hpp"
#include "reply_channel.hpp"

namespace score::gateway_ipc_binding {

namespace {

class Server_reply_channel : public Reply_channel {
    score::message_passing::IServerConnection* m_conn;

   public:
    explicit Server_reply_channel(score::message_passing::IServerConnection& conn)
        : m_conn{&conn} {}

    Result<void> send(score::cpp::span<std::uint8_t const> data) noexcept override {
        auto const send_result = m_conn->Notify(data);
        if (!send_result) {
            std::cerr << __PRETTY_FUNCTION__
                      << ": Failed to send message to client: " << send_result.error() << std::endl;
            return MakeUnexpected(Bidirectional_channel_error::runtime_error_send_failed);
        }
        return {};
    }
};

/// \brief Implementation of Gateway_ipc_binding_server
class Gateway_ipc_binding_server_impl : public Gateway_ipc_binding_server {
   public:
    /// \brief Constructor
    /// \param runtime SOCom runtime for service bridge registration
    /// \param slot_manager Factory for creating shared memory slot manager
    /// \param server Unique pointer to message_passing server
    explicit Gateway_ipc_binding_server_impl(
        score::socom::Runtime& runtime, Shared_memory_manager_factory::Sptr slot_manager,
        Gateway_ipc_binding_server::On_find_service_change on_find_service_change,
        score::cpp::pmr::unique_ptr<score::message_passing::IServer> server)
        : m_server(std::move(server)),
          m_on_find_service_change(std::move(on_find_service_change)),
          m_binding_base{runtime, std::move(slot_manager)} {}

    ~Gateway_ipc_binding_server_impl() {
        // Stop the server before destroying member variables to ensure background threads
        // are no longer accessing shared state
        bool listening = false;
        {
            std::lock_guard<std::mutex> const lock(m_mutex);
            listening = m_listening;
        }
        if (m_server && listening) {
            m_server->StopListening();
        }
    }

    /// \brief Start listening for incoming connections
    Result<void> start() noexcept override {
        {
            std::lock_guard<std::mutex> const lock(m_mutex);
            if (m_listening) {
                return MakeUnexpected(Bidirectional_channel_error::runtime_error_listen_failed,
                                      "Server already listening");
            }
        }

        auto internal_connect_callback =
            [this](score::message_passing::IServerConnection& connection)
            -> score::cpp::expected<score::message_passing::UserData, score::os::Error> {
            std::lock_guard<std::mutex> const lock(m_mutex);
            Client_id const client_id = m_next_client_id.get_next_id();
            auto const inserted = m_connections.emplace(client_id, connection);
            m_binding_base.add_client(client_id, inserted.first->second);
            return static_cast<std::uintptr_t>(client_id);
        };

        auto internal_disconnect_callback =
            [this](score::message_passing::IServerConnection& connection) {
                assert(std::holds_alternative<std::uintptr_t>(connection.GetUserData()));
                auto const user_data = std::get<std::uintptr_t>(connection.GetUserData());
                Client_id const client_id = static_cast<Client_id>(user_data);
                m_binding_base.remove_client(client_id);
                std::lock_guard<std::mutex> const lock(m_mutex);
                m_connections.erase(client_id);
                m_client_identifiers.erase(client_id);

                auto const find_it = m_find_service_elements_by_client.find(client_id);
                if (find_it != m_find_service_elements_by_client.end()) {
                    m_on_find_service_change(client_id, find_it->second, false);
                    m_find_service_elements_by_client.erase(find_it);
                }
            };

        auto internal_message_callback = [this](
                                             score::message_passing::IServerConnection& connection,
                                             score::cpp::span<std::uint8_t const> payload)
            -> score::cpp::expected_blank<score::os::Error> {
            assert(std::holds_alternative<std::uintptr_t>(connection.GetUserData()));
            auto const user_data = std::get<std::uintptr_t>(connection.GetUserData());
            Client_id const client_id = static_cast<Client_id>(user_data);

            auto message_type = get_message_type(payload[0]);

            if (Message_type::Connect == message_type) {
                auto msg_opt = check_and_cast<Connect>(payload);
                if (!msg_opt) {
                    // Invalid message - log and ignore
                    return {};
                }

                auto const& msg = **msg_opt;
                std::lock_guard<std::mutex> const lock(m_mutex);

                m_client_identifiers[client_id] = Client_info{
                    fixed_string_to_string(msg.identifier), connection.GetClientIdentity()};

                if (!msg.find_service_elements.empty()) {
                    m_on_find_service_change(client_id, msg.find_service_elements, true);
                    m_find_service_elements_by_client[client_id] = msg.find_service_elements;
                }
            }

            Server_reply_channel reply_channel{connection};
            m_binding_base.on_receive_message(client_id, reply_channel, payload);
            return {};
        };

        auto result = m_server->StartListening(std::move(internal_connect_callback),
                                               std::move(internal_disconnect_callback),
                                               std::move(internal_message_callback));
        if (!result) {
            std::cerr << __PRETTY_FUNCTION__
                      << ": Failed to start listening for incoming connections: " << result.error()
                      << std::endl;
            return MakeUnexpected(Bidirectional_channel_error::runtime_error_listen_failed);
        }

        {
            std::lock_guard<std::mutex> const lock(m_mutex);
            m_listening = true;
        }
        return {};
    }

    std::unordered_map<Client_id, Client_info> get_client_identifiers() const noexcept override {
        std::lock_guard<std::mutex> const lock(m_mutex);
        return m_client_identifiers;
    }

   private:
    Id_generator<Client_id> m_next_client_id{0};
    bool m_listening{false};
    score::cpp::pmr::unique_ptr<score::message_passing::IServer> m_server;
    std::unordered_map<Client_id, Server_reply_channel> m_connections;
    mutable std::mutex m_mutex;  // Protects m_listening, m_next_client_id, m_connections
    std::unordered_map<Client_id, Find_service_elements> m_find_service_elements_by_client;
    std::unordered_map<Client_id, Client_info> m_client_identifiers;
    Gateway_ipc_binding_server::On_find_service_change m_on_find_service_change;
    Gateway_ipc_binding_base m_binding_base;
};

}  // namespace

std::unique_ptr<Gateway_ipc_binding_server> Gateway_ipc_binding_server::create(
    score::socom::Runtime& runtime,
    score::cpp::pmr::unique_ptr<score::message_passing::IServer> server,
    Shared_memory_manager_factory::Uptr slot_manager,
    On_find_service_change on_find_service_change) noexcept {
    assert(server && "Server must not be null");

    return std::make_unique<Gateway_ipc_binding_server_impl>(
        runtime, std::move(slot_manager), std::move(on_find_service_change), std::move(server));
}

}  // namespace score::gateway_ipc_binding
