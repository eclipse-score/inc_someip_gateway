<!-- ----------------------------------------------------------------------------
  Copyright (c) 2026 Contributors to the Eclipse Foundation

  See the NOTICE file(s) distributed with this work for additional
  information regarding copyright ownership.

  This program and the accompanying materials are made available under the
  terms of the Apache License Version 2.0 which is available at
  https://www.apache.org/licenses/LICENSE-2.0

  SPDX-License-Identifier: Apache-2.0
----------------------------------------------------------------------------- -->

# Gateway IPC Binding

Gateway IPC Binding bridges SOCom service discovery and event transport across a local IPC link.
It combines:

- a control channel built on ``score::message_passing``
- per-service shared memory segments for payload transport
- a ``score::socom::Runtime`` bridge registration on each side

The implementation is intentionally symmetric after the control channel is established: both peers can request services, offer services, subscribe to events, and publish event updates. In the deployed topology, ``someipd`` typically hosts ``Gateway_ipc_binding_server`` and ``gatewayd`` typically hosts ``Gateway_ipc_binding_client``.

## Design

[The design](doc/index.rst) describes the behavior and architecture of the Gateway IPC Binding.
