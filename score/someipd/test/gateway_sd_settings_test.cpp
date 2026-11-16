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
    EXPECT_TRUE(settings.services.empty());
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
    EXPECT_TRUE(settings.services.empty());
}

TEST(GatewaySdSettingsTest, ReadsFieldFlagReliablePortAndMulticastThreshold) {
    constexpr std::string_view json = R"({
        "unicast": "169.254.158.190",
        "services": [
            {
                "service": "0x1234",
                "instance": "0x5678",
                "reliable": "30510",
                "unreliable": "30509",
                "eventgroups": [
                    {
                        "eventgroup": "0x0006",
                        "events": ["0x800B"],
                        "multicast": {"address": "239.0.0.1", "port": "40490"},
                        "threshold": "1"
                    },
                    {
                        "eventgroup": "0x0002",
                        "events": ["0x8001"]
                    }
                ],
                "events": [
                    {"event": "0x8001", "is_field": "true"},
                    {"event": "0x800B", "is_field": "false", "is_reliable": "false"}
                ]
            },
            {
                "service": "0x1234",
                "instance": "0x5680",
                "reliable": "30511",
                "events": [
                    {"event": "0x8002", "is_field": "false", "is_reliable": "true"}
                ]
            }
        ],
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

    EXPECT_EQ(settings.service_port, 30509);
    EXPECT_EQ(settings.multicast_address, "224.244.224.245");
    EXPECT_EQ(settings.multicast_port, 30490);
    EXPECT_EQ(settings.ttl_seconds, 30);
    EXPECT_EQ(settings.cyclic_offer_ms, 2000);
    ASSERT_EQ(settings.services.size(), 2U);

    auto const& udp_and_tcp = settings.services[0];
    EXPECT_EQ(udp_and_tcp.service_id, 0x1234);
    EXPECT_EQ(udp_and_tcp.instance_id, 0x5678);
    EXPECT_EQ(udp_and_tcp.unreliable_port, 30509);
    EXPECT_EQ(udp_and_tcp.reliable_port, 30510);
    ASSERT_EQ(udp_and_tcp.events.size(), 2U);
    EXPECT_EQ(udp_and_tcp.events[0].event_id, 0x8001);
    EXPECT_TRUE(udp_and_tcp.events[0].has_is_field);
    EXPECT_TRUE(udp_and_tcp.events[0].is_field);
    EXPECT_FALSE(udp_and_tcp.events[0].has_is_reliable);
    EXPECT_EQ(udp_and_tcp.events[1].event_id, 0x800B);
    EXPECT_TRUE(udp_and_tcp.events[1].has_is_field);
    EXPECT_FALSE(udp_and_tcp.events[1].is_field);
    EXPECT_TRUE(udp_and_tcp.events[1].has_is_reliable);
    EXPECT_FALSE(udp_and_tcp.events[1].is_reliable);
    ASSERT_EQ(udp_and_tcp.eventgroups.size(), 2U);
    EXPECT_EQ(udp_and_tcp.eventgroups[0].eventgroup_id, 0x0006);
    EXPECT_EQ(udp_and_tcp.eventgroups[0].multicast_address, "239.0.0.1");
    EXPECT_EQ(udp_and_tcp.eventgroups[0].multicast_port, 40490);
    EXPECT_TRUE(udp_and_tcp.eventgroups[0].has_threshold);
    EXPECT_EQ(udp_and_tcp.eventgroups[0].threshold, 1);
    EXPECT_EQ(udp_and_tcp.eventgroups[1].eventgroup_id, 0x0002);
    EXPECT_TRUE(udp_and_tcp.eventgroups[1].multicast_address.empty());
    EXPECT_EQ(udp_and_tcp.eventgroups[1].multicast_port, 0);
    EXPECT_FALSE(udp_and_tcp.eventgroups[1].has_threshold);

    auto const& tcp_only = settings.services[1];
    EXPECT_EQ(tcp_only.service_id, 0x1234);
    EXPECT_EQ(tcp_only.instance_id, 0x5680);
    EXPECT_EQ(tcp_only.unreliable_port, 0);
    EXPECT_EQ(tcp_only.reliable_port, 30511);
    ASSERT_EQ(tcp_only.events.size(), 1U);
    EXPECT_EQ(tcp_only.events[0].event_id, 0x8002);
    EXPECT_TRUE(tcp_only.events[0].has_is_field);
    EXPECT_FALSE(tcp_only.events[0].is_field);
    EXPECT_TRUE(tcp_only.events[0].has_is_reliable);
    EXPECT_TRUE(tcp_only.events[0].is_reliable);
}

}  // namespace
}  // namespace score::someipd
