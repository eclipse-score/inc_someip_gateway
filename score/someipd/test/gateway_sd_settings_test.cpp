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

#include "score/someipd/impl/gateway_sd_settings.h"

#include <gtest/gtest.h>

#include <string_view>

namespace score::someipd {
namespace {

TEST(GatewaySdSettingsTest, EmptyJsonKeepsIntegrationDefaults) {
    auto const settings = gateway_sd_settings_from_json("");
    auto const defaults = gateway_sd_defaults();

    EXPECT_EQ(settings.multicast_address, "224.244.224.245");
    EXPECT_EQ(settings.multicast_port, 30490);
    EXPECT_EQ(settings.initial_delay_min_ms, 10);
    EXPECT_EQ(settings.initial_delay_max_ms, 100);
    EXPECT_EQ(settings.repetition_base_ms, 200);
    EXPECT_EQ(settings.repetitions, 3);
    EXPECT_EQ(settings.ttl_seconds, 3);
    EXPECT_EQ(settings.cyclic_offer_ms, 2000);
    EXPECT_EQ(settings.unicast_address, defaults.unicast_address);
    EXPECT_TRUE(settings.unicast_address.empty());
    EXPECT_EQ(settings.service_port, 0);
}

TEST(GatewaySdSettingsTest, ReadsUnicastAndServiceDiscoveryObject) {
    constexpr std::string_view json = R"({
        "unicast": "169.254.158.190",
        "services": [{"unreliable": "30509"}],
        "service-discovery": {
            "multicast": "224.244.224.245",
            "port": "30490",
            "initial_delay_min": "10",
            "initial_delay_max": "100",
            "repetitions_base_delay": "200",
            "repetitions_max": "3",
            "ttl": "30",
            "cyclic_offer_delay": "2000"
        }
    })";

    auto const settings = gateway_sd_settings_from_json(json);

    EXPECT_EQ(settings.unicast_address, "169.254.158.190");
    EXPECT_EQ(settings.service_port, 30509);
    EXPECT_EQ(settings.multicast_address, "224.244.224.245");
    EXPECT_EQ(settings.multicast_port, 30490);
    EXPECT_EQ(settings.initial_delay_min_ms, 10);
    EXPECT_EQ(settings.initial_delay_max_ms, 100);
    EXPECT_EQ(settings.repetition_base_ms, 200);
    EXPECT_EQ(settings.repetitions, 3);
    EXPECT_EQ(settings.ttl_seconds, 30);
    EXPECT_EQ(settings.cyclic_offer_ms, 2000);
}

}  // namespace
}  // namespace score::someipd
