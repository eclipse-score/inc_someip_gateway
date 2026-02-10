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

#ifndef SOCOM_CLIENTS_T_HPP
#define SOCOM_CLIENTS_T_HPP

#include <atomic>
#include <future>
#include <optional>
#include <vector>

#include "connector_factory.hpp"
#include "score/socom/client_connector.hpp"
#include "server_t.hpp"
#include "socom_mocks.hpp"
#include "temporary_event_subscription.hpp"
#include "utilities.hpp"

namespace ac {

/// \brief Facade for the client connector and callback mocks.
///
/// It allows easy configuration of mocks and blocks its destruction until all
/// expectations have been fulfilled. The stored client connector is connected
/// and ready for communication after construction by default.
struct Client_data {
   private:
    std::atomic<bool> m_event_callback_called{true};
    std::atomic<bool> m_event_request_callback_called{true};
    std::atomic<bool> m_event_subscription_status_change_called{true};
    std::atomic<bool> m_method_callback_called{true};
    std::atomic<bool> m_not_available{true};
    std::atomic<bool> m_available{true};
    std::atomic<bool> m_event_payload_allocate_called{true};
    std::atomic<uint32_t> m_num_event_callback_called{0};
    std::atomic<uint32_t> m_num_method_callback_called{0};
    Client_connector_callbacks_mock m_callbacks;
    score::socom::Method_reply_callback_mock m_method_callback;
    std::vector<score::socom::Method_invocation::Uptr> m_method_invocations;
    ::score::socom::Client_connector::Uptr m_connector;

    std::atomic<bool>& get_atomic(::score::socom::Service_state const& state);

   public:
    using Vector = std::vector<std::unique_ptr<Client_data>>;

    /// \brief Enum to signal constructor not to connect to the server
    enum No_connect_helper { no_connect, might_connect };

    /// \brief Create Client_data and connect to the server
    ///
    /// \param[in] factory factory to create client connector with
    explicit Client_data(Connector_factory& factory);

    /// \brief Create Client_data but do not connect to the server
    ///
    /// \param[in] factory factory to create client connector with
    Client_data(Connector_factory& factory, No_connect_helper const& connect_helper,
                ::score::socom::Service_state_change_callback const& state_change_callback = {});

    /// \brief Create Client_data but do not connect to the server
    ///
    /// \param[in] factory factory to create client connector with
    /// \param[in] configuration use this instead of the one stored in factory
    /// \param[in] instance use this instead of the one stored in factory
    Client_data(Connector_factory& factory, No_connect_helper const& connect_helper,
                ::score::socom::Service_interface_configuration const& configuration,
                ::score::socom::Service_instance const& instance,
                ::score::socom::Service_state_change_callback const& state_change_callback = {});

    /// \brief Create Client_data with custom configuration, POSIX credentials and connect to the
    /// server
    ///
    /// \param[in] factory factory to create client connector with
    /// \param[in] configuration use this instead of the one stored in factory
    /// \param[in] instance use this instead of the one stored in factory
    /// \param[in] credentials POSIX credentials
    Client_data(Connector_factory& factory,
                ::score::socom::Service_interface_configuration const& configuration,
                ::score::socom::Service_instance const& instance,
                std::optional<::score::socom::Posix_credentials> const& credentials = {});

    Client_data(Client_data const&) = delete;
    Client_data(Client_data&&) = delete;

    /// \brief Block until all expectations have been fulfilled
    ~Client_data();

    Client_data& operator=(Client_data const&) = delete;
    Client_data& operator=(Client_data&&) = delete;

    /// \brief Call subscribe_event() with cara::socom::Event_mode::update
    ///
    /// \param[in] event_id event to subscribe to
    void subscribe_event(::score::socom::Event_id const& event_id);

    /// \brief Call unsubscribe_event()
    ///
    /// \param[in] event_id event to unsubscribe from
    void unsubscribe_event(::score::socom::Event_id const& event_id);

    /// \brief Create event subscription for event_id with cara::socom::Event_mode::update
    ///
    /// \param[in] event_id event to subscribe to
    /// \return RAII object holding the subscription
    std::unique_ptr<Temporary_event_subscription> create_event_subscription(
        ::score::socom::Event_id const& event_id);

    /// \brief Create event subscription for event_id with Event_mode::update_and_initial
    ///
    /// \param[in] sc_callbacks callbacks of sc
    /// \param[in] event_id event to subscribe to
    /// \param[in] payload payload of the event update
    /// \return RAII object holding the subscription
    std::unique_ptr<Temporary_event_subscription> create_event_subscription(
        Server_connector_callbacks_mock& sc_callbacks, ::score::socom::Event_id const& event_id,
        ::score::socom::Payload::Sptr const& payload);

    /// \brief Create event subscription for event_id with Event_mode::update_and_initial
    ///
    /// \param[in] server server answering the event update request
    /// \param[in] event_id event to subscribe to
    /// \param[in] payload payload of the event update
    /// \return RAII object holding the subscription
    std::unique_ptr<Temporary_event_subscription> create_event_subscription(
        Server_data& server, ::score::socom::Event_id const& event_id,
        ::score::socom::Payload::Sptr const& payload);

