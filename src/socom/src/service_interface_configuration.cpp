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

#include <score/socom/service_interface_configuration.hpp>

#include <cassert>
#include <string_view>

namespace score::socom {

Num_of_events to_num_of_events(std::size_t const value) noexcept
{
    return static_cast<Num_of_events>(value);
}

Num_of_methods to_num_of_methods(std::size_t const value) noexcept
{
    return static_cast<Num_of_methods>(value);
}

Service_interface_configuration::Service_interface_configuration(
    Service_interface const sif, Num_of_methods const num_of_methods,
    Num_of_events const num_of_events)
    : interface{sif}
    , num_methods{static_cast<std::size_t>(num_of_methods)}
    , num_events{static_cast<std::size_t>(num_of_events)}
{
}

Service_interface_configuration::Service_interface_configuration(Service_interface const sif)
    : interface{sif}
{
}

bool operator==(Service_interface_configuration const& lhs,
                Service_interface_configuration const& rhs)
{
    return !(lhs < rhs) && !(rhs < lhs);
}

bool operator<(Service_interface_configuration const& lhs,
               Service_interface_configuration const& rhs)
{
    return std::tie(lhs.num_methods, lhs.num_events, lhs.interface) <
           std::tie(rhs.num_methods, rhs.num_events, rhs.interface);
}

Server_service_interface_configuration::Server_service_interface_configuration(
    Service_interface const& sif, Num_of_methods const num_of_methods,
    Num_of_events const num_of_events)
    : m_configuration{sif, num_of_methods, num_of_events}
{
}

Server_service_interface_configuration::Server_service_interface_configuration(
    Server_service_interface_configuration const& rhs) = default;

Server_service_interface_configuration::Server_service_interface_configuration(
    Server_service_interface_configuration&& rhs) noexcept
    : m_configuration{std::move(rhs.m_configuration)}
{
}

Server_service_interface_configuration::operator Service_interface_configuration() const
{
    return m_configuration;
}

Server_service_interface_configuration Server_service_interface_configuration::invalid()
{
    return Server_service_interface_configuration{
        Service_interface{std::string_view{""}, Service_interface::Version{0U, 0U}},
        Num_of_methods{}, Num_of_events{}};
}

std::size_t Server_service_interface_configuration::get_num_methods() const noexcept
{
    return m_configuration.num_methods;
}

std::size_t Server_service_interface_configuration::get_num_events() const noexcept
{
    return m_configuration.num_events;
}

Service_interface const& Server_service_interface_configuration::get_interface() const noexcept
{
    return m_configuration.interface;
}

} // namespace score::socom
