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

#include "binding_base.hpp"

#include <cassert>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <score/gateway_ipc_binding/error.hpp>
#include <utility>

#include "gateway_ipc_binding_util.hpp"
#include "shared_memory_payload.hpp"

template <typename... Args>
void log_it_impl(Args... args) {
    static std::mutex log_mutex;
    std::lock_guard<std::mutex> const lock{log_mutex};

    using Expander = int[];
    // Comma operator used in a special case to mimic C++17 fold expressions.
    (void)Expander{0, ((std::cout << std::forward<Args>(args)), 0)...};

    std::cout << std::endl;
}

// #define log_it(...) log_it_impl(__PRETTY_FUNCTION__, ", this == ", this, ", ", __VA_ARGS__)
#define log_it(...) void(nullptr)

namespace score::gateway_ipc_binding {

Gateway_ipc_binding_base::Gateway_ipc_binding_base(score::socom::Runtime& runtime,
                                                   Shared_memory_manager_factory::Sptr slot_manager)
    : m_runtime(runtime),
      m_slot_managers(slot_manager, m_keys),
      m_read_only_slot_managers(std::move(slot_manager)) {
    // Create callbacks for service bridge registration
    auto subscribe_find_service_callback =
        [](score::socom::Find_result_change_callback callback,
           score::socom::Service_interface_identifier const& interface,
           std::optional<score::socom::Service_instance> instance)
        -> score::socom::Find_subscription {
        // TODO: Implement find service subscription logic
        assert(false && "subscribe_find_service_callback not implemented");
        return nullptr;
    };

    auto request_service_callback =
        [this](score::socom::Service_interface_definition const& configuration,
               score::socom::Service_instance const& instance) -> score::socom::Service_request {
        return std::make_unique<Request_service_handle>(*this, configuration, instance);
    };

    // Register this server as a service bridge with the runtime
    auto bridge_identity = score::socom::Bridge_identity::make(this);
    auto bridge_registration_result = m_runtime.register_service_bridge(
        bridge_identity, std::move(subscribe_find_service_callback),
        std::move(request_service_callback));
    assert(bridge_registration_result && "Failed to register service bridge with runtime");
    m_bridge_registration = std::move(bridge_registration_result).value();
}

Gateway_ipc_binding_base::~Gateway_ipc_binding_base() {
    score::socom::Service_bridge_registration bridge_registration;
    std::vector<score::socom::Client_connector::Uptr> client_connectors;
    auto const lock = m_mutex.lock();
    bridge_registration = std::move(m_bridge_registration);
    client_connectors = m_service_states.release_client_connectors();
}

void Gateway_ipc_binding_base::add_client(Client_id const& client_id,
                                          Reply_channel& reply_channel) {
    auto const lock = m_mutex.lock();
    m_connections.add_client(client_id, reply_channel);
}

void Gateway_ipc_binding_base::remove_client(Client_id const& client_id) {
    auto const lock = m_mutex.lock();
    m_connections.remove_client(client_id);
    remove_client_state_locked(client_id);
}

void Gateway_ipc_binding_base::on_receive_message(Client_id client_id, Reply_channel& conn,
                                                  score::cpp::span<std::uint8_t const> data) {
    if (data.empty()) {
        return;  // Ignore empty messages
    }

    auto message_type = get_message_type(data[0]);
    log_it("message_type == ", static_cast<int>(message_type));
    switch (message_type) {
        case Message_type::Connect: {
            auto msg_opt = check_and_cast<Connect>(data);
            if (!msg_opt) {
                // Invalid message - log and ignore
                return;
            }

            handle_connect_message(client_id, conn, **msg_opt);
            break;
        }
        case Message_type::Connect_reply: {
            auto msg_opt = check_and_cast<Connect_reply>(data);
            if (!msg_opt) {
                // Invalid message - log and ignore
                return;
            }

            handle_connect_reply_message(**msg_opt);
            break;
        }
        case Message_type::Connect_service: {
            auto msg_opt = check_and_cast<Connect_service>(data);
            if (!msg_opt) {
                return;
            }

            handle_connect_service_message(client_id, conn, **msg_opt);
            break;
        }
        case Message_type::Connect_service_reply: {
            auto msg_opt = check_and_cast<Connect_service_reply>(data);
            if (!msg_opt) {
                return;
            }

            handle_connect_service_reply_message(client_id, **msg_opt);
            break;
        }
        case Message_type::Request_service: {
            auto msg_opt = check_and_cast<Request_service>(data);
            if (!msg_opt) {
                return;
            }

            handle_request_service_message(client_id, conn, **msg_opt);
            break;
        }
        case Message_type::Offer_service: {
            auto msg_opt = check_and_cast<Offer_service>(data);
            if (!msg_opt) {
                return;
            }

            handle_offer_service_message(client_id, **msg_opt);
            break;
        }
        case Message_type::Subscribe_event: {
            auto msg_opt = check_and_cast<Subscribe_event>(data);
            if (!msg_opt) {
                return;
            }

            handle_subscribe_event_message(client_id, **msg_opt);
            break;
        }
        case Message_type::Event_update: {
            auto msg_opt = check_and_cast<Event_update>(data);
            if (!msg_opt) {
                return;
            }

            handle_event_update_message(client_id, **msg_opt);
            break;
        }
        case Message_type::Payload_consumed: {
            auto msg_opt = check_and_cast<Payload_consumed>(data);
            if (!msg_opt) {
                return;
            }

            handle_payload_consumed_message(client_id, **msg_opt);
            break;
        }
        default:
            // Unhandled message type - log and ignore
            assert(false);
            break;
    }
}

void Gateway_ipc_binding_base::handle_connect_message(Client_id client_id, Reply_channel& conn,
                                                      Connect const& msg) {
    // TODO handle msg.payload and perform any necessary setup for the new connection

    // serialize reply and send back to client
    Message_frame<Connect_reply> reply;
    reply.payload.status = true;

    (void)conn.send(reply);
}

void Gateway_ipc_binding_base::handle_connect_reply_message(Connect_reply const& msg) {
    if (msg.status) {
        auto const lock = m_mutex.lock();
        m_service_states.for_each([this](auto const& key, auto& state) {
            maybe_send_connect_service_locked(key, state);
        });
    }
}

void Gateway_ipc_binding_base::handle_request_service_message(Client_id client_id,
                                                              Reply_channel& conn,
                                                              Request_service const& msg) noexcept {
    log_it("");
    auto const lock = m_mutex.lock();
    auto const& key = m_keys.get(msg.service_id, msg.instance_id);
    if (!msg.in_use) {
        m_service_states.remove_event_subscriptions_for_client(client_id);
        m_id_mapping.remove_mapping_for_client_and_key(client_id, key);
        clear_pending_connects_for_key_locked(key, client_id);
        m_service_to_interested_peers[key].erase(client_id);
        return;
    }

    m_service_to_interested_peers[key].insert(client_id);
    auto const local_offer = m_local_offers.find(key);
    if (local_offer != m_local_offers.end() && local_offer->second) {
        send_offer_service_to_client(conn, msg.service_id, msg.instance_id, true);
        return;
    }

    if (m_service_states.has_client_connector(key)) {
        // if there were already connectors for this service, then the offer must have been sent to
        // the client
        return;
    }

    log_it("Creating client connector for service request");

    // create Client_connector and send offer once Enabled_server_connector is available
    auto const send_event_update = [this, key = key](score::socom::Client_connector const&,
                                                     score::socom::Event_id event_id,
                                                     score::socom::Payload::Sptr const& payload) {
        auto const lock = m_mutex.lock();
        std::size_t recipient_count{0U};

        m_id_mapping.for_each_client(
            key, [this, event_id, payload, &recipient_count](Client_id client_id,
                                                             Connection_metadata::Ids const& ids) {
                Reply_channel* const conn = m_connections.get_reply_channel(client_id);
                assert(conn != nullptr && "Connection not found for client_id");

                if (conn == nullptr) {
                    return;
                }

                Message_frame<Event_update> update_msg;
                update_msg.payload.required_id = ids.remote_handle;
                update_msg.payload.event_id = event_id;
                update_msg.payload.payload = {
                    .slot_index = static_cast<std::uint32_t>(payload->get_slot_handle()),
                    .used_bytes = static_cast<std::uint32_t>(payload->data().size())};
                auto send_result = conn->send(update_msg);
                if (send_result) {
                    ++recipient_count;
                }
            });

        m_slot_managers.insert_allocation(key, payload, recipient_count);
    };

    auto const service_state_change =
        [this, key](score::socom::Client_connector const& connector,
                    score::socom::Service_state state,
                    score::socom::Server_service_interface_definition const& configuration) {
            log_it("");
            auto const lock = m_mutex.lock();

            auto const is_available = state == score::socom::Service_state::available;

            if (!is_available) {
                m_id_mapping.remove_service(key);
            }

            m_local_offers[key] = is_available;
            auto const interface_instance_opt = m_keys.get(key);
            assert(interface_instance_opt.has_value() &&
                   "Interface and instance should exist for key");
            auto const& [interface, instance] = interface_instance_opt.value();

            m_service_states.add_service(key, interface.get(), instance.get(), configuration);

            auto const& client_ids = m_service_to_interested_peers[key];
            log_it("client_ids size: ", client_ids.size());
            for (const auto& client_id : client_ids) {
                Reply_channel* const conn = m_connections.get_reply_channel(client_id);
                assert(conn != nullptr && "Connection not found for client_id");

                send_offer_service_to_client(*conn, interface.get(), instance.get(), is_available);
            }
        };

    auto const event_payload_allocate =
        [this, key](score::socom::Client_connector const&,
                    score::socom::Event_id) -> score::Result<score::socom::Writable_payload::Uptr> {
        auto const lock = m_mutex.lock();

        auto allocation = m_slot_managers.get_shared_memory_slot_manager(key).allocate_slot();

        return allocation.and_then([](auto& guard) {
            return Result<score::socom::Writable_payload::Uptr>(
                std::make_unique<Shared_memory_payload>(std::move(guard)));
        });
    };

    score::socom::Client_connector::Callbacks client_callbacks{
        service_state_change, send_event_update, send_event_update, event_payload_allocate};

    m_service_states.mark_client_connector_pending(key, msg.service_id, msg.instance_id);
    auto client_connector_result = m_runtime.make_client_connector(
        score::socom::Service_interface_definition{msg.service_id.to_socom_identifier()},
        socom::Service_instance{fixed_string_to_string(msg.instance_id)},
        std::move(client_callbacks));

    if (client_connector_result) {
        m_service_states.add_client_connector(key, std::move(client_connector_result).value());
    } else {
        log_it("Failed to create client connector for requested service");
        m_service_states.clear_client_connector_pending(key);
    }
}

void Gateway_ipc_binding_base::handle_offer_service_message(Client_id client_id,
                                                            Offer_service const& msg) noexcept {
    log_it("");
    // avoids lock-order-inversion, mutexes are locked in different order in this class and
    // socom, whether a socom callback was called or we received an IPC message and act on it
    // using socom
    score::socom::Enabled_server_connector::Uptr removed_connector;

    auto const lock = m_mutex.lock();
    auto const& key = m_keys.get(msg.service_id, msg.instance_id);
    auto state = m_service_states.process_offer(key, client_id, msg);
    removed_connector = std::move(state.connector);

    if (!msg.offered) {
        m_service_states.clear_event_subscriptions(key);
        m_id_mapping.remove_service(key);
        clear_pending_connects_for_key_locked(key, client_id);
        return;
    }

    maybe_send_connect_service_locked(key, state.service_state);
}

void Gateway_ipc_binding_base::handle_subscribe_event_message(Client_id client_id,
                                                              Subscribe_event const& msg) noexcept {
    auto const lock = m_mutex.lock();
    // Find the connection by provided_id for this client
    auto const mapping_info = m_id_mapping.get_by_local_handle(client_id, msg.provided_id);

    if (!mapping_info.has_value()) {
        return;
    }

    auto const& key = mapping_info->get().key;
    Event_subscription_endpoint const endpoint{client_id, msg.provided_id};

    m_service_states.update_event_subscription(key, endpoint, msg.event_id, msg.subscribe);
}

void Gateway_ipc_binding_base::handle_event_update_message(Client_id client_id,
                                                           Event_update const& msg) noexcept {
    auto const lock = m_mutex.lock();
    auto const mapping_info = m_id_mapping.get_by_remote_handle(client_id, msg.required_id);
    if (!mapping_info.has_value()) {
        // Mapping was removed, but peer send event update, before it processed the unsubscription
        // or service removal
        return;
    }

    assert(m_service_states.has_connector(mapping_info->get().key) &&
           "Service state should have connector for key");
    score::socom::Enabled_server_connector* enabled_connector =
        m_service_states.get(mapping_info->get().key)->get().enabled_connector.get();
    assert(enabled_connector != nullptr && "Enabled connector should exist for key");
    if (enabled_connector == nullptr) {
        return;
    }

    Read_only_shared_memory_slot_manager::On_payload_destruction_callback on_payload_destruction =
        [this, client_id, required_id = msg.required_id, payload_handle = msg.payload]() {
            auto const lock = m_mutex.lock();
            Reply_channel* const conn = m_connections.get_reply_channel(client_id);

            if (conn == nullptr) {
                return;
            }

            Message_frame<Payload_consumed> payload_consumed_msg;
            payload_consumed_msg.payload.required_id = required_id;
            payload_consumed_msg.payload.handle = payload_handle;
            (void)conn->send(payload_consumed_msg);
        };

    auto payload =
        m_read_only_slot_managers
            .get_read_only_shared_memory_slot_manager(mapping_info->get().remote_metadata)
            .get_payload(msg.payload, on_payload_destruction);

    auto update_result = enabled_connector->update_event(msg.event_id, std::move(payload));
    (void)update_result;
    assert(update_result);
}

void Gateway_ipc_binding_base::handle_payload_consumed_message(
    Client_id client_id, Payload_consumed const& msg) noexcept {
    auto const lock = m_mutex.lock();
    auto const mapping_info = m_id_mapping.get_by_remote_handle(client_id, msg.required_id);
    if (!mapping_info.has_value()) {
        return;
    }

    m_slot_managers.payload_consumed(mapping_info->get().key, msg);
}

void Gateway_ipc_binding_base::handle_connect_service_message(Client_id client_id,
                                                              Reply_channel& conn,
                                                              Connect_service const& msg) noexcept {
    log_it("");
    auto lock = m_mutex.lock();
    auto const& key = m_keys.get(msg.service_id, msg.instance_id);
    if (!msg.in_use) {
        score::socom::Client_connector::Uptr removed_connector;
        auto const mapping_info = m_id_mapping.get_by_remote_handle(client_id, msg.required_id);
        if (mapping_info.has_value()) {
            m_service_states.remove_event_subscriptions_for_connection(
                key, client_id, mapping_info->get().local_handle);
        }

        m_id_mapping.remove_mapping(client_id, msg.required_id);

        clear_pending_connects_for_key_locked(key, client_id);

        removed_connector = m_service_states.remove_client_connector(key);
        lock.unlock();
        return;
    }

    Connection_metadata::Ids info{};

    info.key = key;
    info.local_handle = m_next_local_id.get_next_id();
    info.remote_handle = msg.required_id;
    info.local_metadata = m_slot_managers.get_shared_memory_metadata(info.key);
    info.remote_metadata = msg.metadata;
    m_id_mapping.add_mapping(client_id, info);

    auto service_state_opt = m_service_states.get(key);
    assert(service_state_opt.has_value() && "Service state should exist for key");
    auto& service_state = service_state_opt->get();

    Message_frame<Connect_service_reply> reply;
    reply.payload.required_id = msg.required_id;
    reply.payload.provided_id = info.local_handle;
    reply.payload.metadata = info.local_metadata;
    reply.payload.num_methods = service_state.counts.num_methods;
    reply.payload.num_events = service_state.counts.num_events;
    (void)conn.send(reply);
}

void Gateway_ipc_binding_base::handle_connect_service_reply_message(
    Client_id client_id, Connect_service_reply const& msg) noexcept {
    log_it("");
    Service_state const* state = nullptr;
    Connection_metadata::Ids info{};

    {
        auto const lock = m_mutex.lock();
        auto const pending = m_pending_connects.find(msg.required_id);
        if (pending == m_pending_connects.end()) {
            return;
        }

        info.key = pending->second.key;

        if (pending->second.client_id != client_id) {
            return;
        }

        // Get service and instance information from m_service_states
        auto service_state_opt = m_service_states.get(info.key);
        if (!service_state_opt) {
            m_pending_connects.erase(pending);
            return;
        }

        state = &service_state_opt->get();

        info.local_handle = msg.provided_id;
        info.remote_handle = msg.required_id;
        info.local_metadata = m_slot_managers.get_shared_memory_metadata(info.key);
        info.remote_metadata = msg.metadata;

        m_id_mapping.add_mapping(client_id, info);

        m_pending_connects.erase(pending);
    }

    // Create server connector outside of mutex to avoid deadlock
    // Create Server_service_interface_definition for the remote service
    socom::Server_service_interface_definition server_config{
        state->service, socom::to_num_of_methods(msg.num_methods),
        socom::to_num_of_events(msg.num_events)};

    // Create callbacks for the server connector that send IPC messages
    socom::Disabled_server_connector::Callbacks server_callbacks{
        [](socom::Enabled_server_connector&, socom::Method_id, socom::Payload::Sptr,
           socom::Method_call_reply_data_opt,
           socom::Posix_credentials const&) -> socom::Method_invocation::Uptr {
            // No-op callback - remote service methods are handled through IPC
            return nullptr;
        },
        [this, client_id, provided_id = info.local_handle](socom::Enabled_server_connector&,
                                                           socom::Event_id event_id,
                                                           socom::Event_state event_state) {
            auto const lock = m_mutex.lock();
            Reply_channel* conn = m_connections.get_reply_channel(client_id);

            if (conn == nullptr) {
                return;
            }

            Message_frame<Subscribe_event> msg;
            msg.payload.provided_id = provided_id;
            msg.payload.event_id = event_id;
            msg.payload.subscribe = event_state == socom::Event_state::subscribed;
            (void)conn->send(msg);
        },
        [](socom::Enabled_server_connector&, socom::Event_id) {
            // No-op callback - remote service event updates are handled through IPC
        },
        [](socom::Enabled_server_connector&,
           socom::Method_id) -> score::Result<socom::Writable_payload::Uptr> {
            // No-op callback - remote service method payloads are handled through IPC
            // Return null payload as we're not allocating anything for remote service methods
            return socom::Writable_payload::Uptr{};
        }};

    // Create server connector through the runtime
    auto server_connector_result = m_runtime.make_server_connector(server_config, state->instance,
                                                                   std::move(server_callbacks));

    if (!server_connector_result) {
        assert(false);
        return;  // Failed to create server connector, log and continue
    }

    // Enable the server connector to notify the local runtime
    auto enabled_connector =
        socom::Disabled_server_connector::enable(std::move(server_connector_result).value());

    if (enabled_connector) {
        auto const lock = m_mutex.lock();
        m_service_states.add_server_connector(info.key, std::move(enabled_connector));
    }
}

void Gateway_ipc_binding_base::send_request_service(
    score::socom::Service_interface_definition const& configuration,
    score::socom::Service_instance const& instance, bool in_use) noexcept {
    log_it("in_use == ", in_use);
    Message_frame<Request_service> msg;
    msg.payload.service_id = make_service(configuration.interface);
    msg.payload.instance_id = make_instance_id(instance);
    msg.payload.in_use = in_use;
    score::socom::Enabled_server_connector::Uptr removed_connector;
    auto const lock = m_mutex.lock();

    auto const key = m_keys.get(configuration, instance);
    if (m_service_states.has_client_connector(key)) {
        // Callback call was triggered by our creation of Client_connector
        log_it("Client connector already exists for key, skipping creation and offer");
        return;
    }

    (void)m_connections.send_to_all(msg);

    auto state_opt = m_service_states.process_request_service(key, configuration, msg.payload);
    removed_connector = std::move(state_opt.connector);
    if (!state_opt.service_state) {
        m_id_mapping.remove_service(key);
        m_pending_connects.clear_pending_connects_for_key(key);
        return;
    }

    auto& state = state_opt.service_state->get();
    maybe_send_connect_service_locked(key, state);
}

void Gateway_ipc_binding_base::send_offer_service_to_client(Reply_channel& conn,
                                                            Service const& service,
                                                            Instance_id const& instance,
                                                            bool offered) noexcept {
    log_it("offered ==", offered);

    Message_frame<Offer_service> msg;
    msg.payload.service_id = service;
    msg.payload.instance_id = instance;
    msg.payload.offered = offered;

    (void)conn.send(msg);
}

void Gateway_ipc_binding_base::maybe_send_connect_service_locked(Key_t const& key,
                                                                 Service_state& state) noexcept {
    auto send_func = [this, &key](auto const& client_id, auto const& remote_handle,
                                  auto const& connect_service) {
        auto* conn = m_connections.get_reply_channel(client_id);
        if (conn == nullptr) {
            return true;
        }

        m_pending_connects.emplace(remote_handle, {key, client_id});

        return conn->send(connect_service).has_value();
    };

    state.send_connect_service(m_next_local_id, m_keys, m_slot_managers, send_func);
}

void Gateway_ipc_binding_base::remove_client_state_locked(Client_id client_id) noexcept {
    m_service_states.remove_event_subscriptions_for_client(client_id);
    m_service_states.remove_offers(client_id);

    m_id_mapping.remove_client(client_id);
    m_pending_connects.clear_pending_connects(
        [&client_id](auto const& val) { return val.client_id == client_id; });
}

void Gateway_ipc_binding_base::clear_pending_connects_for_key_locked(
    Key_t const& key, Client_id const& client_id) noexcept {
    m_pending_connects.clear_pending_connects([&key, &client_id](auto const& val) {
        return val.key == key && val.client_id == client_id;
    });
}

}  // namespace score::gateway_ipc_binding
