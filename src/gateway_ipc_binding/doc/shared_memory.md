<!--
*******************************************************************************
Copyright (c) 2026 Contributors to the Eclipse Foundation

See the NOTICE file(s) distributed with this work for additional
information regarding copyright ownership.

This program and the accompanying materials are made available under the
terms of the Apache License Version 2.0 which is available at
https://www.apache.org/licenses/LICENSE-2.0

SPDX-License-Identifier: Apache-2.0
*******************************************************************************
-->

# Shared Memory Slot Manager

The Gateway IPC binding uses shared memory as its data plane. Control messages travel over `score::message_passing`, while event payload bytes live in shared memory and are referenced by `Shared_memory_handle`.

## Current design

The binding does not maintain one global payload pool. Instead, it creates shared-memory managers per service instance.

- each side owns writable shared memory for the services it publishes over the binding
- peer memory is opened read-only on demand using `Shared_memory_metadata`
- payload ownership is tracked on the sender side until the receiver signals `Payload_consumed`

This per-service split matches the code in `Shared_memory_managers` and keeps shared-memory sizing configurable for each service/instance pair.

## Main types

### `Shared_memory_slot_manager`

Writable manager used by the local side.

- allocates fixed-size slots
- exposes slot memory for writing payload bytes
- reference-counts shared ownership of a slot
- returns a `Shared_memory_slot_guard` for RAII cleanup

### `Read_only_shared_memory_slot_manager`

Read-only view used for a peer's shared-memory pool.

- opens a pool from `Shared_memory_metadata`
- resolves `Shared_memory_handle` into a `score::socom::Payload::Sptr`
- triggers a destruction callback when the payload wrapper is released locally

### `Shared_memory_manager_factory`

Abstract factory used by the binding to:

- create writable managers for local service instances
- open read-only managers for peer service instances

## Lifetime model

The intended lifetime is:

1. allocate a writable slot for an outgoing payload
2. write bytes into the slot memory
3. send an IPC control message that references the slot via `Shared_memory_handle`
4. retain ownership until the receiver is done
5. release the slot after `Payload_consumed`

On the receiving side:

1. resolve the service mapping to peer `Shared_memory_metadata`
2. open or reuse a read-only manager for that shared-memory path
3. wrap the referenced slot as a SOCom payload
4. send `Payload_consumed` when that payload wrapper is destroyed

## How the binding uses it today

### Sending event updates

When a local `score::socom::Client_connector` produces an event update:

- the binding allocates a slot from the writable manager for that service key
- the payload is stored in `m_shared_memory_allocations` to keep it alive
- `Event_update` is sent with the local slot index and used byte count

### Receiving event updates

When an `Event_update` arrives:

- the binding looks up the peer metadata by `required_id`
- it opens the peer shared-memory pool read-only if needed
- it creates a payload object from the referenced slot
- that payload object's destruction callback sends `Payload_consumed`

## Important implementation details

- `Shared_memory_handle` contains `slot_index` and `used_bytes`; there is no byte offset field in the current protocol
- `Payload_consumed` includes `required_id`, allowing the sender to map reclamation to the correct service key
- shared-memory metadata exchange happens during `Connect_service` and `Connect_service_reply`, not during the initial `Connect`
- the writable allocation table is currently keyed first by service key and then by slot handle

## Current limitations

The code contains a few constraints worth documenting explicitly:

- only the event-update path currently uses shared memory end to end; method payload transport is not implemented yet

## Minimal usage example

```cpp
auto manager_result = Shared_memory_slot_manager::create("/service_shm", 8, 4096);
if (!manager_result) {
    return manager_result.error();
}

auto& manager = **manager_result;
auto guard_result = manager.allocate_slot();
if (!guard_result) {
    return guard_result.error();
}

auto guard = std::move(*guard_result);
auto memory = guard.get_memory();
auto handle_result = guard.get_handle();
if (!handle_result) {
    return handle_result.error();
}

std::memcpy(memory.data(), payload_bytes, payload_size);

Shared_memory_handle handle{
    static_cast<std::uint32_t>(*handle_result),
    static_cast<std::uint32_t>(payload_size),
};

// Send handle in Event_update or another payload-carrying IPC message.
```
