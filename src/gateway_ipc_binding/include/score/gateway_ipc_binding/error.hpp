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

#ifndef SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_ERROR
#define SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_ERROR

#include <string_view>

#include "score/result/error.h"
#include "score/result/error_code.h"

namespace score::gateway_ipc_binding {

/// \brief Error conditions for Shared_memory_slot_manager operations
enum class Shared_memory_manager_error : score::result::ErrorCode {
    /// Slot size is zero
    logic_error_invalid_slot_size,
    /// Number of slots is zero
    logic_error_invalid_slot_count,
    /// Failed to allocate shared memory
    runtime_error_shared_memory_allocation_failed,
    /// Slot handle is invalid or out of range
    logic_error_invalid_slot_handle,
    /// Attempted operation on unallocated slot
    runtime_error_slot_not_allocated,
    /// No available slots for allocation
    runtime_error_no_available_slots,
    /// No configuration found for the specified interface
    logic_error_no_configuration_for_interface,
    /// No configuration found for the specified instance
    logic_error_no_configuration_for_instance,
};

score::result::Error MakeError(Shared_memory_manager_error code,
                               std::string_view user_message = "") noexcept;

/// \brief Error conditions for Bidirectional_channel operations
enum class Bidirectional_channel_error : score::result::ErrorCode {
    /// Requested client not found in server's client map
    runtime_error_client_not_found,
    /// Failed to send message
    runtime_error_send_failed,
    /// Client is not connected
    runtime_error_not_connected,
    /// Server is already listening
    logic_error_already_listening,
    /// Failed to start listening
    runtime_error_listen_failed,
    /// Server is not listening
    runtime_error_not_listening,
    /// Invalid argument provided to function
    logic_error_invalid_argument,
};

score::result::Error MakeError(Bidirectional_channel_error code,
                               std::string_view user_message = "") noexcept;

enum class Gateway_ipc_binding_error : score::result::ErrorCode {
    /// Data to be copied into container is larger than the container's maximum size
    fixed_size_container_too_small
};

score::result::Error MakeError(Gateway_ipc_binding_error code,
                               std::string_view user_message = "") noexcept;

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_INCLUDE_SCORE_GATEWAY_IPC_BINDING_ERROR
