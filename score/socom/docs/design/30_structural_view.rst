..
   # *******************************************************************************
   # Copyright (c) 2025 Contributors to the Eclipse Foundation
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

Structural View
===============

Overview
--------

The SOCom (Service-Oriented Communication) library provides intra-process client/server communication and a bridge plugin interface that enables service gateways across external protocols (such as IPC and SOME/IP). It supports two communication patterns:

- **Remote procedure calls (RPC)**: Asynchronous request/response and fire-and-forget method invocations.
- **Publish/subscribe (event communication)**: Event subscriptions, event updates, and field notifications with initial-value requests.

Classified as a ``QM`` component, ``socom`` is instantiated in-process inside both SOME/IP Gateway daemons:

- ``gatewayd`` (``ASIL_B``): Frames or strips the 16-byte SOME/IP header contiguously with the payload in the buffer span (enabling future in-place E2E protection in the ``ASIL_B`` context) and serializes or deserializes application samples.
- ``someipd`` (``QM``): Bridges ``socom`` connectors between the SOME/IP network stack and the IPC binding to ``gatewayd``.

Rationale Behind Architecture Decomposition
-------------------------------------------

SOCom separates connection setup and service matching from steady-state message transport:

- **Central matching vs direct data path** (``Runtime`` vs connectors): ``Runtime`` handles service registration, interface and version compatibility checks (``Service_interface_definition``), and bridge callbacks during setup; once connected, ``Client_connector`` and ``Enabled_server_connector`` invoke each other's endpoints directly without central broker contention on data-path calls.
- **Compile-time provider state** (``Disabled_server_connector`` / ``Enabled_server_connector``): Encoding disabled and enabled server states as distinct move-only C++ types prevents a provider from allocating or publishing events while its service is not offered.
- **Zero-copy buffer ownership** (``Payload`` / ``Writable_payload``): Separating buffer lifetime (release callback and optional shared-memory ``slot_handle``) from connector routing lets bridges such as ``gateway_ipc_binding`` pass LoLa shared-memory sample slots across connectors without copying.

Static Architecture
-------------------

.. comp_arc_sta:: SOCom Static Architecture
   :id: comp_arc_sta__socom_design__static_view
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :belongs_to: comp__socom

   .. uml:: models/component_diagram_socom.puml
      :align: center
      :caption: Component diagram of SOCom

**Software elements**

- :ref:`runtime_component`
- :ref:`client_connector_component`
- :ref:`server_connector_component`
- :ref:`payload_component`

Interfaces
----------

.. _runtime_component:

Runtime
~~~~~~~

``Runtime`` is the in-process service registry and connector factory and outlives all created client and server connectors:

- **Connector creation and matching**: Creates ``Client_connector`` and ``Disabled_server_connector`` instances and connects compatible service interfaces and instances within the process.
- **Bridge plugin registration**: Registers cross-domain service bridges (such as ``gateway_ipc_binding`` and the SOME/IP stack bridge) to forward service discovery and communication across IPC or network boundaries.

.. real_arc_int:: SOCom Runtime Interface
   :id: real_arc_int__socom_design__runtime
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :language: cpp

   In-process service registry and factory interface (``score::socom::Runtime``) for creating client and server connectors and registering cross-domain service bridges.

.. real_arc_int_op:: make_client_connector
   :id: real_arc_int_op__socom_design__make_client
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__runtime

   Creates and registers a ``Client_connector`` for a target service interface and instance.

.. real_arc_int_op:: make_server_connector
   :id: real_arc_int_op__socom_design__make_server
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__runtime

   Creates and registers a ``Disabled_server_connector`` for a provided service interface and instance, and supports registering service bridges with the runtime.

.. _client_connector_component:

Client connector
~~~~~~~~~~~~~~~~

A service consumer owns a ``Client_connector`` to interact with a matching server connector in SOCom. The ``Client_connector`` API provides the following capabilities:

- Service instance state change notifications (``Service_state::available`` and ``Service_state::not_available``).
- Asynchronous remote procedure calls:

  - with method reply
  - without method reply (fire and forget)

