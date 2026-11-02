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

#include <chrono>
#include <cstddef>
#include <vector>

#include "score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp"
#include "score/socom/service_interface_definition.hpp"
#include "util.hpp"

namespace score::gateway_ipc_binding {

inline constexpr std::chrono::seconds very_long_timeout{500};

inline const std::vector<std::byte> expected_payload{std::byte{1}, std::byte{2}, std::byte{3},
                                                     std::byte{4}};

using socom::Event_id;
using socom::Method_id;

struct Shared_memory_metadata {
    std::size_t slot_size;
    std::size_t slot_count;
};

class Test_constants {
   protected:
    socom::Service_interface_identifier const interface{
        "bridged_/test/ipc/BridgedService", socom::Literal_tag{}, {1, 0}};
    socom::Service_instance const instance{"1", socom::Literal_tag{}};
    socom::Server_service_interface_definition const socom_server_config{
        interface, socom::to_num_of_methods(0), socom::to_num_of_events(2)};

    Shared_memory_metadata const mw_com_metadata{8232U, 4U};

    Event_id const event_id{0};
};

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_TEST_TEST_CONSTANTS
