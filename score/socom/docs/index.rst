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

.. _socom:

SOCom
#####

.. comp:: SOCom
   :id: comp__socom
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :belongs_to: feat__someip_gateway
   :implements: real_arc_int_op__socom_design__make_client, real_arc_int_op__socom_design__make_server, real_arc_int_op__socom_design__sub_event, real_arc_int_op__socom_design__call_method, real_arc_int_op__socom_design__enable_srv, real_arc_int_op__socom_design__alloc_payload, real_arc_int_op__socom_design__update_event, real_arc_int_op__socom_design__payload_span

   In-process SOME/IP service-oriented runtime and connector abstraction library.

Overview
========

The SOCom (Service-Oriented Communication) library (``score::socom``) provides the in-process
service-oriented runtime and connector abstraction for SOME/IP communication in the S-CORE
framework. It decouples service consumers and providers from concrete SOME/IP network stack and
IPC bridge implementations through four primary abstractions:

- ``Runtime``: In-process service instance registry, connector factory, and service bridge registration point.
- ``Client_connector``: Consumer-side endpoint for service availability notifications, event subscriptions, and method invocations.
- ``Disabled_server_connector`` / ``Enabled_server_connector``: Provider-side state-typed endpoints for offering services, allocating and publishing event updates, and handling method calls.
- ``Payload`` / ``Writable_payload``: Move-only buffer handle exposing a contiguous byte span (``Payload::data()`` / ``Writable_payload::wdata()``) holding the full SOME/IP message (SOME/IP header followed contiguously by payload bytes) and an optional shared-memory ``slot_handle`` for zero-copy transport.

Safety Role
-----------

``socom`` is classified as a ``QM`` component and is instantiated in-process inside both:

- ``gatewayd`` (``ASIL_B``), which protects outgoing messages and checks incoming messages end-to-end (E2E) directly over the contiguous full SOME/IP message in ``Payload::data()``.
- ``someipd`` (``QM``), which bridges ``socom`` connectors between the SOME/IP network stack and the IPC binding to ``gatewayd``.

.. toctree::
   :maxdepth: 1

   design/index
