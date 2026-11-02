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

.. _gateway_ipc_binding_architecture:

Gateway IPC Binding Architecture
================================

Rationale for Architecture Decomposition
----------------------------------------

``comp__gateway_ipc_binding`` separates the asymmetric daemon-level control channel from symmetric per-service event bridging:

- ``Client_adapter`` and ``Server_adapter`` implement the public client (``gatewayd``) and server (``someipd``) endpoints. Both own the active ``Service_binding`` instances and register synthesized bridged-service deployments with the ``mw::com`` runtime. ``Server_adapter`` also offloads consumer service discovery onto a dedicated worker thread so discovery never runs inside an ``mw::com`` method-call handler.
- ``Someipd_service_consumer`` and ``Someipd_service_provider`` isolate the typed ``SomeipdService`` control channel. The provider (in ``someipd``) offers ``SomeipdService`` and receives runtime service registrations; the consumer (in ``gatewayd``) discovers ``SomeipdService``, registers ``gatewayd``'s configuration and consumer event sizes, and tracks peer connection state.
- ``Provider_service_binding`` and ``Consumer_service_binding`` implement per-service event bridging. Because both ``gatewayd`` and ``someipd`` simultaneously provide some services and consume others over IPC, separating per-service data direction from the client/server daemon role avoids duplicating bridging logic:

  - ``Provider_service_binding`` consumes a local ``socom`` service and publishes its events through a ``mw::com::GenericSkeleton``.
  - ``Consumer_service_binding`` subscribes to a remote ``mw::com::GenericProxy`` and exposes the service locally through ``socom`` server connectors.
- ``sample_layout`` encapsulates the 8-byte payload-length prefix and alignment calculation needed to carry variable-length SOME/IP messages inside fixed-size ``mw::com`` shared-memory slots.

FlatBuffers configuration parsing is kept outside ``comp__gateway_ipc_binding`` in the separate ``score/bridged_services`` helper library, keeping the IPC binding independent of the gateway configuration schema.

Static View
-----------

.. comp_arc_sta:: Gateway IPC Binding Static Architecture
   :id: comp_arc_sta__gib_arch__static_view
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :belongs_to: comp__gateway_ipc_binding

   Static decomposition of the Gateway IPC Binding into client/server adapters, the ``SomeipdService`` control endpoints, provider/consumer service bindings, and sample framing, showing their relationships with ``mw::com`` and ``socom``.

   .. uml:: ../models/component_diagram_gateway_ipc_binding_mw_com.puml
      :align: center
      :caption: Gateway IPC Binding Static Architecture

Dynamic View
------------

.. comp_arc_dyn:: Gateway IPC Binding Startup and Event Flow
   :id: comp_arc_dyn__gib_arch__startup_and_events
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :belongs_to: comp__gateway_ipc_binding

   Startup sequence for ``SomeipdService`` discovery and runtime service registration, followed by zero-copy event sample allocation, transmission, and reception between ``socom`` and ``mw::com``.

   .. uml:: ../models/mw_com_startup.puml
      :align: center
      :caption: Startup sequence: SomeipdService discovery and AddServiceConfiguration

   .. uml:: ../models/mw_com_event_flow.puml
      :align: center
      :caption: Bridged service event allocation and delivery flow

Provided Interfaces
-------------------

.. real_arc_int:: Gateway IPC Binding Client Interface
   :id: real_arc_int__gib_arch__client
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :language: cpp

   Client-side lifecycle and configuration interface used by ``gatewayd`` to connect its bridged services to ``someipd``.

.. real_arc_int_op:: mw_com::create_client
   :id: real_arc_int_op__gib_arch__create_client
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__gib_arch__client

   Creates the client-side IPC binding for ``gatewayd``, sets up the configured bridged service bindings in the local ``socom`` runtime, and starts ``SomeipdService`` discovery to register ``gatewayd``'s configuration with ``someipd``.

.. real_arc_int_op:: Gateway_ipc_binding_client::is_connected
   :id: real_arc_int_op__gib_arch__is_connected
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__gib_arch__client

   Returns ``true`` while the peer ``someipd`` daemon is available and has accepted this ``gatewayd``'s service configuration.

.. real_arc_int:: Gateway IPC Binding Server Interface
   :id: real_arc_int__gib_arch__server
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :language: cpp

   Server-side lifecycle and configuration handler interface used by ``someipd`` to accept registrations from ``gatewayd`` peers.

.. real_arc_int_op:: mw_com::create_server
   :id: real_arc_int_op__gib_arch__create_server
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__gib_arch__server

   Creates the server-side IPC binding for ``someipd`` with the handler invoked when a ``gatewayd`` peer registers its configuration.

.. real_arc_int_op:: Gateway_ipc_binding_server::start
   :id: real_arc_int_op__gib_arch__start_server
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__gib_arch__server

   Offers ``SomeipdService`` over ``mw::com`` so ``gatewayd`` peers can discover ``someipd`` and register their bridged services.

.. real_arc_int:: SomeipdService Control Interface
   :id: real_arc_int__gib_arch__someipd_svc
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :language: cpp

   Typed ``mw::com`` control service (``/score/someip/SomeipdService``) used between ``gatewayd`` and ``someipd`` for peer liveness detection and runtime configuration registration.

.. real_arc_int_op:: AddServiceConfiguration
   :id: real_arc_int_op__gib_arch__add_svc_cfg
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__gib_arch__someipd_svc

   Transfers ``gatewayd``'s gateway configuration bytes and consumer event payload sizes to ``someipd`` for all-or-nothing creation of the corresponding bridged services.

Required Interfaces
-------------------

- :need:`comp__socom`:

  - Connector creation (:need:`real_arc_int_op__socom_design__make_client`, :need:`real_arc_int_op__socom_design__make_server`) to attach local client and server endpoints for each bridged service instance.
  - Event subscription and forwarding (:need:`real_arc_int_op__socom_design__sub_event`, :need:`real_arc_int_op__socom_design__enable_srv`, :need:`real_arc_int_op__socom_design__alloc_payload`, :need:`real_arc_int_op__socom_design__update_event`, :need:`real_arc_int_op__socom_design__payload_span`) to propagate availability and subscriptions and to wrap shared-memory event samples as zero-copy ``socom`` payloads.

- ``score::mw::com`` (LoLa):

  - ``GenericSkeleton`` and ``GenericProxy`` for type-erased shared-memory event transport, service discovery, and subscription notification across bridged services.
  - Typed skeleton and proxy for ``SomeipdService`` peer liveness and runtime configuration registration.
  - Runtime configuration registration to synthesize and add bridged service deployments at startup or registration time.
