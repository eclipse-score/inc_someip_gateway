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

#ifndef SRC_GATEWAY_IPC_BINDING_TEST_TEST_FIXTURES
#define SRC_GATEWAY_IPC_BINDING_TEST_TEST_FIXTURES

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/socom/callback_mocks.hpp"
#include "score/socom/runtime.hpp"
#include "test_constants.hpp"
#include "util.hpp"

namespace score::gateway_ipc_binding {

struct Bidirectional_test_parameter {
    Direction direction;
};

class Gateway_ipc_binding_unconnected_integration_test : public ::testing::Test,
                                                         protected Test_constants {
   protected:
    socom::Runtime::Uptr runtime_server = score::socom::create_runtime();
    socom::Runtime::Uptr runtime_client = score::socom::create_runtime();

    socom::Service_state_change_callback_mock mock_service_state_change_cb;
    socom::Event_update_callback_mock mock_event_update_cb;
    socom::Event_payload_allocate_callback_mock mock_event_payload_allocate_cb;
    socom::Method_call_credentials_callback_mock mock_method_call_credentials_cb;
    socom::Event_subscription_change_callback_mock mock_event_subscription_change_cb;
    socom::Event_request_update_callback_mock mock_event_request_update_cb;
    socom::Method_call_payload_allocate_callback_mock mock_method_payload_allocate_cb;

    std::unique_ptr<Gateway_ipc_binding_server> server;
    std::unique_ptr<Gateway_ipc_binding_client> client;

    ~Gateway_ipc_binding_unconnected_integration_test() override {
        client.reset();
        server.reset();
    }

    mw_com::Service_configs mw_com_services(mw_com::Role const role,
                                            std::size_t const max_payload_size) const {
        return {{interface,
                 instance,
                 "bridged_ipc/bridged",
                 6401U,
                 1U,
                 role,
                 {{"event_a", mw_com::Mw_com_event_id{1U}, max_payload_size},
                  {"event_b", mw_com::Mw_com_event_id{2U}, max_payload_size}},
                 mw_com_sample_slots}};
    }

    std::unique_ptr<Gateway_ipc_binding_server> create_mw_com_server(
        socom::Runtime& runtime, std::string const& someipd_service_specifier,
        mw_com::Service_configs services) {
        return mw_com::create_server(
            runtime, someipd_service_specifier,
            [services = std::move(services)](
                score::cpp::span<std::byte const>,
                mw_com::Add_bridged_services const& add_bridged_services) {
                return add_bridged_services(services);
            });
    }

    std::unique_ptr<Gateway_ipc_binding_client> create_mw_com_client(
        socom::Runtime& runtime, std::string const& someipd_service_specifier,
        mw_com::Service_configs services) {
        return mw_com::create_client(runtime, someipd_service_specifier, std::move(services));
    }

    void create_mw_com_pair(Direction const direction, bool const start) {
        bool const server_provides = direction == Direction::Client_to_server;
        server = mw_com::create_server(
            *runtime_server, "someipd/daemon",
            [this, server_provides](score::cpp::span<std::byte const>,
                                    mw_com::Add_bridged_services const& add_bridged_services) {
                return add_bridged_services(mw_com_services(
                    server_provides ? mw_com::Role::provider : mw_com::Role::consumer,
                    server_provides ? 0U : mw_com_payload_size));
            });
        client = mw_com::create_client(
            *runtime_client, "someipd/daemon",
            mw_com_services(server_provides ? mw_com::Role::consumer : mw_com::Role::provider,
                            mw_com_payload_size));
        ASSERT_NE(server, nullptr);
        ASSERT_NE(client, nullptr);
        if (start) {
            start_and_wait_for_client_connection();
        }
    }

    void SetUp() override { create_mw_com_pair(Direction::Client_to_server, false); }

    void start_and_wait_for_client_connection() {
        auto start_result = server->start();
        ASSERT_TRUE(start_result);
        EXPECT_TRUE(wait_on_connection_state(*client, Connection_state::Connected,
                                             very_long_timeout));
    }
};

class Gateway_ipc_binding_integration_test
    : public Gateway_ipc_binding_unconnected_integration_test {
   protected:
    void SetUp() override {
        Gateway_ipc_binding_unconnected_integration_test::SetUp();
        start_and_wait_for_client_connection();
    }
};

inline std::string readable_test_names(
    testing::TestParamInfo<Bidirectional_test_parameter> const& param) {
    return param.param.direction == Direction::Client_to_server ? "Client_to_server"
                                                                : "Server_to_client";
}

template <typename BASE>
class Gateway_ipc_binding_bidirectional_test
    : public BASE,
      public ::testing::WithParamInterface<Bidirectional_test_parameter> {
   protected:
    void SetUp() override {
        constexpr bool is_connected_fixture =
            std::is_same_v<BASE, Gateway_ipc_binding_integration_test>;
        this->create_mw_com_pair(this->GetParam().direction, is_connected_fixture);
    }

    void TearDown() override {
        if (this->GetParam().direction == Direction::Server_to_client) {
            this->server.reset();
        }
    }

    socom::Runtime& get_client_runtime() {
        return this->GetParam().direction == Direction::Client_to_server ? *this->runtime_client
                                                                         : *this->runtime_server;
    }
    socom::Runtime& get_server_runtime() {
        return this->GetParam().direction == Direction::Client_to_server ? *this->runtime_server
                                                                         : *this->runtime_client;
    }

    std::size_t get_server_payload_size() const { return this->mw_com_payload_size; }
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_TEST_TEST_FIXTURES
