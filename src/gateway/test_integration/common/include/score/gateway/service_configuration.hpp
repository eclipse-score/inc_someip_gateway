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

#ifndef SCORE_MW_COM_IPC_BRIDGE_SERVICE_CONFIGURATION_H
#define SCORE_MW_COM_IPC_BRIDGE_SERVICE_CONFIGURATION_H

#include <score/socom/service_interface_configuration.hpp>

namespace score::gateway {

/// Interface configuration used in tests
score::socom::Server_service_interface_configuration const& get_interface_configuration();

/// Instance used in tests
score::socom::Service_instance const& get_instance();

} // namespace score::gateway

#endif
