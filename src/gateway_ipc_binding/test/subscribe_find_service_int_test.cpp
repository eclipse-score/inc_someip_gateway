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
#include <thread>
#include <vector>

#include "gmock/gmock.h"
#include "test_constants.hpp"
#include "test_fixtures.hpp"
#include "util.hpp"

using testing::_;
using testing::Values;
using namespace std::chrono_literals;

namespace score::gateway_ipc_binding {

class Gateway_ipc_binding_subscribe_find_service_integration_test
    : public Gateway_ipc_binding_integration_test,
      public ::testing::WithParamInterface<std::vector<Find_service_elements>> {
   protected:
    std::vector<Find_service_elements> const find_service_elements = GetParam();
    std::size_t const num_clients = find_service_elements.size();

    std::vector<std::unique_ptr<socom::Runtime>> client_runtimes;
    std::vector<std::unique_ptr<Gateway_ipc_binding_client>> clients;
};

Service const service0{.service_id = {"fake_service_id"}, .version = {1, 2}};
Service const service1{.service_id = {"test"}, .version = {4, 2}};
Instance_id const instance_id{"fake_instance_id"};

Find_service_elements const find_service_elements0{{service0, instance_id}};
Find_service_elements const find_service_elements1{{service1, instance_id}};
Find_service_elements const find_service_elements2{{service0, instance_id},
                                                   {service1, instance_id}};

INSTANTIATE_TEST_SUITE_P(
    , Gateway_ipc_binding_subscribe_find_service_integration_test,
    Values(std::vector<Find_service_elements>{find_service_elements0},
           std::vector<Find_service_elements>{find_service_elements1},
           std::vector<Find_service_elements>{find_service_elements2},
           std::vector<Find_service_elements>{find_service_elements0, find_service_elements1},
           std::vector<Find_service_elements>{find_service_elements0, find_service_elements2},
           std::vector<Find_service_elements>{find_service_elements1, find_service_elements2},
           std::vector<Find_service_elements>{find_service_elements0, find_service_elements1,
                                              find_service_elements2}));

TEST_P(Gateway_ipc_binding_subscribe_find_service_integration_test,
       reports_connect_find_service_elements_on_connect_and_disconnect) {
    std::vector<std::promise<void>> event_subscription_change_promises(num_clients);

    // mocks need to be setup first to avoid data races
    for (std::size_t i = 0; i < num_clients; ++i) {
        EXPECT_CALL(mock_on_find_service_change_cb, Call(_, find_service_elements[i], true))
            .Times(1);

        EXPECT_CALL(mock_on_find_service_change_cb, Call(_, find_service_elements[i], false))
            .Times(1)
            .WillOnce([&event_subscription_change_promises, i](auto, auto const&, auto) {
                event_subscription_change_promises[i].set_value();
            });
    }

    for (std::size_t i = 0; i < num_clients; ++i) {
        Shared_memory_metadata const client_metadata_1 =
            make_metadata("/gw_client_shm_many_clients_tests_" + std::to_string(i), 256, 8);
        Shared_memory_manager_factory::Shared_memory_configuration const client_shm_config_1{
            {interface, {{instance, client_metadata_1}}}};

        auto client_runtime = socom::create_runtime();
        clients.push_back(
            create_ipc_client(*client_runtime, client_shm_config_1, find_service_elements[i]));
        client_runtimes.push_back(std::move(client_runtime));
    }

    for (auto& client : clients) {
        while (!client->is_connected()) {
            std::this_thread::sleep_for(1ms);
        }
    }

    // All find service elements are added, now reset the clients to trigger disconnects and
    // verify the callbacks are invoked for each
    clients.clear();

    // Wait for all clients to disconnect
    for (std::size_t i = 0; i < num_clients; ++i) {
        EXPECT_EQ(event_subscription_change_promises[i].get_future().wait_for(very_long_timeout),
                  std::future_status::ready)
            << "Did not receive disconnect callback for client " << i;
    }
}

}  // namespace score::gateway_ipc_binding
