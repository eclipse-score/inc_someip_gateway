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

#ifndef SCORE_SOCOM_RUNTIME_HPP
#define SCORE_SOCOM_RUNTIME_HPP

#include <memory>

#include "score/socom/client_connector.hpp"
#include "score/socom/server_connector.hpp"
#include "score/socom/service_interface_identifier.hpp"

namespace score::socom {

/// \brief Interface that provides access to the service oriented communication (SOCom) middleware.
/// \details SOCom implements a client-service-server based architectural pattern.
/// A service is an instance (Service_instance) of an interface (Service_interface).
/// A server provides a service.
/// Clients use services.
/// The service pattern makes client and server independent from concrete instances of each other
/// (loose coupling). Depending on their availability, SOCom performs the dependency resolution
/// client/server connection and disconnection at runtime.

/// A service interface supports the following communication patterns:
///   - method call (1:1)
///     - client-server-client
///     - client-server
///   - event, also known as publish/subscribe (1:n)
///     - server-clients
class Runtime {
   public:
    /// \brief Alias for an unique pointer to this interface.
    using Uptr = std::unique_ptr<Runtime>;

    Runtime() = default;
    virtual ~Runtime() noexcept = default;
    Runtime(Runtime const&) = delete;
    Runtime(Runtime&&) = delete;
    Runtime& operator=(Runtime const&) = delete;
    Runtime& operator=(Runtime&&) = delete;

    /// \brief Creates a new client connector.
    /// \details Returns a new instance Client_connector registered as a service user for the
    /// service defined by the configuration.interface and instance parameters to the SOCom service
    /// registry.
    ///
    /// \param configuration Service interface configuration.
    /// \param instance Service instance.
    /// \param callbacks User callbacks to be called based on the internal states.
    /// \return A pointer to a Client_connector instance in case of successful operation, otherwise
    /// an error.
    /// \note Construction_error::callback_missing is returned if any of the callbacks is not set.
    [[nodiscard]]
    virtual Result<Client_connector::Uptr> make_client_connector(
        Service_interface_definition configuration, Service_instance instance,
        Client_connector::Callbacks callbacks) noexcept = 0;

    /// \brief Creates a new server connector.
    /// \details Returns a new instance of Disabled_server_connector registered as a service
    /// provider for the service defined by the configuration.interface and instance parameters to
    /// the SOCom service registry if this service does not exist in the runtime service registry
    /// yet.
    ///
    /// Logs an error and returns Construction_error::duplicate_service if a service defined by the
    /// configuration.interface and instance parameters is already registered in the runtime service
    /// registry.

    /// Returns Construction_error::callback_missing if any of the callbacks is not set.
    /// \param configuration Service interface configuration.
    /// \param instance Service instance.
    /// \param callbacks User callbacks to be called based on the internal states.
    /// \return A pointer to a server connector instance in case of successful operation, otherwise
    /// an error.
    [[nodiscard]]
    virtual Result<Disabled_server_connector::Uptr> make_server_connector(
        Server_service_interface_definition configuration, Service_instance instance,
        Disabled_server_connector::Callbacks callbacks) noexcept = 0;
};

/// \brief Function to instantiate a Runtime object.
/// \param logger Logger for logging messages.
/// \return Pointer to Runtime object.
Runtime::Uptr create_runtime();

}  // namespace score::socom

#endif  // SCORE_SOCOM_RUNTIME_HPP
