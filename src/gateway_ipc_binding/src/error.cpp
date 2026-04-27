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

#include <score/gateway_ipc_binding/error.hpp>

namespace score::gateway_ipc_binding {
namespace {

class Shared_memory_manager_error_domain final : public score::result::ErrorDomain {
   public:
    std::string_view MessageFor(score::result::ErrorCode const& code) const noexcept override {
        switch (static_cast<Shared_memory_manager_error>(code)) {
            case Shared_memory_manager_error::logic_error_invalid_slot_size:
                return "Slot size must be greater than zero";
            case Shared_memory_manager_error::logic_error_invalid_slot_count:
                return "Number of slots must be greater than zero";
            case Shared_memory_manager_error::runtime_error_shared_memory_allocation_failed:
                return "Failed to allocate shared memory";
            case Shared_memory_manager_error::logic_error_invalid_slot_handle:
                return "Slot handle is invalid or out of range";
            case Shared_memory_manager_error::runtime_error_slot_not_allocated:
                return "Slot is not allocated";
            case Shared_memory_manager_error::runtime_error_no_available_slots:
                return "No available slots for allocation";
            case Shared_memory_manager_error::logic_error_no_configuration_for_interface:
                return "No configuration found for the specified interface";
            case Shared_memory_manager_error::logic_error_no_configuration_for_instance:
                return "No configuration found for the specified instance";
            default:
                return "Unknown error";
        }
    }
};

}  // namespace

score::result::Error MakeError(Shared_memory_manager_error code,
                               std::string_view user_message) noexcept {
    static constexpr Shared_memory_manager_error_domain error_domain;
    return {static_cast<score::result::ErrorCode>(code), error_domain, user_message};
}

namespace {

class Bidirectional_channel_error_domain final : public score::result::ErrorDomain {
   public:
    std::string_view MessageFor(score::result::ErrorCode const& code) const noexcept override {
        switch (static_cast<Bidirectional_channel_error>(code)) {
            case Bidirectional_channel_error::runtime_error_client_not_found:
                return "Client not found";
            case Bidirectional_channel_error::runtime_error_send_failed:
                return "Failed to send message";
            case Bidirectional_channel_error::runtime_error_not_connected:
                return "Client is not connected";
            case Bidirectional_channel_error::logic_error_already_listening:
                return "Server is already listening";
            case Bidirectional_channel_error::runtime_error_listen_failed:
                return "Failed to start listening";
            case Bidirectional_channel_error::runtime_error_not_listening:
                return "Server is not listening";
            case Bidirectional_channel_error::logic_error_invalid_argument:
                return "Invalid argument provided to function";
            default:
                return "Unknown error";
        }
    }
};

}  // namespace

score::result::Error MakeError(Bidirectional_channel_error code,
                               std::string_view user_message) noexcept {
    static constexpr Bidirectional_channel_error_domain error_domain;
    return {static_cast<score::result::ErrorCode>(code), error_domain, user_message};
}

namespace {
class Gateway_ipc_binding_error_domain final : public score::result::ErrorDomain {
   public:
    std::string_view MessageFor(score::result::ErrorCode const& code) const noexcept override {
        switch (static_cast<Gateway_ipc_binding_error>(code)) {
            case Gateway_ipc_binding_error::fixed_size_container_too_small:
                return "Data to be copied into container is larger than the container's maximum "
                       "size";
            default:
                return "Unknown error";
        }
    }
};
}  // namespace

score::result::Error MakeError(Gateway_ipc_binding_error code,
                               std::string_view user_message) noexcept {
    static constexpr Gateway_ipc_binding_error_domain error_domain;
    return {static_cast<score::result::ErrorCode>(code), error_domain, user_message};
}

}  // namespace score::gateway_ipc_binding
