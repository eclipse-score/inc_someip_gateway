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

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <future>
#include <string>
#include <utility>
#include <vector>

#include "score/socom/runtime.hpp"
#include "test_fixtures.hpp"

namespace score::gateway_ipc_binding {

using testing::Values;

namespace {
enum class Service_variant : std::uint8_t { alpha = 1U, beta = 2U, gamma = 4U };

Service_variant operator|(Service_variant lhs, Service_variant rhs) {
    return static_cast<Service_variant>(static_cast<std::uint8_t>(lhs) |
                                        static_cast<std::uint8_t>(rhs));
}

bool includes_service(Service_variant const selection, Service_variant const service) {
    return (static_cast<std::uint8_t>(selection) & static_cast<std::uint8_t>(service)) != 0U;
}

std::string many_services_test_name(testing::TestParamInfo<Service_variant> const& param) {
    return "Services_" + std::to_string(static_cast<std::uint8_t>(param.param));
}
}  // namespace

class Gateway_ipc_binding_many_services_integration_test
    : public Gateway_ipc_binding_unconnected_integration_test {
    protected:
    socom::Service_interface_identifier const service_interface_alpha{
        std::string{"bridged_com.test.service.alpha"}, {1, 0}};
    socom::Service_interface_identifier const service_interface_beta{
        std::string{"bridged_com.test.service.beta"}, {1, 0}};
    socom::Service_interface_identifier const service_interface_gamma{
        std::string{"bridged_com.test.service.gamma"}, {1, 0}};
    socom::Service_instance const instance{"1", socom::Literal_tag{}};

    socom::Server_service_interface_definition const server_config_alpha{
        service_interface_alpha, socom::to_num_of_methods(0), socom::to_num_of_events(1)};
    socom::Server_service_interface_definition const server_config_beta{
        service_interface_beta, socom::to_num_of_methods(0), socom::to_num_of_events(2)};
    socom::Server_service_interface_definition const server_config_gamma{
        service_interface_gamma, socom::to_num_of_methods(0), socom::to_num_of_events(3)};

    mw_com::Service_configs make_mw_com_services(mw_com::Role const role) const {
        mw_com::Service_configs services;
        auto add_service = [this, &services, role](auto const& interface, std::size_t payload_size,
                                                   std::string instance_specifier,
                                                   std::uint16_t service_id,
                                                   std::uint16_t event_count) {
            std::vector<mw_com::Event_config> events;
            events.reserve(event_count);
            for (std::uint16_t event_index = 0U; event_index < event_count; ++event_index) {
                events.push_back({"event_" + std::to_string(event_index),
                                  static_cast<mw_com::Mw_com_event_id>(event_index + 1U),
                                  payload_size});
            }
            services.push_back({interface, instance, std::move(instance_specifier), service_id, 1U,
                                role, std::move(events), mw_com_sample_slots});
        };

        add_service(service_interface_alpha, 512U, "bridged_ipc/many_services_alpha", 6403U, 1U);
        add_service(service_interface_beta, 640U, "bridged_ipc/many_services_beta", 6404U, 2U);
        add_service(service_interface_gamma, 768U, "bridged_ipc/many_services_gamma", 6405U, 3U);
        return services;
    }
};

class Gateway_ipc_binding_many_services_integration_test_fixture
      : public Gateway_ipc_binding_many_services_integration_test,
        public testing::WithParamInterface<Service_variant> {
    protected:
      void SetUp() override {
          client = create_mw_com_client(*runtime_client, "someipd/daemon_many_services",
                                        make_mw_com_services(mw_com::Role::consumer));
        server = create_mw_com_server(*runtime_server, "someipd/daemon_many_services",
                                      make_mw_com_services(mw_com::Role::provider));

        ASSERT_NE(client, nullptr);
        ASSERT_NE(server, nullptr);
        auto const start_result = server->start();
        ASSERT_TRUE(start_result);
        EXPECT_TRUE(
            wait_on_connection_state(*client, Connection_state::Connected, very_long_timeout));
    }
};

INSTANTIATE_TEST_SUITE_P(
    , Gateway_ipc_binding_many_services_integration_test_fixture,
    Values(Service_variant::alpha, Service_variant::beta, Service_variant::gamma,
           Service_variant::alpha | Service_variant::beta,
           Service_variant::alpha | Service_variant::gamma,
           Service_variant::beta | Service_variant::gamma,
           Service_variant::alpha | Service_variant::beta | Service_variant::gamma),
    many_services_test_name);

TEST_P(Gateway_ipc_binding_many_services_integration_test_fixture,
       clients_connect_to_selected_services) {
    Client_connector_with_callbacks alpha_observer;
    Client_connector_with_callbacks beta_observer;
    Client_connector_with_callbacks gamma_observer;

    auto connect_selected_service =
        [this](Client_connector_with_callbacks& observer, Service_variant const service,
               socom::Server_service_interface_definition const& config) {
        if (!includes_service(GetParam(), service)) {
            return;
        }
        observer.expect_client_connected(config);
        observer.create_connector(*runtime_client, config, instance);
        EXPECT_EQ(observer.client_connected_promise.get_future().wait_for(very_long_timeout),
                  std::future_status::ready);
    };

    connect_selected_service(alpha_observer, Service_variant::alpha, server_config_alpha);
    connect_selected_service(beta_observer, Service_variant::beta, server_config_beta);
    connect_selected_service(gamma_observer, Service_variant::gamma, server_config_gamma);
}

}  // namespace score::gateway_ipc_binding
