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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_SERVICE_STATE
#define SRC_GATEWAY_IPC_BINDING_SRC_SERVICE_STATE

#include <score/os/errno.h>

#include <cassert>
#include <cerrno>
#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <score/gateway_ipc_binding/shared_memory_slot_manager.hpp>
#include <score/socom/client_connector.hpp>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gateway_ipc_binding_util.hpp"
#include "key.hpp"
#include "shared_memory_managers.hpp"

namespace score::gateway_ipc_binding {

struct Event_subscription_endpoint {
    Client_id client_id;
    Remote_handle provided_id;

    bool operator==(Event_subscription_endpoint const& rhs) const noexcept {
        return client_id == rhs.client_id && provided_id == rhs.provided_id;
    }
};

struct Event_subscription_endpoint_hash {
    std::size_t operator()(Event_subscription_endpoint const& endpoint) const noexcept {
        return std::hash<Client_id>{}(endpoint.client_id) ^
               (std::hash<Remote_handle>{}(endpoint.provided_id) << 1U);
    }
};

using Event_subscriber_set =
    std::unordered_set<Event_subscription_endpoint, Event_subscription_endpoint_hash>;
using Event_subscribers = std::unordered_map<socom::Event_id, Event_subscriber_set>;

struct Offer_state {
    bool offered{false};
    bool connect_sent{false};
    Remote_handle required_id{0};
};

struct Service_state {
    socom::Service_interface_identifier service;
    socom::Service_instance instance;
    Service_counts counts{};
    bool requested{false};
    std::unordered_map<Client_id, Offer_state> offers;
    score::socom::Client_connector::Uptr client_connector{};
    bool client_connector_pending{false};
    score::socom::Enabled_server_connector::Uptr enabled_connector{};
    Event_subscribers event_subscriptions;

    // iterate over offers and send connect IPC message
    void send_connect_service(
        Id_generator<Remote_handle>& next_local_id, Keys& keys,
        Shared_memory_managers& slot_managers,
        std::function<bool(Client_id const&, Remote_handle const& remote_handle,
                           Message_frame<Connect_service>&)>
            send_func) {
        if (!requested) {
            return;
        }
        for (auto& offer_entry : offers) {
            if (offer_entry.second.offered && !offer_entry.second.connect_sent) {
                Client_id const& client_id = offer_entry.first;
                Offer_state& offer = offer_entry.second;

                if (!offer.offered || offer.connect_sent) {
                    continue;
                }

                if (offer.required_id == 0) {
                    offer.required_id = next_local_id.get_next_id();
                }

                Message_frame<Connect_service> msg;
                msg.payload.service_id = make_service(service);
                fill_fixed_string(msg.payload.instance_id, instance.id.string_view());
                msg.payload.required_id = offer.required_id;
                msg.payload.metadata =
                    slot_managers.get_shared_memory_metadata(keys.get(service, instance));
                msg.payload.in_use = true;
                auto const send_result = send_func(client_id, offer.required_id, msg);
                if (send_result) {
                    // Keep connect_sent=false so a later reconnect can retry this handshake.
                    offer.connect_sent = true;
                }
            }
        }
    }

    void remove_event_subscriptions(Client_id client_id,
                                    score::socom::Client_connector* connector) {
        for (auto event_it = event_subscriptions.begin(); event_it != event_subscriptions.end();) {
            auto& subscribers = event_it->second;
            for (auto endpoint_it = subscribers.begin(); endpoint_it != subscribers.end();) {
                if (endpoint_it->client_id == client_id) {
                    endpoint_it = subscribers.erase(endpoint_it);
                } else {
                    ++endpoint_it;
                }
            }

            if (!subscribers.empty()) {
                ++event_it;
                continue;
            }

            if (connector != nullptr) {
                connector->unsubscribe_event(event_it->first);
            }
            event_it = event_subscriptions.erase(event_it);
        }
    }

