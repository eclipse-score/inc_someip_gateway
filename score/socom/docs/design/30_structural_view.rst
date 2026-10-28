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

Rationale for Architecture Decomposition
----------------------------------------

SOCom decomposes in-process service-oriented communication into four orthogonal units:

- ``Runtime`` isolates service discovery, compatibility matching, and bridge registration from steady-state data transfer so connected endpoints communicate directly without central broker contention.
- ``Client_connector`` and ``Server_connector`` (split into ``Disabled_server_connector`` and ``Enabled_server_connector``) encode endpoint role and lifecycle state in the C++ type system, preventing event publication or method dispatch while a service is disabled.
- ``Payload`` and ``Writable_payload`` separate zero-copy memory ownership and contiguous full SOME/IP message span management (header + payload + ``slot_handle``) from connector routing logic, allowing ``gatewayd`` to run E2E protection/verification in place and ``gateway_ipc_binding`` to map shared-memory LoLa slots without copying.

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

.. _runtime_component:

Runtime
-------

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

   Factory and registry interface (``score::socom::Runtime`` and ``score::socom::create_runtime()``) for creating client/server connectors and registering service bridges.

.. real_arc_int_op:: make_client_connector
   :id: real_arc_int_op__socom_design__make_client
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__runtime

   Creates a ``Client_connector`` instance registered for a ``Service_interface_definition`` and ``Service_instance`` with user ``Client_connector::Callbacks`` (and optional ``Posix_credentials``).

.. real_arc_int_op:: make_server_connector
   :id: real_arc_int_op__socom_design__make_server
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__runtime

   Creates a ``Disabled_server_connector`` instance registered for a ``Server_service_interface_definition`` and ``Service_instance`` with user ``Disabled_server_connector::Callbacks`` (and optional ``Posix_credentials``), and supports registering service bridges via ``register_service_bridge``.

.. _client_connector_component:

Client connector
----------------

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

   Consumer-side service interface (``score::socom::Client_connector``) for subscribing to events, requesting event updates, allocating method call payloads, and invoking methods.

.. real_arc_int_op:: subscribe_event and unsubscribe_event
   :id: real_arc_int_op__socom_design__sub_event
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__client_conn

   Subscribes to or unsubscribes from an ``Event_id`` (with ``Event_mode::update`` or ``Event_mode::update_and_initial_value``) and requests on-demand updates via ``request_event_update``.

.. real_arc_int_op:: call_method and allocate_method_call_payload
   :id: real_arc_int_op__socom_design__call_method
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__client_conn

   Allocates a ``Writable_payload`` for a method call via ``allocate_method_call_payload`` and invokes ``call_method`` with a ``Payload`` and optional ``Method_call_reply_data``.

.. _server_connector_component:

Server connector
----------------

The Server_connector represents a service, which is used by a Client_connector.
It can send event updates and respond to method calls.

.. real_arc_int:: SOCom Server Connector Interface
   :id: real_arc_int__socom_design__server_conn
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :language: cpp

   Provider-side service interfaces (``score::socom::Disabled_server_connector`` and ``score::socom::Enabled_server_connector``) for enabling/disabling service availability, allocating event payloads, and publishing event updates.

.. real_arc_int_op:: enable and disable
   :id: real_arc_int_op__socom_design__enable_srv
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__server_conn

   Transitions a server connector between ``Disabled_server_connector`` and ``Enabled_server_connector`` via ``Disabled_server_connector::enable`` and ``Enabled_server_connector::disable``, notifying matching ``Client_connector`` instances of ``Service_state`` changes.

.. real_arc_int_op:: allocate_event_payload
   :id: real_arc_int_op__socom_design__alloc_payload
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__server_conn

   Allocates a zero-copy ``Writable_payload`` for a given ``Event_id`` by delegating to the subscribed ``Client_connector`` callback ``on_event_payload_allocate``.

.. real_arc_int_op:: update_event and update_requested_event
   :id: real_arc_int_op__socom_design__update_event
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__server_conn

   Publishes a ``Payload`` containing the full SOME/IP message to all subscribed ``Client_connector`` instances via ``update_event`` or to update-requesting clients via ``update_requested_event``.

.. _payload_component:

Payload
-------

``Payload`` and ``Writable_payload`` represent the contiguous message buffer transferred across SOCom connectors.
In the SOME/IP gateway, ``Payload::data()`` holds the full SOME/IP message (header + payload) in contiguous memory so E2E protection and verification operate directly on the span without copying, while ``get_slot_handle()`` tracks the underlying shared-memory slot across the IPC binding.

.. real_arc_int:: SOCom Payload Interface
   :id: real_arc_int__socom_design__payload
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :language: cpp

   Contiguous byte-span buffer interfaces (``score::socom::Payload`` and ``score::socom::Writable_payload``) with RAII release callback and shared-memory slot handle tracking.

.. real_arc_int_op:: data, wdata, shrink, and get_slot_handle
   :id: real_arc_int_op__socom_design__payload_span
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__socom_design__payload

   Provides read-only access to the contiguous full SOME/IP message via ``Payload::data()``, writable access via ``Writable_payload::wdata()``, payload size reduction via ``Writable_payload::shrink()``, and shared-memory slot identification via ``Payload::get_slot_handle()``.

Required Interfaces
-------------------

SOCom depends exclusively on ``score_baselibs``:

- ``score::Result`` (``@score_baselibs//score/result``) for error-code-based return values across connector and runtime operations.
- ``score::cpp::span`` and ``score::cpp::move_only_function`` (``@score_baselibs//score/language/futurecpp``) for contiguous byte views in ``Payload`` / ``Writable_payload`` and non-copyable callbacks.
- ``score::mw::log`` (``@score_baselibs//score/mw/log``) for diagnostic and deadlock-detection logging.
