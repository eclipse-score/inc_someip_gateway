/********************************************************************************
 * Copyright (c) 2025 Elektrobit Automotive GmbH
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

#ifndef SCORE_SOCOM_SERVER_CONNECTOR_IMPL_HPP
#define SCORE_SOCOM_SERVER_CONNECTOR_IMPL_HPP

#include <future>
#include <list>
#include <mutex>
#include <score/socom/service_interface_configuration.hpp>
#include <set>
#include <vector>

#include "endpoint.hpp"
#include "final_action.hpp"
#include "messages.hpp"
#include "runtime_registration.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/service_interface.hpp"
#include "temporary_thread_id_add.hpp"

namespace score {
namespace socom {

class Runtime_impl;

namespace server_connector {

class Impl;

class Client_connection {
   public:
    explicit Client_connection(Impl& impl, Client_connector_endpoint client);

    template <typename MessageType>
    typename MessageType::Return_type receive(MessageType message) const;

    Client_connector_endpoint get_client_endpoint() const;

   private:
    Impl& m_impl;
    Client_connector_endpoint m_client;
};

class Event {
   public:
    // \retval true First client has been added
    // \retval false 2nd or higher number client has been added
    bool add_client(Client_connection const& client) {
        auto const first_client = m_clients.empty();
        m_clients.insert(&client);
        return first_client;
    }

    // \retval true Last client has been removed
    // \retval false None (client not contained) or one of more than 2 existing clients has been
    // removed
    bool remove_client(Client_connection const& client) {
        if (m_clients.empty()) {
            return false;
        }

        m_clients.erase(&client);
        return m_clients.empty();
    }

    void clear() { m_clients.clear(); }

    std::vector<Client_connector_endpoint> get_clients() const {
        std::vector<Client_connector_endpoint> result{};
        result.reserve(m_clients.size());
        std::for_each(std::begin(m_clients), std::end(m_clients), [&result](auto const& item) {
            result.emplace_back(item->get_client_endpoint());
        });
        return result;
    }

   private:
    std::set<Client_connection const*> m_clients{};
};

class Impl final : public Disabled_server_connector, public Enabled_server_connector {
   public:
    using Listen_endpoint = Server_connector_listen_endpoint;
    using Endpoint = Server_connector_endpoint;

    Impl(Runtime_impl& runtime, Server_service_interface_configuration configuration,
         Service_instance instance, Disabled_server_connector::Callbacks callbacks,
         Final_action final_action, Posix_credentials const& credentials);
    Impl(Impl const&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl const&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl() noexcept override;

    // interface ::score::socom::Enabled_server_connector
    Result<Blank> update_event(Event_id server_id, Payload::Sptr payload) noexcept override;
    Result<Blank> update_requested_event(Event_id server_id,
                                         Payload::Sptr payload) noexcept override;
    Result<Blank> set_event_subscription_state(Event_id server_id,
                                               Event_state event_state) noexcept override;
    Result<Event_mode> get_event_mode(Event_id server_id) const noexcept override;
    Impl* enable() override;
    Impl* disable() noexcept override;
    Server_service_interface_configuration const& get_configuration() const noexcept override;
    Service_instance const& get_service_instance() const noexcept override;

    // Endpoint APIs
    // Listen endpoint
    message::Connect::Return_type receive(message::Connect message);

    // Connection endpoint
    message::Call_method::Return_type receive(Client_connection const& client,
                                              message::Call_method const& message);
    message::Posix_credentials::Return_type receive(Client_connection const& client,
                                                    message::Posix_credentials const& message);
    message::Subscribe_event::Return_type receive(Client_connection const& client,
                                                  message::Subscribe_event message);
    message::Unsubscribe_event::Return_type receive(Client_connection const& client,
                                                    message::Unsubscribe_event message);
    message::Request_event_update::Return_type receive(Client_connection const& client,
                                                       message::Request_event_update message);

   private:
    enum class Event_ack_state : std::uint8_t { not_ack = 0U, ack };
    struct Event_info {
        Event_ack_state ack_state;
        Event_mode mode;
    };

    using Clients = std::list<Client_connection>;
    using Events = std::vector<Event>;
    using Event_infos = std::vector<Event_info>;

    void unsubscribe_event();
    void unsubscribe_event(Client_connection const& client);
    void unsubscribe_event(Client_connection const& client, Event_id id);
    void remove_client(Clients::iterator client);

    template <typename MessageType>
    typename MessageType::Return_type send_all(MessageType message) const;

    template <typename MessageType>
    static typename MessageType::Return_type send(Client_connector_endpoint const& client,
                                                  MessageType message);

    template <typename MessageType>
    static typename MessageType::Return_type send(
        std::vector<Client_connector_endpoint> const& clients, MessageType message);

    Runtime_impl& m_runtime;
    Server_service_interface_configuration const m_configuration;
    Service_instance const m_instance;
    Disabled_server_connector::Callbacks const m_callbacks;
#ifdef WITH_SOCOM_DEADLOCK_DETECTION
    Deadlock_detector m_deadlock_detector;
#endif
    mutable std::mutex m_mutex;
    std::promise<void> m_stop_complete_promise;
    Reference_token m_stop_block_token;  // Protected by m_mutex
    Events m_subscriber;                 // Entries protected by m_mutex
    Events m_update_requester;           // Entries protected by m_mutex
    Event_infos m_event_infos;           // Entries protected by m_mutex
    Clients m_clients;                   // Protected by m_mutex
    Registration m_registration;
    Final_action m_final_action;
    Posix_credentials m_credentials;
};

template <typename MessageType>
typename MessageType::Return_type Impl::send_all(MessageType message) const {
    std::unique_lock<std::mutex> lock{m_mutex};
    auto locked_clients = m_clients;
    lock.unlock();

    std::for_each(std::begin(locked_clients), std::end(locked_clients),
                  [&message](auto const& client) { client.get_client_endpoint().send(message); });
}

template <typename MessageType>
typename MessageType::Return_type Impl::send(Client_connector_endpoint const& client,
                                             MessageType message) {
    client.send(message);
}

template <typename MessageType>
typename MessageType::Return_type Impl::send(std::vector<Client_connector_endpoint> const& clients,
                                             MessageType message) {
    std::for_each(std::begin(clients), std::end(clients),
                  [&message](auto& client) { client.send(message); });
}

template <typename MessageType>
typename MessageType::Return_type Client_connection::receive(MessageType message) const {
    return m_impl.receive(*this, message);
}

inline Client_connector_endpoint Client_connection::get_client_endpoint() const { return m_client; }

}  // namespace server_connector
}  // namespace socom
}  // namespace score

#endif  // SCORE_SOCOM_SERVER_CONNECTOR_IMPL_HPP