    /// \brief Create event subscription for event_id with Event_mode::update_and_initial
    ///        but the server is not answering the update event request
    ///
    /// \param[in] server server receiving the event update request
    /// \param[in] event_id event to subscribe to
    /// \param[in] brokenness flag to indicate not to send event update
    /// \return RAII object holding the subscription
    std::unique_ptr<Temporary_event_subscription> create_event_subscription(
        Server_data& server, ::score::socom::Event_id const& event_id,
        Temporary_event_subscription::Brokenness const& brokenness);

    /// \brief Request event update for event_id
    ///
    /// \param[in] event_id event to request update for
    void request_event_update(::score::socom::Event_id const& event_id) const;

    /// \brief Allocate a payload for the given method ID.
    ///
    /// \param method_id ID of the method for which a payload should be allocated.
    /// \return A writable payload in case of successful operation, otherwise an error.
    score::Result<std::unique_ptr<score::socom::Writable_payload>> allocate_method_payload(
        score::socom::Method_id method_id);

    /// \brief Call method method_id with payload
    ///
    /// \param[in] method_id method to call
    /// \param[in] payload payload of method
    void call_method(::score::socom::Method_id const& method_id,
                     ::score::socom::Payload::Sptr const& payload);

    /// \brief Call method method_id with payload and reply callback
    ///
    /// \param[in] method_id method to call
    /// \param[in] payload payload of method
    /// \param[in] reply reply callback
    void call_method(::score::socom::Method_id const& method_id,
                     ::score::socom::Payload::Sptr const& payload,
                     ::score::socom::Method_reply_callback const& reply);

    /// \brief Call method method_id with payload and reply callback
    ///
    /// \param[in] method_id method to call
    /// \param[in] payload payload of method
    /// \param[in] reply reply callback
    void call_method(::score::socom::Method_id const& method_id,
                     ::score::socom::Payload::Sptr const& payload,
                     ::score::socom::Method_call_reply_data reply);

    /// \brief Call method without callback and without expecting a response
    ///
    /// \param[in] method_id method to call
    /// \param[in] payload the data of the method call
    void call_method_fire_and_forget(::score::socom::Method_id const& method_id,
                                     ::score::socom::Payload::Sptr const& payload);

    /// Call method without callback and without expecting a response
    ///
    /// \param[in] method_id method to call
    /// \param[in] payload the data of the method call
    /// \return Method_invocation on success, else error
    score::Result<::score::socom::Method_invocation::Uptr>
    call_method_fire_and_forget_and_return_invocation(::score::socom::Method_id const& method_id,
                                                      ::score::socom::Payload::Sptr const& payload);

    /// \brief Expect state change of configured service
    ///
    /// \param[in] state state into which the service switches to
    /// \return boolean reference which becomes true when the callback is called
    std::atomic<bool> const& expect_service_state_change(
        ::score::socom::Service_state const& state);

    /// \brief Expect state changes of configured service
    ///
    /// \param[in] count number of times the service switches into this state
    /// \param[in] state state into which the service switches to
    /// \return boolean reference which becomes true when the callback is called
    std::atomic<bool> const& expect_service_state_change(
        size_t count, ::score::socom::Service_state const& state);

    /// \brief Expect state changes of configured service
    ///
    /// \param[in] count number of times the service switches into this state
    /// \param[in] state state into which the service switches to
    /// \param[in] conf server configuration received via callback
    /// \return boolean reference which becomes true when the callback is called
    std::atomic<bool> const& expect_service_state_change(
        size_t count, ::score::socom::Service_state const& state,
        Optional_reference<::score::socom::Server_service_interface_configuration const> const&
            conf);

    /// \brief Expect event payload allocation
    ///
    /// \param[in] event_id event which is updated
    /// \param[in] result the return value of the callback
    /// \return boolean reference which becomes true after the callback has been called
    std::atomic<bool> const& expect_event_payload_allocation(
        ::score::socom::Event_id const& event_id,
        score::Result<std::unique_ptr<::score::socom::Writable_payload>> result);

    /// \brief Expect event update
    ///
    /// \param[in] event_id event which is updated
    /// \param[in] payload the data of the send
    /// \return boolean reference which becomes true after the event has been received
    std::atomic<bool> const& expect_event_update(::score::socom::Event_id const& event_id,
                                                 ::score::socom::Payload::Sptr const& payload);

    /// \brief Expect event updates
    ///
    /// \param[in] count number of times the service switches into this state
    /// \param[in] event_id event which is updated
    /// \param[in] payload the data of the send
    /// \return boolean reference which becomes true after all events have been received
    std::atomic<bool> const& expect_event_updates(size_t const& count,
                                                  ::score::socom::Event_id const& event_id,
                                                  ::score::socom::Payload::Sptr const& payload);

    /// Expect event updates
    ///
    /// \param[in] count minimum number of times the service switches into this state
    /// \param[in] event_id event which is updated
    /// \param[in] payload the data of the send
    /// \return future which unblocks after minimum received event updates.
    std::future<void> expect_event_updates_min_number(std::size_t const& count,
                                                      ::score::socom::Event_id const& event_id,
                                                      ::score::socom::Payload::Sptr const& payload);

