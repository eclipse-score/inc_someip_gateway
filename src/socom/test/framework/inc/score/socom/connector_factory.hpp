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

#ifndef SOCOM_CONNECTOR_FACTORY_HPP
#define SOCOM_CONNECTOR_FACTORY_HPP

#include "score/socom/client_connector.hpp"
#include "score/socom/posix_credentials.hpp"
#include "score/socom/runtime.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/service_interface_configuration.hpp"
#include "score/socom/socom_mocks.hpp"
#include "score/socom/utilities.hpp"

namespace score::socom {

/// \brief Creates client and server connectors with default values
class Connector_factory {
    ::score::socom::Runtime::Uptr m_runtime;
    ::score::socom::Server_service_interface_configuration m_configuration;
    ::score::socom::Service_instance m_instance;

    ::score::socom::Runtime& get_runtime();

   public:
    /// \brief Creates a Connector_factory with default configuration
    ///
    /// \param[in] configuration default configuration for created server and clients
    /// \param[in] instance default intance for created server and clients
    Connector_factory(::score::socom::Server_service_interface_configuration configuration,
                      ::score::socom::Service_instance instance);

    /// \brief Creates a Connector_factory with default configuration
    ///
    /// \param[in] sif default service interface for created server and clients
    /// \param[in] version Version of the server interface
    /// \param[in] methods Number of Methods
    /// \param[in] events Number of Events
    /// \param[in] instance default intance for created server and clients
    Connector_factory(::score::socom::Service_interface const& sif,
                      ::score::socom::Num_of_methods num_methods,
                      ::score::socom::Num_of_events num_events,
                      ::score::socom::Service_instance instance);

    /// \brief Copy constructor which copies the configuraton of con_fac
    ///
    /// \param[in] con_fac objects whichs configuration is copied
    Connector_factory(Connector_factory const& con_fac);

    Connector_factory(Connector_factory&&) = default;

    ~Connector_factory() = default;

    Connector_factory& operator=(Connector_factory const&) = delete;
    Connector_factory& operator=(Connector_factory&&) = delete;

    /// \return Service_finder instance of the internal runtime object
    ::score::socom::Runtime& get_service_finder();

    /// \brief Subscribe to availability changes of running servers/services
    ///
    /// During execution of subscribe_find_service() the on_result_change callback
    /// will be called with the already known set of services.
    /// After the function has been called and on each service availability change
    /// the callback on_result_change will be called. The callback will be
    /// called as long as the returned Find_subscription object is not destroyed.
    ///
    /// \param[in] on_result_change callback which is called when service state changes
    /// \param[in] instance filter for a specific service instance
    /// \return RAII object which keeps the subscription alive until it is destroyed
    ::score::socom::Find_subscription subscribe_find_service(
        ::score::socom::Find_result_change_callback on_result_change,
        std::optional<::score::socom::Service_instance> instance = {},
        std::optional<::score::socom::Bridge_identity> identity = {});

    /// \brief Legacy subscribe to availability changes of running servers/services
    ///
    /// During execution of legacy subscribe_find_service() the on_result_set_change callback
    /// will be called with the already known set of services.
    /// After the function has been called and on each service availability change
    /// the callback on_result_set_change will be called. The callback will be
    /// called as long as the returned Find_subscription object is not destroyed.
    ///
    /// \param[in] on_result_set_change callback which is called when service state changes
    /// \param[in] instance filter for a specific service instance
    /// \return RAII object which keeps the subscription alive until it is destroyed
    ::score::socom::Find_subscription subscribe_find_service(
        ::score::socom::Find_result_callback on_result_set_change,
        std::optional<::score::socom::Service_instance> instance = {});

    /// \brief Subscribe to availability changes of running servers/services
    ///
    /// During execution of subscribe_find_service() the on_result_change callback
    /// will be called with the already known set of services.
    /// After the function has been called and on each service availability change
    /// the callback on_result_change will be called. The callback will be
    /// called as long as the returned Find_subscription object is not destroyed.
    ///
    /// Results of bridges are not forwarded to on_result_change and only Runtime
    /// local services are reported.
    ///
    /// \param[in] on_result_change callback which is called when service state changes
    /// \return RAII object which keeps the subscription alive until it is destroyed
    ::score::socom::Find_subscription subscribe_find_service_wildcard(
        ::score::socom::Find_result_change_callback on_result_change);

