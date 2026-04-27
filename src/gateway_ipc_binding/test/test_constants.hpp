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

#ifndef SRC_GATEWAY_IPC_BINDING_TEST_TEST_CONSTANTS
#define SRC_GATEWAY_IPC_BINDING_TEST_TEST_CONSTANTS

#include <score/message_passing/service_protocol_config.h>
#include <score/message_passing/unix_domain/unix_domain_client_factory.h>
#include <score/message_passing/unix_domain/unix_domain_server_factory.h>

#include <chrono>
#include <cstddef>
#include <score/gateway_ipc_binding/gateway_ipc_binding.hpp>
#include <score/socom/service_interface_definition.hpp>
#include <string>

#include "util.hpp"

using testing::_;
using testing::AtMost;
using testing::Values;
using namespace std::chrono_literals;

namespace score::gateway_ipc_binding {

// When everything works as expected we should never reach a fraction of this timeout
std::chrono::seconds const very_long_timeout{500};

std::vector<std::byte> const expected_payload{std::byte{1}, std::byte{2}, std::byte{3},
                                              std::byte{4}};

class Test_constants {
   protected:
    static constexpr std::size_t k_max_message_size = 4096;

    std::string const service_name = make_service_name();

    score::message_passing::ServiceProtocolConfig const protocol_config{
        service_name, k_max_message_size, k_max_message_size, k_max_message_size};
    score::message_passing::IServerFactory::ServerConfig const server_config{10, 10, 10};
    score::message_passing::IClientFactory::ClientConfig const client_config{10, 10, false, false,
                                                                             false};

    score::socom::Service_interface_identifier const interface{
        "com.test.service", socom::Literal_tag{}, {1, 0}};
    score::socom::Service_instance const instance{"instance1", socom::Literal_tag{}};
    score::socom::Service_interface_definition const socom_client_config{interface};
    score::socom::Server_service_interface_definition const socom_server_config{
        interface, score::socom::to_num_of_methods(1), score::socom::to_num_of_events(1)};

    Shared_memory_metadata const client_metadata = make_metadata("/gw_client_shm", 256, 8);
    Shared_memory_metadata const server_metadata = make_metadata("/gw_server_shm", 512, 4);

    Shared_memory_manager_factory::Shared_memory_configuration const server_shm_config{
        {interface, {{instance, server_metadata}}}};

    Shared_memory_manager_factory::Shared_memory_configuration const client_shm_config{
        {interface, {{instance, client_metadata}}}};

    Event_id const event_id{0};
    Method_id const method_id{0};
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_TEST_TEST_CONSTANTS
