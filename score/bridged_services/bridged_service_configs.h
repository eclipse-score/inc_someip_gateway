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

#ifndef SCORE_BRIDGED_SERVICES_BRIDGED_SERVICE_CONFIGS_H
#define SCORE_BRIDGED_SERVICES_BRIDGED_SERVICE_CONFIGS_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#include "score/config/mw_someip_config_generated.h"
#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"

namespace score::bridged_services {

/// \brief mw::com instance specifier of SomeipdService
inline constexpr char const* kSomeipd_service_specifier{"someipd/daemon"};

/// \brief The daemon the services are built for
enum class Daemon : std::uint8_t { gatewayd, someipd };

/// \brief Largest SOME/IP message of an event, header included
using Event_size_strategy =
    std::function<std::size_t(std::string_view service_type_name, std::string_view event_name)>;

/// \brief Bridged services between gatewayd and someipd, one per service type
/// \details gatewayd provides the types with local instances. It consumes the remote ones.
///          someipd takes the opposite role. Without `event_size` every size is 0. The binding
///          then uses the size from the peer.
gateway_ipc_binding::mw_com::Service_configs bridged_service_configs(
    mw_someip_config::Root const& config, Daemon daemon,
    Event_size_strategy const& event_size = {});

}  // namespace score::bridged_services

#endif  // SCORE_BRIDGED_SERVICES_BRIDGED_SERVICE_CONFIGS_H
