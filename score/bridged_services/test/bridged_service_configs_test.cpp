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

#include "score/bridged_services/bridged_service_configs.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

#include "score/config/mw_someip_config_generated.h"
#include "score/someip/constants.h"

namespace score::bridged_services {
namespace {

namespace mw_com = gateway_ipc_binding::mw_com;

/// \brief Path of bridged_services_test_config.bin, the test argument
char const* g_config_path{nullptr};

/// \brief Gateway config from bridged_services_test_config.json
class Bridged_service_configs_test : public ::testing::Test {
   protected:
    void SetUp() override {
        std::ifstream file{g_config_path, std::ios::binary};
        m_buffer.assign(std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{});
        ASSERT_FALSE(m_buffer.empty()) << "Cannot read " << g_config_path;
    }

    mw_someip_config::Root const& gateway_config() const {
        return *mw_someip_config::GetRoot(m_buffer.data());
    }

    std::vector<char> m_buffer;
};

/// \brief Fake sizes: 100 per letter of the service type name, 1 per letter of the event name
std::size_t letters(std::string_view const service_type_name, std::string_view const event_name) {
    return (service_type_name.size() * 100U) + event_name.size();
}

TEST_F(Bridged_service_configs_test, gatewayd_provides_local_and_consumes_remote_service_types) {
    auto const services = bridged_service_configs(gateway_config(), Daemon::gatewayd);

    ASSERT_EQ(services.size(), 2U);
    auto const& provided = services.front();
    EXPECT_EQ(provided.interface.id.string_view(), "app_service");
    EXPECT_EQ(provided.instance.id.string_view(), "app_service");
    EXPECT_EQ(provided.instance_specifier, "bridged_ipc/app_service");
    EXPECT_EQ(provided.service_id, 0x1234U);
    EXPECT_EQ(provided.instance_id, 7U);
    EXPECT_EQ(provided.role, mw_com::Role::provider);
    EXPECT_EQ(provided.max_sample_count, someip::kMaxSampleCount);

    auto const& consumed = services.back();
    EXPECT_EQ(consumed.service_id, 0x4321U);
    EXPECT_EQ(consumed.role, mw_com::Role::consumer);
    EXPECT_EQ(consumed.interface.version.major, 2U);
    EXPECT_EQ(consumed.interface.version.minor, 3U);
}

TEST_F(Bridged_service_configs_test, someipd_takes_the_other_role_of_each_service_type) {
    auto const services = bridged_service_configs(gateway_config(), Daemon::someipd);

    ASSERT_EQ(services.size(), 2U);
    EXPECT_EQ(services.front().role, mw_com::Role::consumer);
    EXPECT_EQ(services.back().role, mw_com::Role::provider);
}

TEST_F(Bridged_service_configs_test, an_event_has_its_someip_id_and_the_size_of_the_strategy) {
    auto const services = bridged_service_configs(gateway_config(), Daemon::gatewayd, letters);

    ASSERT_EQ(services.back().events.size(), 1U);
    auto const& event = services.back().events.front();
    EXPECT_EQ(event.name, "info");
    EXPECT_EQ(event.id, mw_com::Mw_com_event_id{0x8001U});
    EXPECT_EQ(event.max_payload_size, 1104U);
}

TEST_F(Bridged_service_configs_test, without_a_strategy_every_size_comes_from_the_peer) {
    auto const services = bridged_service_configs(gateway_config(), Daemon::someipd);

    ASSERT_EQ(services.size(), 2U);
    ASSERT_EQ(services.front().events.size(), 2U);
    EXPECT_EQ(services.front().events.front().max_payload_size, 0U);
    EXPECT_EQ(services.front().events.back().max_payload_size, 0U);
    ASSERT_EQ(services.back().events.size(), 1U);
    EXPECT_EQ(services.back().events.front().max_payload_size, 0U);
}

}  // namespace
}  // namespace score::bridged_services

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <bridged_services_test_config.bin>\n";
        return EXIT_FAILURE;
    }
    score::bridged_services::g_config_path = argv[1];
    return RUN_ALL_TESTS();
}
