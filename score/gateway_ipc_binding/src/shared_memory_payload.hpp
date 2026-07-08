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

#ifndef SRC_GATEWAY_IPC_BINDING_SRC_SHARED_MEMORY_PAYLOAD
#define SRC_GATEWAY_IPC_BINDING_SRC_SHARED_MEMORY_PAYLOAD

#include <cassert>
#include <memory>
#include <score/gateway_ipc_binding/shared_memory_slot_manager.hpp>
#include <score/socom/payload.hpp>
#include <utility>

namespace score::gateway_ipc_binding {

/// \brief Creates a Writable_payload backed by a shared memory slot
inline score::socom::Writable_payload make_shared_memory_writable_payload(
    Shared_memory_slot_guard guard) noexcept {
    auto mem_result = guard.get_memory();
    assert(!mem_result.empty());
    auto handle = guard.get_handle();
    assert(handle != socom::kNoSlotHandle);
    return score::socom::Writable_payload{mem_result, *handle, [guard = std::move(guard)]() {}};
}

}  // namespace score::gateway_ipc_binding

#endif  // SRC_GATEWAY_IPC_BINDING_SRC_SHARED_MEMORY_PAYLOAD
