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

#include "runtime_impl.hpp"

#include <algorithm>
#include <iterator>
#include <memory>
#include <score/assert.hpp>

#include "client_connector_impl.hpp"
#include "score/mw/log/logging.h"
#include "score/socom/client_connector.hpp"
#include "score/socom/final_action.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/service_interface_identifier.hpp"
#include "server_connector_impl.hpp"

namespace score::socom {

namespace {

class Final_action_registration final : public IRegistration {
    // will be executed as a final action on destruction.
    Final_action m_final_action;

   public:
    explicit Final_action_registration(Final_action final_action)
        : m_final_action{std::move(final_action)} {}
};

bool is_minor_version_compatible(Service_interface_identifier const& server,
                                 Service_interface_identifier const& client) {
    return client.version.minor <= server.version.minor;
}

bool is_interface_compatible(Service_interface_identifier const& server,
                             Service_interface_identifier const& client) {
    // Defensive programming. This function is called in the two register_connector functions.
    // First, a record is loaded depending on service interface and instance. The record ensures
    // that the right server/client is loaded, therefore this function will always return true.

    return (server.id == client.id) && (server.version.major == client.version.major) &&
           is_minor_version_compatible(server, client);
}

bool is_valid(Client_connector::Callbacks const& callbacks) {
    return !callbacks.on_service_state_change.empty() && !callbacks.on_event_update.empty() &&
           !callbacks.on_event_requested_update.empty() &&
           !callbacks.on_event_payload_allocate.empty();
}

bool is_valid(Disabled_server_connector::Callbacks const& callbacks) {
    return !callbacks.on_method_call.empty() && !callbacks.on_event_update_request.empty() &&
           !callbacks.on_event_subscription_change.empty() &&
           !callbacks.on_method_call_payload_allocate.empty();
}

}  // namespace

Service_database::Service_database(std::mutex& runtime_mutex) : m_runtime_mutex{runtime_mutex} {}

Service_record& Service_database::get_record(Service_interface_identifier const& interface,
                                             Service_instance const& instance) {
    auto it_interface = m_service_records.find(interface);
    if (it_interface == std::end(m_service_records)) {
        it_interface = m_service_records.emplace(interface, Service_instances{}).first;
    }

    auto& record =
        it_interface->second.emplace(instance, Service_record{m_runtime_mutex}).first->second;

    return record;
}

Service_record::Service_record(std::mutex& runtime_mutex) : m_runtime_mutex{runtime_mutex} {}

Service_record::Server_registration Service_record::register_server_connector(
    Service_interface_identifier const& interface, SC_impl::Listen_endpoint connector) {
    // Duplicate server connectors are not allowed.
    SCORE_LANGUAGE_FUTURECPP_ASSERT(!m_server);

    m_server.emplace(Interfaced_server{interface, std::move(connector)});
    auto final_action = [this]() {
        {
            std::lock_guard<std::mutex> const lock{m_runtime_mutex};
            m_server.reset();
        }
    };

    return Server_registration{
        std::make_unique<Final_action_registration>(Final_action{std::move(final_action)}),
        m_client};
}

Result<Service_record::Client_registration> Service_record::register_client_connector(
    Service_interface_identifier const& interface, CC_impl::Server_indication on_server_update) {
    if (m_client) {
        return MakeUnexpected(Construction_error::duplicate_client);
    }
    m_client.emplace(Interfaced_client{interface, std::move(on_server_update)});

    auto remove_from_registry = [this]() {
        std::lock_guard<std::mutex> const lock{m_runtime_mutex};
        m_client.reset();
    };

    return Client_registration{
        std::make_unique<Final_action_registration>(Final_action{std::move(remove_from_registry)}),
        m_server};
}

Result<Client_connector::Uptr> Runtime_impl::make_client_connector(
    Service_interface_definition configuration, Service_instance instance,
    Client_connector::Callbacks callbacks) noexcept {
    if (!is_valid(callbacks)) {
        return MakeUnexpected(Construction_error::callback_missing);
    }

    // check if one is already registered for this service interface and instance and return error
    // if yes
    auto client_connector =
        std::make_unique<CC_impl>(std::move(configuration), instance, std::move(callbacks));

    auto registration = register_connector(client_connector->get_configuration(),
                                           client_connector->get_service_instance(),
                                           client_connector->make_on_server_update_callback());

    if (!registration) {
        return Unexpected{registration.error()};
    }

    client_connector->set_registration(std::move(*registration));

    return {std::move(client_connector)};
}

Result<Disabled_server_connector::Uptr> Runtime_impl::make_server_connector(
    Server_service_interface_definition configuration, Service_instance instance,
    Disabled_server_connector::Callbacks callbacks) noexcept {
    Service_instance_identifier const identifier{configuration.get_interface(), instance};

    if (!is_valid(callbacks)) {
        return MakeUnexpected(Construction_error::callback_missing);
    }

    {
        std::lock_guard<std::mutex> const lock(this->m_service_identifiers->mutex);
        if (!m_service_identifiers->data.insert(identifier).second) {
            return MakeUnexpected(Construction_error::duplicate_service);
        }
    }

    Final_action final_action{
        [identifier, identifiers = std::weak_ptr<Service_identifiers>{m_service_identifiers}]() {
            auto const locked_identifiers = identifiers.lock();
            if (locked_identifiers) {
                std::lock_guard<std::mutex> const lock(locked_identifiers->mutex);
                locked_identifiers->data.erase(identifier);
            }
        }};

    return {std::make_unique<SC_impl>(*this, std::move(configuration), instance,
                                      std::move(callbacks), std::move(final_action))};
}

Result<Registration> Runtime_impl::register_connector(
    Service_interface_definition const& configuration, Service_instance const& instance,
    CC_impl::Server_indication const& on_server_update) {
    std::unique_lock<std::mutex> lock{m_runtime_mutex};
    auto& sii_record = m_database.get_record(configuration.interface, instance);
    auto result = sii_record.register_client_connector(configuration.interface, on_server_update);
    lock.unlock();

    if (!result) {
        return Unexpected{result.error()};
    }

    if (result->current_server) {
        if (is_minor_version_compatible(result->current_server->interface,
                                        configuration.interface)) {
            SCORE_LANGUAGE_FUTURECPP_ASSERT(is_interface_compatible(
                result->current_server->interface, configuration.interface));
            on_server_update(result->current_server->endpoint);
        } else {
            score::mw::log::LogError()
                << "SOCom error: Bind client to server - minor version incompatible:"
                << " client=" << configuration.interface.id
                << ", server=" << result->current_server->interface.id
                << ", instance=" << instance.id;
        }
    }

    return std::move(result->registration);
}

Registration Runtime_impl::register_connector(Service_interface_identifier const& interface,
                                              Service_instance const& instance,
                                              SC_impl::Listen_endpoint endpoint) {
    std::unique_lock<std::mutex> lock{m_runtime_mutex};
    auto& sii_record = m_database.get_record(interface, instance);
    auto result = sii_record.register_server_connector(interface, endpoint);
    lock.unlock();

    auto const connect_client = [&endpoint, &interface, &instance](auto const& client) {
        if (is_minor_version_compatible(interface, client.interface)) {
            SCORE_LANGUAGE_FUTURECPP_ASSERT(is_interface_compatible(interface, client.interface));
            client.indication(endpoint);
        } else {
            score::mw::log::LogError()
                << "SOCom error: Bind client to server - minor version incompatible:"
                << " client=" << client.interface.id << ", server=" << interface.id
                << ", instance=" << instance.id;
        }
    };

    if (result.current_client) {
        connect_client(*result.current_client);
    }

    return std::move(result.registration);
}

}  // namespace score::socom
