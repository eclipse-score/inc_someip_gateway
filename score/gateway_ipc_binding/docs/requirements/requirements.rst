..
   # *******************************************************************************
   # Copyright (c) 2026 Contributors to the Eclipse Foundation
   #
   # See the NOTICE file(s) distributed with this work for additional
   # information regarding copyright ownership.
   #
   # This program and the accompanying materials are made available under the
   # terms of the Apache License Version 2.0 which is available at
   # https://www.apache.org/licenses/LICENSE-2.0
   #
   # SPDX-License-Identifier: Apache-2.0
   # *******************************************************************************

Component Gateway IPC Binding Requirements
##########################################

Functional Requirements
-----------------------

.. comp_req:: Symmetric IPC binding with split control and payload planes
   :id: comp_req__gateway_ipc_binding__ctrl_chan
   :reqtype: Functional
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__gateway_ipc_binding

   The gateway IPC binding shall establish a symmetric client/server
   control channel over ``score::message_passing`` and, once
   established, allow either peer to offer services, request services,
   subscribe to events, and publish event updates. Event payloads
   shall travel through per-service shared-memory segments rather than
   the control channel, referenced by a ``Shared_memory_handle`` and
   released via ``Payload_consumed``.

.. comp_req:: Fixed-size shared memory slot allocation
   :id: comp_req__gateway_ipc_binding__slot_alloc
   :reqtype: Functional
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__gateway_ipc_binding

   The binding shall allocate event payloads out of a
   ``SharedMemorySlotManager`` backed by a ``FixedSizeContainer`` so
   that payload allocation, transmission and reclaim happen without
   dynamic allocation on the hot path and without heap fragmentation
   in long-running gateway processes.

.. comp_req:: Deferred connection setup for late peers
   :id: comp_req__gateway_ipc_binding__pending_conn
   :reqtype: Functional
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__gateway_ipc_binding

   When a peer requests a service that has not yet been offered, the
   binding shall queue the connect attempt in ``PendingConnects`` and
   complete the connection automatically once the corresponding
   ``OfferService`` is received, so client and server startup order do
   not need to be coordinated externally.

