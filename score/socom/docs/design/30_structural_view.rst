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

The bundle service oriented communication (SOCom) implements components for client/server pattern based communication for the following communication paradigms:

- remote procedure call
- publish/subscribe (event communication)

SOCom provides interfaces which enable the implementation of service gateways (bridges).
This allows users to independently implement service bridges for different communication protocols (e.g. IPC, SOME/IP, …).
SOCom provides intra-process client/server communication only.

Rationale Behind Architecture Decomposition
-------------------------------------------

SOCom decomposes in-process service-oriented communication into four orthogonal units:

- ``Runtime`` isolates service discovery, version-compatibility matching, and bridge registration from steady-state data transfer so connected endpoints communicate directly without central broker contention.
- ``Client_connector`` encapsulates the consumer endpoint for service availability notifications, event subscriptions, and method calls.
- ``Server_connector`` (split into ``Disabled_server_connector`` and ``Enabled_server_connector``) encodes provider lifecycle state in the C++ type system, preventing event publication or method dispatch while a service is disabled.
- ``Payload`` and ``Writable_payload`` decouple zero-copy buffer ownership and contiguous message span management from connector routing, keeping the 16-byte SOME/IP header and payload contiguous in memory (to support future in-place E2E protection in ``gatewayd``) and allowing ``gateway_ipc_binding`` to pass shared-memory LoLa slots without copying.

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

The Runtime creates and connects Client_connectors and Server_connectors.
In addition to that it has a plugin interface for adding bridges to cross IPC or network boundaries.
The Runtime outlives all created Client_connectors and Server_connectors.

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

A client application owns and uses this component to join client/server pattern based service oriented communication (SOCom).
The client connector interacts with the server connector in order perform the supported communication primitives.
The client connector API provides the following features:

- service instance state change indications
- asynchronous remote procedure call:

  - with method reply
  - without method reply (fire and forget)

- event subscription:

  - with optional initial value request (required for fields)

- event update indication
- requested event update indication
- event payload allocation callback for zero-copy reception

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

The Server_connector represents a service, which is used by a Client_connector.
It can send event updates and respond to method calls.

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

``Payload`` and ``Writable_payload`` represent the move-only message buffer transferred across SOCom connectors.
In the SOME/IP gateway, the buffer span holds the full SOME/IP message (16-byte header followed contiguously by payload bytes) so ``gatewayd`` can frame or strip headers and support future in-place E2E protection without copying, while an optional slot handle identifies the underlying LoLa shared-memory slot across ``gateway_ipc_binding``.

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

   Provides read-only and writable byte-span access to the contiguous SOME/IP message buffer, payload size adjustment after writing, and shared-memory slot handle lookup.

Required Interfaces
~~~~~~~~~~~~~~~~~~~

SOCom has no external network or IPC dependencies and relies only on ``score_baselibs`` utilities:

- ``score::Result`` for explicit error propagation across runtime and connector operations.
- ``score::cpp::span``, ``score::cpp::move_only_function``, and ``score::utils::ScopeExit`` for contiguous byte views, move-only callbacks, and RAII cleanup actions.
- ``score::mw::log`` for runtime diagnostic logging (such as interface minor-version mismatches).
