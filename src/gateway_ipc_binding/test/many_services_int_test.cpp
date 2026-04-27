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

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <score/message_passing/unix_domain/unix_domain_client_factory.h>
#include <score/message_passing/unix_domain/unix_domain_server_factory.h>
#include <score/result/result.h>

#include <cstddef>
#include <future>
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_client.hpp>
#include <score/gateway_ipc_binding/gateway_ipc_binding_server.hpp>
#include <score/socom/callback_mocks.hpp>
#include <score/socom/client_connector.hpp>
#include <score/socom/client_connector_mock.hpp>
#include <score/socom/error.hpp>
#include <score/socom/runtime.hpp>
#include <score/socom/runtime_mock.hpp>
#include <score/socom/server_connector.hpp>
#include <score/socom/server_connector_mock.hpp>
#include <string>

#include "test_fixtures.hpp"
#include "util.hpp"

using testing::_;
using testing::Values;

namespace score::gateway_ipc_binding {

enum class Service_variant : std::uint8_t { alpha = 1 << 0, beta = 1 << 1, gamma = 1 << 2 };

Service_variant operator|(Service_variant a, Service_variant b) {
    return static_cast<Service_variant>(static_cast<std::uint8_t>(a) |
                                        static_cast<std::uint8_t>(b));
}

void expect_connection(Client_connector_with_callbacks& client, Service_variant const client_type,
                       Service_variant const configured_type,
                       socom::Server_service_interface_definition const& config) {
    if (static_cast<std::uint8_t>(client_type) & static_cast<std::uint8_t>(configured_type)) {
        client.expect_client_connected(config);
    } else {
        EXPECT_CALL(client.mock_service_state_change_cb, Call(_, _, _)).Times(0);
        client.client_connected_promise.set_value();
    }
}

std::unique_ptr<Server_connector_with_callbacks> create_server_connector(
    socom::Runtime& server_runtime, Service_variant const server_type,
    Service_variant const configured_type, socom::Server_service_interface_definition const& config,
    socom::Service_instance const& instance) {
    if (static_cast<std::uint8_t>(server_type) & static_cast<std::uint8_t>(configured_type)) {
        return std::make_unique<Server_connector_with_callbacks>(server_runtime, config, instance);
    }
    return nullptr;
}

void subscribe_event(Client_connector_with_callbacks& client, Service_variant const client_type,
                     Service_variant const configured_type, Server_connector_with_callbacks& server,
                     Event_id const& event_id) {
    if (static_cast<std::uint8_t>(client_type) & static_cast<std::uint8_t>(configured_type)) {
        client.subscribe_event(server.mock_event_subscription_change_cb, event_id);
    }
}

void send_event_update(Server_connector_with_callbacks& server, Service_variant const client_type,
                       Service_variant const configured_type, Event_id const& event_id,
                       Client_connector_with_callbacks& client) {
    if (!(static_cast<std::uint8_t>(client_type) & static_cast<std::uint8_t>(configured_type))) {
        return;
    }
    auto payload = create_payload(*server.connector, event_id, expected_payload);

    std::promise<void> event_received_promise;
    EXPECT_CALL(client.mock_event_update_cb, Call(_, event_id, _))
        .Times(1)
        .WillOnce(
            [&event_received_promise](auto&, auto, auto) { event_received_promise.set_value(); });

    auto const update_result = server.connector->update_event(event_id, std::move(payload));
    ASSERT_TRUE(update_result);
    EXPECT_EQ(event_received_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

class Gateway_ipc_binding_many_services_integration_test
    : public Gateway_ipc_binding_unconnected_integration_test {
   protected:
    socom::Service_interface_identifier const service_interface_alpha{
        std::string{"com.test.service.alpha"}, {1, 0}};
    socom::Service_interface_identifier const service_interface_beta{
        std::string{"com.test.service.beta"}, {1, 0}};
    socom::Service_interface_identifier const service_interface_gamma{
        std::string{"com.test.service.gamma"}, {1, 0}};
    socom::Service_instance const instance{"instance1", socom::Literal_tag{}};

    socom::Server_service_interface_definition const server_config_alpha{
        service_interface_alpha, socom::to_num_of_methods(1), socom::to_num_of_events(1)};
    socom::Server_service_interface_definition const server_config_beta{
        service_interface_beta, socom::to_num_of_methods(1), socom::to_num_of_events(2)};
    socom::Server_service_interface_definition const server_config_gamma{
        service_interface_gamma, socom::to_num_of_methods(2), socom::to_num_of_events(3)};

    Shared_memory_metadata const client_metadata_alpha =
        make_metadata("/gw_client_shm_many_services_alpha", 256, 8);
    Shared_memory_metadata const client_metadata_beta =
        make_metadata("/gw_client_shm_many_services_beta", 320, 8);
    Shared_memory_metadata const client_metadata_gamma =
        make_metadata("/gw_client_shm_many_services_gamma", 384, 8);

    Shared_memory_metadata const server_metadata_alpha =
        make_metadata("/gw_server_shm_many_services_alpha", 512, 4);
    Shared_memory_metadata const server_metadata_beta =
        make_metadata("/gw_server_shm_many_services_beta", 640, 4);
    Shared_memory_metadata const server_metadata_gamma =
        make_metadata("/gw_server_shm_many_services_gamma", 768, 4);

    Shared_memory_manager_factory::Shared_memory_configuration const server_shm_config{
        {service_interface_alpha, {{instance, server_metadata_alpha}}},
        {service_interface_beta, {{instance, server_metadata_beta}}},
        {service_interface_gamma, {{instance, server_metadata_gamma}}}};

    Shared_memory_manager_factory::Shared_memory_configuration const client_shm_config{
        {service_interface_alpha, {{instance, client_metadata_alpha}}},
        {service_interface_beta, {{instance, client_metadata_beta}}},
        {service_interface_gamma, {{instance, client_metadata_gamma}}}};

    Event_id const beta_event_id{1};

    Gateway_ipc_binding_many_services_integration_test() {
        client = nullptr;
        server = nullptr;
        client = create_ipc_client(*runtime_client, client_shm_config);
        server = create_ipc_server(*runtime_server, server_shm_config);

        start_and_wait_for_client_connection();
    }
};

using Service_variant_pair = std::tuple<Service_variant, Service_variant>;

Service_variant_pair tsv(Service_variant a, Service_variant b) { return std::make_tuple(a, b); }

class Gateway_ipc_binding_many_services_param_integration_test
    : public Gateway_ipc_binding_many_services_integration_test,
      public testing::WithParamInterface<Service_variant_pair> {
   protected:
    Service_variant const client_type = std::get<0>(GetParam());
    Service_variant const server_type = std::get<1>(GetParam());
};

INSTANTIATE_TEST_SUITE_P(
    , Gateway_ipc_binding_many_services_param_integration_test,
    Values(tsv(Service_variant::alpha, Service_variant::alpha),
           tsv(Service_variant::beta, Service_variant::beta),
           tsv(Service_variant::gamma, Service_variant::gamma),
           tsv(Service_variant::alpha | Service_variant::beta,
               Service_variant::alpha | Service_variant::beta),
           tsv(Service_variant::alpha | Service_variant::gamma,
               Service_variant::alpha | Service_variant::gamma),
           tsv(Service_variant::beta | Service_variant::gamma,
               Service_variant::beta | Service_variant::gamma),
           tsv(Service_variant::alpha | Service_variant::beta | Service_variant::gamma,
               Service_variant::alpha | Service_variant::beta | Service_variant::gamma)));

TEST_P(Gateway_ipc_binding_many_services_param_integration_test, clients_connect_to_service) {
    Client_connector_with_callbacks alpha_observer;
    Client_connector_with_callbacks beta_observer;
    Client_connector_with_callbacks gamma_observer;

    expect_connection(alpha_observer, client_type, Service_variant::alpha, server_config_alpha);
    expect_connection(beta_observer, client_type, Service_variant::beta, server_config_beta);
    expect_connection(gamma_observer, client_type, Service_variant::gamma, server_config_gamma);

    alpha_observer.create_connector(*runtime_client, server_config_alpha, instance);
    beta_observer.create_connector(*runtime_client, server_config_beta, instance);
    gamma_observer.create_connector(*runtime_client, server_config_gamma, instance);

    auto alpha_server_connector = create_server_connector(
        *runtime_server, server_type, Service_variant::alpha, server_config_alpha, instance);
    auto beta_server_connector = create_server_connector(
        *runtime_server, server_type, Service_variant::beta, server_config_beta, instance);
    auto gamma_server_connector = create_server_connector(
        *runtime_server, server_type, Service_variant::gamma, server_config_gamma, instance);

    EXPECT_EQ(alpha_observer.client_connected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
    EXPECT_EQ(beta_observer.client_connected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
    EXPECT_EQ(gamma_observer.client_connected_promise.get_future().wait_for(very_long_timeout),
              std::future_status::ready);
}

TEST_P(Gateway_ipc_binding_many_services_param_integration_test,
       server_sends_event_update_to_subscribed_clients) {
    Server_connector_with_callbacks alpha_server_connector(*runtime_server, server_config_alpha,
                                                           instance);
    Server_connector_with_callbacks beta_server_connector(*runtime_server, server_config_beta,
                                                          instance);
    Server_connector_with_callbacks gamma_server_connector(*runtime_server, server_config_gamma,
                                                           instance);

    Client_connector_with_callbacks alpha_observer(*runtime_client, server_config_alpha, instance);
    Client_connector_with_callbacks beta_observer(*runtime_client, server_config_beta, instance);
    Client_connector_with_callbacks gamma_observer(*runtime_client, server_config_gamma, instance);

    subscribe_event(alpha_observer, client_type, Service_variant::alpha, alpha_server_connector,
                    event_id);
    subscribe_event(beta_observer, client_type, Service_variant::beta, beta_server_connector,
                    event_id);
    subscribe_event(gamma_observer, client_type, Service_variant::gamma, gamma_server_connector,
                    event_id);

    send_event_update(alpha_server_connector, server_type, Service_variant::alpha, event_id,
                      alpha_observer);
    send_event_update(beta_server_connector, server_type, Service_variant::beta, event_id,
                      beta_observer);
    send_event_update(gamma_server_connector, server_type, Service_variant::gamma, event_id,
                      gamma_observer);
}

}  // namespace score::gateway_ipc_binding
