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

.. _gateway_ipc_binding:

Gateway IPC Binding
###################

Abstract
========

The Gateway IPC Binding (``score::gateway_ipc_binding``) bridges SOCom service discovery,
subscription propagation, and zero-copy event transport between ``gatewayd`` and ``someipd`` over
``mw::com`` (LoLa) shared-memory IPC:

- ``Gateway_ipc_binding_client`` / ``Client_adapter`` and ``Gateway_ipc_binding_server`` / ``Server_adapter``: Daemon-level endpoints in ``gatewayd`` and ``someipd`` that own the configured per-service bindings and the ``SomeipdService`` endpoint.
- ``SomeipdService`` (``Someipd_service_consumer`` / ``Someipd_service_provider``): Typed ``mw::com`` service used to track ``someipd`` connectivity and register ``gatewayd`` service configurations at runtime via ``AddServiceConfiguration``.
- ``Provider_service_binding`` and ``Consumer_service_binding``: Per-service bridges connecting local ``socom`` client or server connectors to ``mw::com`` ``GenericSkeleton`` or ``GenericProxy`` instances.
- ``sample_layout``: Shared-memory framing helpers that place an 8-byte binding prefix (4-byte ``uint32_t`` payload length plus 4 reserved bytes) in front of variable-length SOME/IP messages inside fixed-size LoLa sample slots.

.. comp:: Gateway IPC Binding
   :id: comp__gateway_ipc_binding
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :belongs_to: feat__someip_gateway
   :implements: real_arc_int_op__gib_arch__create_client, real_arc_int_op__gib_arch__is_connected, real_arc_int_op__gib_arch__create_server, real_arc_int_op__gib_arch__start_server, real_arc_int_op__gib_arch__add_svc_cfg
   :uses: real_arc_int_op__socom_design__make_client, real_arc_int_op__socom_design__make_server, real_arc_int_op__socom_design__sub_event, real_arc_int_op__socom_design__enable_srv, real_arc_int_op__socom_design__alloc_payload, real_arc_int_op__socom_design__update_event, real_arc_int_op__socom_design__payload_span

   Bridges SOCom service discovery, subscription propagation, and shared-memory event transport between :need:`comp__gatewayd` and :need:`comp__someipd` over ``mw::com`` (LoLa), and provides daemon connectivity and runtime configuration registration via ``SomeipdService``.

.. toctree::
   :maxdepth: 2

   architecture/index