    /// \brief Registers bridge callbacks at the runtime until registration is destroyed
    ///
    /// Bridges can provide additional services via IPC. The callbacks are used the query the
    /// bridges for services.
    ///
    /// \param[in] subscribe_find_service Callback to call to search for services
    /// \param[in] request_service
    /// \return RAII object which keeps the registration alive until it is destroyed
    ::score::Result<::score::socom::Service_bridge_registration> register_service_bridge(
        ::score::socom::Bridge_identity identity,
        ::score::socom::Subscribe_find_service_function subscribe_find_service,
        ::score::socom::Request_service_function request_service);

    /// \brief Create server connector with default configuration
    ///
    /// \param[in] sc_callbacks callbacks for the server connector
    /// \return server connector
    ::score::socom::Disabled_server_connector::Uptr create_server_connector(
        Optional_reference<Server_connector_callbacks_mock> sc_callbacks);

    /// \brief Create server connector with default configuration
    ///
    /// \param[in] sc_callbacks callbacks for the server connector
    /// \return server connector factory result
    ::score::socom::Disabled_server_connector::Uptr create_server_connector(
        ::score::socom::Disabled_server_connector::Callbacks sc_callbacks);

    /// \brief Create server connector with default configuration
    ///
    /// \param[in] sc_callbacks callbacks for the server connector
    /// \return server connector factory result
    ::score::Result<::score::socom::Disabled_server_connector::Uptr>
    create_server_connector_with_result(
        ::score::socom::Disabled_server_connector::Callbacks sc_callbacks);

    /// \brief Create server connector with default configuration
    ///
    /// \param[in] sc_callbacks callbacks for the server connector
    /// \return server connector factory result
    ::score::Result<::score::socom::Disabled_server_connector::Uptr>
    create_server_connector_with_result(
        Optional_reference<Server_connector_callbacks_mock> sc_callbacks);

    /// \brief Create server connector with custom configuration
    ///
    /// \param[in] configuration other configuration
    /// \param[in] instance other instance
    /// \param[in] sc_callbacks callbacks for the server connector
    /// \return server connector
    ::score::socom::Disabled_server_connector::Uptr create_server_connector(
        ::score::socom::Server_service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance,
        Optional_reference<Server_connector_callbacks_mock> sc_callbacks);

    /// \brief Create server connector with custom configuration and POSIX credentials
    ///
    /// \param[in] configuration other configuration
    /// \param[in] instance other instance
    /// \param[in] sc_callbacks callbacks for the server connector
    /// \param[in] credentials POSIX credentials
    /// \return server connector
    ::score::socom::Disabled_server_connector::Uptr create_server_connector(
        ::score::socom::Server_service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance,
        Optional_reference<Server_connector_credentials_callbacks_mock> sc_callbacks,
        ::score::socom::Posix_credentials const& credentials);

    /// \brief Create and enable server connector with default configuration
    ///
    /// \param[in] sc_callbacks callbacks for the server connector
    /// \return enabled server connector
    ::score::socom::Enabled_server_connector::Uptr create_and_enable(
        Optional_reference<Server_connector_callbacks_mock> sc_callbacks);

    /// \brief Create and enable server connector with custom configuration
    ///
    /// \param[in] configuration other configuration
    /// \param[in] instance other instance
    /// \param[in] sc_callbacks callbacks for the server connector
    /// \return enabled server connector
    ::score::socom::Enabled_server_connector::Uptr create_and_enable(
        ::score::socom::Server_service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance,
        Optional_reference<Server_connector_callbacks_mock> sc_callbacks);

    /// \brief Create and enable server connector with custom configuration and POSIX credentials
    ///
    /// \param[in] configuration other configuration
    /// \param[in] instance other instance
    /// \param[in] sc_callbacks callbacks for the server connector
    /// \param[in] credentials POSIX credentials
    /// \return enabled server connector
    ::score::socom::Enabled_server_connector::Uptr create_and_enable(
        ::score::socom::Server_service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance,
        Optional_reference<Server_connector_credentials_callbacks_mock> sc_callbacks,
        ::score::socom::Posix_credentials const& credentials);

