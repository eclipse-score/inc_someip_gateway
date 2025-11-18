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

#include <score/gateway/service_configuration.hpp>

using score::socom::Server_service_interface_configuration;
using score::socom::Service_instance;
using score::socom::Service_interface;
using score::socom::to_num_of_events;
using score::socom::to_num_of_methods;

namespace score::gateway {

Server_service_interface_configuration const& get_interface_configuration()
{
    static Server_service_interface_configuration config{
        Service_interface{"example.interface", {0, 0}}, to_num_of_methods(0), to_num_of_events(1)};
    return config;
}

Service_instance const& get_instance()
{
    static Service_instance const instance{"instance1"};
    return instance;
}
} // namespace score::gateway