    void remove_event_subscription(Event_subscription_endpoint const endpoint,
                                   score::socom::Client_connector* connector) {
        for (auto event_it = event_subscriptions.begin(); event_it != event_subscriptions.end();) {
            auto& subscribers = event_it->second;
            subscribers.erase(endpoint);

            if (!subscribers.empty()) {
                ++event_it;
                continue;
            }

            if (connector != nullptr) {
                connector->unsubscribe_event(event_it->first);
            }
            event_it = event_subscriptions.erase(event_it);
        }
    }
};

class Service_states {
   public:
    using Service_state_map = std::unordered_map<Key_t, Service_state>;

    // Used for delayed destruction of Enabled_server_connector
    template <typename T>
    struct Delayed_destruction {
        T service_state;
        score::socom::Enabled_server_connector::Uptr connector;
    };

    Service_state& add_service(Key_t const& key, Service const& interface,
                               Instance_id const& instance,
                               socom::Server_service_interface_definition const& configuration) {
        auto& insert_result = get_or_create(key, interface, instance);

        insert_result.counts = {static_cast<std::uint16_t>(configuration.get_num_methods()),
                                static_cast<std::uint16_t>(configuration.get_num_events())};
        insert_result.requested = true;

        return insert_result;
    }

    Delayed_destruction<Service_state&> process_offer(Key_t const& key, Client_id const& client_id,
                                                      Offer_service const& msg) {
        auto& state = get_or_create(key, msg.service_id, msg.instance_id);

        Delayed_destruction<Service_state&> result{.service_state = state, .connector = nullptr};
        if (!msg.offered) {
            state.offers.erase(client_id);
            result.connector = std::move(state.enabled_connector);
        } else {
            auto& offer_state = state.offers[client_id];
            offer_state.offered = true;
        }
        return result;
    }

    Delayed_destruction<std::optional<std::reference_wrapper<Service_state>>>
    process_request_service(Key_t const& key,
                            socom::Service_interface_definition const& configuration,
                            Request_service const& msg) {
        Delayed_destruction<std::optional<std::reference_wrapper<Service_state>>> result{
            .service_state = std::nullopt, .connector = nullptr};
        auto& state = get_or_create(key, msg.service_id, msg.instance_id);

        if (!msg.in_use) {
            result.connector = std::move(state.enabled_connector);
            state.client_connector_pending = false;
            m_service_states.erase(key);
            return result;
        }

        assert(msg.in_use);
        state.counts = {static_cast<std::uint16_t>(configuration.num_methods),
                        static_cast<std::uint16_t>(configuration.num_events)};
        state.requested = msg.in_use;
        result.service_state = state;
        return result;
    }

    void add_server_connector(Key_t const& key, socom::Enabled_server_connector::Uptr connector) {
        auto state_ref = get(key);
        if (!state_ref) {
            // peer quickly lost interest and state was removed at process_request_service()
            return;
        }
        state_ref->get().enabled_connector = std::move(connector);
    }

    void mark_client_connector_pending(Key_t const& key, Service const& interface,
                                       Instance_id const& instance) {
        auto& state = get_or_create(key, interface, instance);
        state.client_connector_pending = true;
    }

    void clear_client_connector_pending(Key_t const& key) {
        auto state_ref = get(key);
        if (!state_ref) {
            return;
        }

        state_ref->get().client_connector_pending = false;
    }

    void add_client_connector(Key_t const& key, socom::Client_connector::Uptr connector) {
        auto state_ref = get(key);
        // m_services_states is only cleaned at process_request_service(), which might be followed
        // by a call to add_server_connector(). State created prior calling add_client_connector()
        // is never cleaned up so far.
        assert(state_ref && "Service state must exist when adding client connector");
        state_ref->get().client_connector = std::move(connector);
        state_ref->get().client_connector_pending = false;
    }

    socom::Client_connector::Uptr remove_client_connector(Key_t const& key) {
        auto state_ref = get(key);
        if (!state_ref) {
            return nullptr;
        }

        auto& state = state_ref->get();
        state.client_connector_pending = false;
        return std::move(state.client_connector);
    }