    /// \brief Create client connector with default configuration
    ///
    /// \param[in] cc_callbacks callbacks for the client connector
    /// \return client connector
    ::score::socom::Client_connector::Uptr create_client_connector(
        Optional_reference<Client_connector_callbacks_mock> cc_callbacks);

    /// \brief Create client connector with default configuration
    ///
    /// \param[in] cc_callbacks callbacks for the client connector
    /// \return client connector
    ::score::socom::Client_connector::Uptr create_client_connector(
        ::score::socom::Client_connector::Callbacks cc_callbacks);

    /// \brief Create client connector with custom configuration
    ///
    /// \param[in] configuration other configuration
    /// \param[in] instance other instance
    /// \param[in] cc_callbacks callbacks for the client connector
    /// \return client connector
    ::score::socom::Client_connector::Uptr create_client_connector(
        ::score::socom::Service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance,
        Optional_reference<Client_connector_callbacks_mock> cc_callbacks);

    /// \brief Create client connector with custom configuration and POSIX credentials
    ///
    /// \param[in] configuration other configuration
    /// \param[in] instance other instance
    /// \param[in] cc_callbacks callbacks for the client connector
    /// \param[in] credentials POSIX credentials
    /// \return client connector
    ::score::socom::Client_connector::Uptr create_client_connector(
        ::score::socom::Service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance,
        Optional_reference<Client_connector_callbacks_mock> cc_callbacks,
        ::score::socom::Posix_credentials const& credentials);

    /// \brief Create client connector with custom configuration and POSIX credentials
    ///
    /// \param[in] configuration other configuration
    /// \param[in] instance other instance
    /// \param[in] cc_callbacks callbacks for the client connector
    /// \param[in] credentials POSIX credentials
    /// \return client connector
    ::score::Result<::score::socom::Client_connector::Uptr> create_client_connector_with_result(
        ::score::socom::Service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance,
        Optional_reference<Client_connector_callbacks_mock> cc_callbacks,
        std::optional<::score::socom::Posix_credentials> const& credentials);

    /// \brief Create client connector with custom configuration
    ///
    /// \param[in] configuration other configuration
    /// \param[in] instance other instance
    /// \param[in] cc_callbacks callbacks for the client connector
    /// \return client connector
    ::score::Result<::score::socom::Client_connector::Uptr> create_client_connector_with_result(
        ::score::socom::Service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance,
        ::score::socom::Client_connector::Callbacks cc_callbacks);

    /// \brief Create client connector with custom configuration and POSIX credentials
    ///
    /// \param[in] configuration other configuration
    /// \param[in] instance other instance
    /// \param[in] cc_callbacks callbacks for the client connector
    /// \param[in] credentials POSIX credentials
    /// \return client connector
    ::score::Result<::score::socom::Client_connector::Uptr> create_client_connector_with_result(
        ::score::socom::Service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance,
        ::score::socom::Client_connector::Callbacks cc_callbacks,
        ::score::socom::Posix_credentials const& credentials);

    /// \brief Create and connect client connector with default configuration
    ///
    /// \param[in] cc_callbacks callbacks for the client connector
    /// \return connected client connector
    ::score::socom::Client_connector::Uptr create_and_connect(
        Optional_reference<Client_connector_callbacks_mock> cc_callbacks);

    /// \brief Create and connect client connector with custom configuration
    ///
    /// \param[in] configuration other configuration
    /// \param[in] instance other instance
    /// \param[in] cc_callbacks callbacks for the client connector
    /// \param[in] credentials optional POSIX credentials
    /// \return connected client connector
    ::score::socom::Client_connector::Uptr create_and_connect(
        ::score::socom::Service_interface_configuration const& configuration,
        ::score::socom::Service_instance const& instance,
        Optional_reference<Client_connector_callbacks_mock> cc_callbacks,
        std::optional<::score::socom::Posix_credentials> const& credentials);

    /// \return default configuration for server and client connector
    ::score::socom::Server_service_interface_configuration const& get_configuration() const;

    /// \return default instance for server and client connector
    ::score::socom::Service_instance const& get_instance() const;

    /// \return method names in same order as used by server and client
    std::size_t get_num_methods() const noexcept;

    /// \return event names in same order as used by server and client
    std::size_t get_num_events() const noexcept;
};

}  // namespace score::socom

#endif