- Event subscriptions:

  - with optional initial value request (required for fields)

- Event update and requested-event update indications.
- Event payload allocation callback for zero-copy reception.

.. real_arc_int:: SOCom Client Connector Interface
   :id: real_arc_int__socom_design__client_conn
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :language: cpp

   Consumer-side connector interface (``score::socom::Client_connector``) for receiving service state updates, subscribing to events, and invoking methods.

.. real_arc_int_op:: subscribe_event and unsubscribe_event
   :id: real_arc_int_op__socom_design__sub_event
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__client_conn

   Subscribes to or unsubscribes from service events (with optional initial-value request for fields) and requests on-demand event updates.

.. real_arc_int_op:: call_method and allocate_method_call_payload
   :id: real_arc_int_op__socom_design__call_method
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__client_conn

   Allocates method-call request buffers and invokes asynchronous RPC methods (with optional reply handling or fire-and-forget).

.. _server_connector_component:

Server connector
~~~~~~~~~~~~~~~~

A service provider owns a server connector to offer a service instance to matching ``Client_connector`` endpoints, publish event updates, and respond to incoming method calls. SOCom splits the server connector into two state-typed handles that enforce lifecycle ordering at compile time:

- ``Disabled_server_connector``: Initial inactive endpoint returned by ``Runtime::make_server_connector()``, configured with server callbacks before offering the service via ``enable()``.
- ``Enabled_server_connector``: Active endpoint returned by ``enable()`` that exposes ``allocate_event_payload()``, ``update_event()``, and ``update_requested_event()`` while the service is available to clients, and transitions back via ``disable()``.

.. real_arc_int:: SOCom Server Connector Interface
   :id: real_arc_int__socom_design__server_conn
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :language: cpp

   Provider-side connector interfaces (``score::socom::Disabled_server_connector`` and ``score::socom::Enabled_server_connector``) for controlling service availability, publishing events, and handling method calls.

.. real_arc_int_op:: enable and disable
   :id: real_arc_int_op__socom_design__enable_srv
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__server_conn

   Transitions a server connector between disabled and enabled states and notifies matching clients of service availability changes.

.. real_arc_int_op:: allocate_event_payload
   :id: real_arc_int_op__socom_design__alloc_payload
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__server_conn

   Allocates a writable event buffer from the subscribed client endpoint for zero-copy event transmission.

.. real_arc_int_op:: update_event and update_requested_event
   :id: real_arc_int_op__socom_design__update_event
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__server_conn

   Publishes an event payload to the subscribed client or responds to a pending on-demand event update request.

.. _payload_component:

Payload
~~~~~~~

SOCom transfers message buffers across connectors using move-only RAII handles that decouple memory ownership from connector routing. In the SOME/IP Gateway, the contiguous buffer span holds the full SOME/IP message (16-byte header followed contiguously by payload bytes) so ``gatewayd`` can frame or strip headers and support future in-place E2E protection without copying:

- ``Payload``: Read-only move-only buffer handle exposing ``data()`` for message inspection and ``get_slot_handle()`` to identify an underlying LoLa shared-memory slot across ``gateway_ipc_binding``.
- ``Writable_payload``: Mutable move-only buffer handle returned by payload allocation calls, exposing ``wdata()`` for in-place header framing and payload serialization and ``shrink()`` to finalize the written byte length before transmission.

.. real_arc_int:: SOCom Payload Interface
   :id: real_arc_int__socom_design__payload
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :language: cpp

   Move-only message buffer interfaces (``score::socom::Payload`` and ``score::socom::Writable_payload``) providing contiguous byte-span access, RAII memory release, and shared-memory slot identification.

.. real_arc_int_op:: data, wdata, shrink, and get_slot_handle
   :id: real_arc_int_op__socom_design__payload_span
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__payload

   Provides read-only and writable byte-span access to the contiguous message buffer, payload size adjustment after writing, and shared-memory slot handle lookup.
