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

#ifndef SCORE_SOCOM_SERVICE_INTERFACE_CONFIGURATION_HPP
#define SCORE_SOCOM_SERVICE_INTERFACE_CONFIGURATION_HPP

#include <score/socom/event.hpp>
#include <score/socom/method.hpp>
#include <score/socom/service_interface.hpp>

namespace score::socom {

/// description: Strong type for forced proper construction.
enum class Num_of_events : std::size_t {};

/// description: Strong type for forced proper construction.
enum class Num_of_methods : std::size_t {};

Num_of_events to_num_of_events(std::size_t value) noexcept;
Num_of_methods to_num_of_methods(std::size_t value) noexcept;

/// \brief Service interface configuration data structure for Client_connector instances.
/// \details This type, which is used by Runtime::make_client_connector(), allows an optional member
/// configuration.
struct Service_interface_configuration final {
    /// \brief Constructor for default use-case.
    /// \param sif Service interface identification information.
    /// \param methods Methods of the service interface.
    /// \param events Events of the service interface.
    Service_interface_configuration(Service_interface sif, Num_of_methods num_of_methods,
                                    Num_of_events num_of_events);

    /// \brief Constructor without methods and events.
    /// \details Client_connectors which have no member configuration must use the provided
    /// Server_service_interface_configuration configuration.
    /// \param sif Service interface identification information.
    explicit Service_interface_configuration(Service_interface sif);

    Service_interface_configuration(Service_interface_configuration const&) = default;
    Service_interface_configuration(Service_interface_configuration&&) noexcept = default;

    ~Service_interface_configuration() noexcept = default;

    Service_interface_configuration& operator=(Service_interface_configuration const&) = delete;
    Service_interface_configuration& operator=(Service_interface_configuration&&) = delete;

    /// \brief Service interface identification information.
    Service_interface const interface;
    std::size_t num_methods{0U};
    std::size_t num_events{0U};
};

bool operator==(Service_interface_configuration const& lhs,
                Service_interface_configuration const& rhs);

bool operator<(Service_interface_configuration const& lhs,
               Service_interface_configuration const& rhs);

/// \brief Service interface configuration data structure for Server_connector instances.
/// \details This type, which is used by Runtime::make_server_connector(), enforces a member
/// configuration.
class Server_service_interface_configuration final {
    Service_interface_configuration m_configuration;

   public:
    /// \brief Constructor.
    /// \param sif Service interface identification information.
    /// \param methods Methods of the service interface.
    /// \param events Events of the service interface.
    Server_service_interface_configuration(Service_interface const& sif,
                                           Num_of_methods num_of_methods,
                                           Num_of_events num_of_events);

    Server_service_interface_configuration(Server_service_interface_configuration const& rhs);
    Server_service_interface_configuration(Server_service_interface_configuration&& rhs) noexcept;

    ~Server_service_interface_configuration() noexcept = default;

    Server_service_interface_configuration& operator=(
        Server_service_interface_configuration const&) = delete;
    Server_service_interface_configuration& operator=(Server_service_interface_configuration&&) =
        delete;

    // Service_interface_configuration
    /// \brief Retrieves the configuration by implicitly converting an instance to
    /// Service_interface_configuration.
    /// \return The stored configuration.
    operator Service_interface_configuration() const;

    static Server_service_interface_configuration invalid();

    std::size_t get_num_methods() const noexcept;
    std::size_t get_num_events() const noexcept;
    Service_interface const& get_interface() const noexcept;
};

}  // namespace score::socom

#endif  // SCORE_SOCOM_SERVICE_INTERFACE_CONFIGURATION_HPP