    void remove_offers(Client_id const& client_id) {
        for (auto& entry : m_service_states) {
            entry.second.offers.erase(client_id);
        }
    }

    std::vector<score::socom::Client_connector::Uptr> release_client_connectors() {
        std::vector<score::socom::Client_connector::Uptr> released_connectors;
        released_connectors.reserve(m_service_states.size());

        for (auto& [key, state] : m_service_states) {
            (void)key;
            state.client_connector_pending = false;
            if (state.client_connector != nullptr) {
                released_connectors.push_back(std::move(state.client_connector));
            }
        }

        return released_connectors;
    }

    std::optional<std::reference_wrapper<Service_state const>> get(Key_t const& key) const {
        auto it = m_service_states.find(key);
        if (it != m_service_states.end()) {
            return std::cref(it->second);
        }
        return std::nullopt;
    }

    std::optional<std::reference_wrapper<Service_state>> get(Key_t const& key) {
        auto it = m_service_states.find(key);
        if (it != m_service_states.end()) {
            return std::ref(it->second);
        }
        return std::nullopt;
    }

    bool has_connector(Key_t const& key) const {
        auto const state = get(key);
        return state && (state->get().enabled_connector != nullptr);
    }

    bool has_client_connector(Key_t const& key) const {
        auto const state = get(key);
        return state &&
               (state->get().client_connector_pending || state->get().client_connector != nullptr);
    }

    void for_each(std::function<void(Key_t const&, Service_state&)> func) {
        for (auto& entry : m_service_states) {
            func(entry.first, entry.second);
        }
    }

    void update_event_subscription(Key_t const& key, Event_subscription_endpoint const& endpoint,
                                   score::socom::Event_id event_id, bool subscribe) {
        auto state_opt = get(key);
        if (!state_opt) {
            return;
        }
        auto& state = state_opt->get();
        auto& event_subscriptions = state.event_subscriptions;
        auto* connector = state.client_connector.get();

        if (subscribe) {
            auto& subscribers = event_subscriptions[event_id];
            auto const inserted = subscribers.insert(endpoint).second;
            if (connector != nullptr && inserted && subscribers.size() == 1U) {
                connector->subscribe_event(event_id, socom::Event_mode::update);
            }
            return;
        }

        auto event_it = event_subscriptions.find(event_id);
        if (event_it == event_subscriptions.end()) {
            return;
        }

        event_it->second.erase(endpoint);

        if (!event_it->second.empty()) {
            return;
        }

        if (connector != nullptr) {
            connector->unsubscribe_event(event_id);
        }
        event_subscriptions.erase(event_it);
    }

    void remove_event_subscriptions_for_client(Client_id client_id) {
        for (auto& [key, state] : m_service_states) {
            (void)key;
            state.remove_event_subscriptions(client_id, state.client_connector.get());
        }
    }

    void remove_event_subscriptions_for_connection(Key_t const& key, Client_id client_id,
                                                   Remote_handle provided_id) {
        auto state_opt = get(key);
        if (!state_opt) {
            return;
        }

        Event_subscription_endpoint const endpoint{client_id, provided_id};
        auto& state = state_opt->get();
        state.remove_event_subscription(endpoint, state.client_connector.get());
    }

    void clear_event_subscriptions(Key_t const& key) {
        auto state_opt = get(key);
        if (state_opt) {
            state_opt->get().event_subscriptions.clear();
        }
    }

   private:
    Service_state& get_or_create(Key_t const& key, Service const& interface,
                                 Instance_id const& instance) {
        auto const insert_result = m_service_states.emplace(
            key, Service_state{interface.to_socom_identifier(),
                               socom::Service_instance{fixed_string_to_string(instance)}});
        return insert_result.first->second;
    }

    Service_state_map m_service_states;
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_SERVICE_STATE
