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

#ifndef IMPL_SOMEIPD_GATEWAY_SD_SETTINGS
#define IMPL_SOMEIPD_GATEWAY_SD_SETTINGS

#include <cstdint>
#include <string>
#include <string_view>

namespace score::someipd {

/// \brief Service-discovery timing and addresses from the gateway's vsomeip JSON.
/// \details The numbers stay in that file. The defaults match
///          tests/integration_test/vsomeip.json when the file is absent or a key is missing.
struct GatewaySdSettings {
    std::string multicast_address{"224.244.224.245"};
    std::uint16_t multicast_port{30490};
    /// Empty when the file does not name a unicast address.
    std::string unicast_address;
    /// SOME/IP-TP/UDP service port from the first "unreliable" entry. 0 means ephemeral.
    std::uint16_t service_port{0};
    int initial_delay_min_ms{10};
    int initial_delay_max_ms{100};
    int repetition_base_ms{200};
    /// vsomeip repetitions_max: how many repetition-phase offers to send. Not a duration.
    int repetitions{3};
    int ttl_seconds{3};
    int cyclic_offer_ms{2000};
};

/// \brief Defaults used when the gateway file is missing a key.
GatewaySdSettings gateway_sd_defaults();

/// \brief Reads the SD object and the top-level unicast address from @p json.
/// \details Missing keys keep gateway_sd_defaults().
GatewaySdSettings gateway_sd_settings_from_json(std::string_view json);

/// \brief Loads VSOMEIP_CONFIGURATION, or gateway_sd_defaults() when it is unset or unreadable.
GatewaySdSettings load_gateway_sd_settings();

}  // namespace score::someipd

#endif  // IMPL_SOMEIPD_GATEWAY_SD_SETTINGS
