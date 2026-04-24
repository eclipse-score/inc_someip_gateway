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
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>

#include "test_constants.hpp"
#include "test_fixtures.hpp"
#include "util.hpp"

using testing::_;
using testing::Combine;
using testing::Values;
using namespace std::chrono_literals;

namespace score::gateway_ipc_binding {

class Gateway_ipc_binding_bidirectional_many_events_integration_test
    : public Gateway_ipc_binding_integration_test,
      public ::testing::WithParamInterface<std::tuple<Direction, std::vector<socom::Event_id>>> {
   protected:
    socom::Runtime& get_client_runtime() {
        return std::get<0>(GetParam()) == Direction::Client_to_server ? *runtime_client
                                                                      : *runtime_server;
    }
    socom::Runtime& get_server_runtime() {
        return std::get<0>(GetParam()) == Direction::Client_to_server ? *runtime_server
                                                                      : *runtime_client;
    }

    Shared_memory_metadata const& get_server_metadata() {
        return std::get<0>(GetParam()) == Direction::Client_to_server ? server_metadata
                                                                      : client_metadata;
    }

    score::socom::Server_service_interface_definition const socom_server_config_many_events{
        interface, score::socom::to_num_of_methods(1), score::socom::to_num_of_events(10)};

    Server_connector_with_callbacks server{get_server_runtime(), socom_server_config_many_events,
                                           instance};
    Client_connector_with_callbacks client{get_client_runtime(), socom_server_config_many_events,
                                           instance};
    std::vector<socom::Event_id> const& event_ids = std::get<1>(GetParam());
};

INSTANTIATE_TEST_SUITE_P(
    , Gateway_ipc_binding_bidirectional_many_events_integration_test,
    Combine(Values(Direction::Client_to_server, Direction::Server_to_client),
            Values(std::vector<socom::Event_id>{0}, std::vector<socom::Event_id>{1},
                   std::vector<socom::Event_id>{2}, std::vector<socom::Event_id>{3},
                   std::vector<socom::Event_id>{4}, std::vector<socom::Event_id>{5},
                   std::vector<socom::Event_id>{6}, std::vector<socom::Event_id>{7},
                   std::vector<socom::Event_id>{8}, std::vector<socom::Event_id>{9},
                   std::vector<socom::Event_id>{0, 2, 4, 6, 8},
                   std::vector<socom::Event_id>{1, 3, 5, 7, 9},
                   std::vector<socom::Event_id>{0, 1, 2, 3, 4},
                   std::vector<socom::Event_id>{5, 6, 7, 8, 9},
                   std::vector<socom::Event_id>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9})));

TEST_P(Gateway_ipc_binding_bidirectional_many_events_integration_test,
       server_sends_update_for_some_events) {
    for (socom::Event_id current_event_id : event_ids) {
        client.subscribe_event(server.mock_event_subscription_change_cb, current_event_id);
        auto payload_handle = create_payload(*server.connector, current_event_id, expected_payload);
        payload_handle->wdata()[0] = std::byte{static_cast<std::uint8_t>(
            current_event_id)};  // differentiate payloads of different events

        std::promise<socom::Payload::Uptr> event_update_received_promise;
        EXPECT_CALL(client.mock_event_update_cb, Call(_, current_event_id, _))
            .Times(1)
            .WillOnce([&event_update_received_promise](auto&, auto, auto payload) {
                event_update_received_promise.set_value(std::move(payload));
            });
        auto update_result =
            server.connector->update_event(current_event_id, std::move(payload_handle));
        ASSERT_TRUE(update_result);

        auto payload_future = event_update_received_promise.get_future();
        ASSERT_EQ(payload_future.wait_for(very_long_timeout), std::future_status::ready);

        auto received_payload = payload_future.get();
        ASSERT_TRUE(received_payload);
        EXPECT_EQ(received_payload->data().size(), get_server_metadata().slot_size);
        EXPECT_EQ(received_payload->data()[0],
                  std::byte{static_cast<std::uint8_t>(current_event_id)});
        EXPECT_EQ(received_payload->data()[1], expected_payload[1]);
        EXPECT_EQ(received_payload->data()[2], expected_payload[2]);
        EXPECT_EQ(received_payload->data()[3], expected_payload[3]);
    }
}

}  // namespace score::gateway_ipc_binding
