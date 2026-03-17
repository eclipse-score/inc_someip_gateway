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

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "src/someipd/someipd_types.h"

namespace score::someip_gateway::someipd {

/// A single event/eventgroup pair within a SOME/IP service.
struct ServiceEventConfig {
    EventId event_id;
    EventGroupId eventgroup_id;
};

/// Transport-level configuration for one SOME/IP service instance.
struct ServiceConfig {
    ServiceId service_id;
    InstanceId instance_id;
    /// UDP port for unreliable (UDP) transport. 0 means no external transport configured.
    std::uint16_t unreliable_port{0};
    std::vector<ServiceEventConfig> events;
};

/// Full someipd service configuration.
struct SomeipDConfig {
    /// Services that someipd offers to the SOME/IP network (outbound: IPC → network).
    std::vector<ServiceConfig> offered_services;
    /// Services that someipd subscribes to from the SOME/IP network (inbound: network → IPC).
    std::vector<ServiceConfig> subscribed_services;
};

/// @brief Parse the someipd service configuration from a JSON file.
/// @param config_path Absolute or relative path to the someipd config JSON file.
/// @throws std::runtime_error if the file cannot be opened or parsed.
SomeipDConfig ReadSomeipDConfig(const std::string& config_path);

}  // namespace score::someip_gateway::someipd