    /// \brief Expect requested event update
    ///
    /// \param[in] event_id event for which an update is requested
    /// \param[in] payload the data of the send
    /// \return boolean reference which becomes true after the event has been received
    std::atomic<bool> const& expect_requested_event_update(
        ::score::socom::Event_id const& event_id, ::score::socom::Payload::Sptr const& payload);

    /// Expect requested event updates
    ///
    /// \param[in] count minimum number of times the service switches into this state
    /// \param[in] event_id event which is updated
    /// \param[in] payload the data of the send
    /// \return future which unblocks after minimum received event updates.
    std::future<void> expect_requested_event_updates_min_number(
        std::size_t const& count, ::score::socom::Event_id const& event_id,
        ::score::socom::Payload::Sptr const& payload);

    /// \brief Expect and request event update
    ///
    /// \param[in] event_id event for which an update is requested
    /// \param[in] payload the data of the send
    /// \return boolean reference which becomes true after the event has been received
    std::atomic<bool> const& expect_and_request_event_update(
        ::score::socom::Event_id const& event_id, ::score::socom::Payload::Sptr const& payload);

    /// \brief Expect response and call method
    ///
    /// \param[in] method_id method to call
    /// \param[in] payload the data of the method call
    /// \param[in] method_result response of the method
    /// \return boolean reference which becomes true when the response is received
    std::atomic<bool> const& expect_and_call_method(
        ::score::socom::Method_id const& method_id, ::score::socom::Payload::Sptr const& payload,
        ::score::socom::Method_result const& method_result);

    /// \brief Expect responses and call methods
    ///
    /// \param[in] count number of times the service switches into this state
    /// \param[in] method_id method to call
    /// \param[in] payload the data of the method call
    /// \param[in] method_result response of the method
    /// \return boolean reference which becomes true after all responses have been received
    std::atomic<bool> const& expect_and_call_methods(
        size_t const& count, ::score::socom::Method_id const& method_id,
        ::score::socom::Payload::Sptr const& payload,
        ::score::socom::Method_result const& method_result);

    /// \brief Create and connect clients
    ///
    /// \param[in] factory factory used to create and configure the clients
    /// \param[in] size number of clients to create
    /// \return created and connected clients
    static Vector create_clients(Connector_factory& factory, size_t const& size);

    /// \brief Create clients without server connection
    ///
    /// \param[in] factory factory used to create and configure the clients
    /// \param[in] size number of clients to create
    /// \param[in] no_connect do not connect clients to Server_connector
    /// \return created and connected clients
    static Vector create_clients(Connector_factory& factory, size_t const& size,
                                 No_connect_helper const& no_connect);

    /// \brief Create and connect clients with custom configuration
    ///
    /// \param[in] factory factory used to create the clients
    /// \param[in] size number of clients to create
    /// \param[in] configuration use this instead of the one stored in factory
    /// \param[in] instance use this instead of the one stored in factory
    /// \return created and connected clients
    static Vector create_clients(
        Connector_factory& factory, size_t const& size,
        ::score::socom::Service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance);

    /// \brief Expect event update
    ///
    /// \param[in] clients clients which are subscribed to the event
    /// \param[in] event_id event which is updated
    /// \param[in] payload the data of the send
    /// \return boolean references for each client which become true after the event has been
    /// received
    static Callbacks_called_t expect_event_update(Vector& clients,
                                                  ::score::socom::Event_id event_id,
                                                  ::score::socom::Payload::Sptr const& payload);

    /// \brief Expect and request event update
    ///
    /// \param[in] clients clients which are subscribed to the event
    /// \param[in] event_id event which is requested to update
    /// \param[in] payload the data of the send
    /// \return boolean references for each client which become true after the event has been
    /// received
    static Callbacks_called_t expect_and_request_event_update(
        Vector& clients, ::score::socom::Event_id event_id,
        ::score::socom::Payload::Sptr const& payload);

    /// \brief Expect response and call method
    ///
    /// \param[in] clients clients which are subscribed to the event
    /// \param[in] method_id method to call
    /// \param[in] payload the data of the method call
    /// \param[in] method_result response of the method
    /// \return boolean references for each client which become true after the response has been
    /// received
    static Callbacks_called_t expect_and_call_method(
        Vector& clients, ::score::socom::Method_id method_id,
        ::score::socom::Payload::Sptr const& payload,
        ::score::socom::Method_result const& method_result);

    /// \brief Create event subscriptions for event_id with Event_mode::update
    ///
    /// \param[in] clients clients to subscribe to event_id
    /// \param[in] event_id event to subscribe to
    /// \return subscription RAII objects for all clients
    static Subscriptions subscribe(Client_data::Vector const& clients,
                                   ::score::socom::Event_id const& event_id);

    /// \brief Get peer credentials from server.
    ///
    /// \return Result with valid peer credentials or error.
    score::Result<::score::socom::Posix_credentials> get_peer_credentials() const;
};

}  // namespace ac

#endif
