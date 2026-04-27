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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_BINDING_BASE
#define SRC_GATEWAY_IPC_BINDING_SRC_BINDING_BASE

#include <mutex>
#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <score/socom/client_connector.hpp>
#include <score/socom/runtime.hpp>
#include <set>
#include <unordered_map>

#include "connection_metadata.hpp"
#include "connections.hpp"
#include "key.hpp"
#include "pending_connects.hpp"
#include "reply_channel.hpp"
#include "request_service_handle.hpp"
#include "service_state.hpp"
#include "shared_memory_managers.hpp"

namespace score::gateway_ipc_binding {

/// \brief Basis for Gateway IPC Binding client and server implementations
///
/// Via the server this will manage multiple client connections and via the client there will be
/// only one connection to the server, but in both cases the underlying logic for handling service
/// offers, requests, and events is shared.
class Gateway_ipc_binding_base : public Service_request_sender {
   public:
    /// \brief Constructor
    /// \param runtime SOCom runtime for service bridge registration
    /// \param slot_manager Factory for creating shared memory slot manager
    /// \param server Unique pointer to message_passing server
    explicit Gateway_ipc_binding_base(score::socom::Runtime& runtime,
                                      Shared_memory_manager_factory::Sptr slot_manager);
    ~Gateway_ipc_binding_base() override;

    /// \brief Add an IPC connection to another process
    /// \param client_id Identifier for the client
    /// \param reply_channel Channel for sending replies to the client
    void add_client(Client_id const& client_id, Reply_channel& reply_channel);

    /// \brief Remove an IPC connection to another process
    /// \param client_id Identifier for the client
    void remove_client(Client_id const& client_id);

    /// \brief Handle a received message from a client
    /// \param client_id Identifier for the client
    /// \param conn Channel for sending replies to the client
    /// \param data Message data
    void on_receive_message(Client_id client_id, Reply_channel& conn,
                            score::cpp::span<std::uint8_t const> data);

   private:
    void handle_connect_message(Client_id client_id, Reply_channel& conn, Connect const& msg);

    void handle_connect_reply_message(Connect_reply const& msg);

    void handle_request_service_message(Client_id client_id, Reply_channel& conn,
                                        Request_service const& msg) noexcept;

    void handle_offer_service_message(Client_id client_id, Offer_service const& msg) noexcept;

    void handle_subscribe_event_message(Client_id client_id, Subscribe_event const& msg) noexcept;

    void handle_event_update_message(Client_id client_id, Event_update const& msg) noexcept;

    void handle_payload_consumed_message(Client_id client_id, Payload_consumed const& msg) noexcept;

    void handle_connect_service_message(Client_id client_id, Reply_channel& conn,
                                        Connect_service const& msg) noexcept;

    void handle_connect_service_reply_message(Client_id client_id,
                                              Connect_service_reply const& msg) noexcept;

    bool send_request_service_locked(Service const& service, Instance_id const& instance,
                                     bool in_use) noexcept;

    void send_request_service(score::socom::Service_interface_definition const& configuration,
                              score::socom::Service_instance const& instance,
                              bool in_use) noexcept override;

    void send_offer_service_to_client(Reply_channel& conn, Service const& service,
                                      Instance_id const& instance, bool offered) noexcept;

    void maybe_send_connect_service_locked(Key_t const& key, Service_state& state) noexcept;

    void remove_client_state_locked(Client_id client_id) noexcept;

    void clear_pending_connects_for_key_locked(Key_t const& key,
                                               Client_id const& client_id) noexcept;

    Keys m_keys;
    Connections m_connections;
    score::socom::Runtime& m_runtime;
    Shared_memory_managers m_slot_managers;
    Read_only_memory_managers m_read_only_slot_managers;
    score::socom::Service_bridge_registration m_bridge_registration;
    std::recursive_mutex m_mutex;
    Service_states m_service_states;
    Connection_metadata m_id_mapping;
    std::unordered_map<Key_t, bool> m_local_offers;
    Pending_connects m_pending_connects;
    std::unordered_map<Key_t, std::set<Client_id>> m_service_to_interested_peers;
    Id_generator<Remote_handle> m_next_local_id{1};
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_BINDING_BASE
