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

.. _gatewayd_architecture:

Gateway Daemon Architecture
===========================

Overview
--------

``gatewayd`` (:need:`comp__gatewayd`) is the ``ASIL_B`` SOME/IP Gateway Daemon in the S-CORE communication stack. It bridges local ``ASIL_B`` and ``QM`` applications communicating over ``score::mw::com`` (LoLa shared-memory IPC) with the ``QM`` ``someipd`` network daemon (:need:`comp__someipd`), isolating local safety-critical applications from the third-party SOME/IP network stack while handling SOME/IP header framing and sample serialization within the ``ASIL_B`` process boundary.

Description
-----------

Rationale Behind Architecture Decomposition
*******************************************

``gatewayd`` separates process-level safety isolation and startup coordination from per-service directional bridging:

- **ASIL_B isolation from the QM network stack**: To maintain freedom from interference (FFI) between ``vsomeip`` (``QM``) and ``ASIL_B`` applications, ``gatewayd`` runs as a separate ``ASIL_B`` process with zero dependency on ``vsomeip``. It runs ``score_com_serializer`` and frames or strips the 16-byte SOME/IP header contiguously in shared memory inside the ``ASIL_B`` domain (where E2E protection will run), while ``someipd`` (``QM``) owns the network sockets and ``vsomeip`` routing.
- **Startup synchronization before service bridging** (``main.cpp``): ``main.cpp`` initializes the configuration, serializer plugin, ``mw::com`` runtime, and ``socom::Runtime``, creates the ``gateway_ipc_binding`` client, and waits for ``binding_client->is_connected()`` before creating any service adapters so ``someipd`` is guaranteed to have registered the bridged IPC services first.
- **Directional split** (``LocalServiceInstance`` vs ``RemoteServiceInstance``): Outbound and inbound services have opposite discovery and ``mw::com`` roles — ``LocalServiceInstance`` discovers local ``mw::com`` providers via ``GenericProxy`` and publishes framed SOME/IP events into a ``socom`` server connector, whereas ``RemoteServiceInstance`` offers a local ``GenericSkeleton`` up front and subscribes to incoming SOME/IP events via a ``socom`` client connector.

Static Architecture
-------------------

.. comp_arc_sta:: Gateway Daemon Static Architecture
   :id: comp_arc_sta__gatewayd_arch__static_view
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :belongs_to: comp__gatewayd
   :fulfils: comp_req__gatewayd__starts_from_config

   Component structure of ``gatewayd`` and its dependencies on ``mw::com``, ``socom``, ``gateway_ipc_binding``, and ``serializer``.

   .. uml:: ../models/component_diagram_gatewayd.puml
      :align: center
      :caption: Gateway Daemon Static Component Diagram

Dynamic Architecture
--------------------

.. comp_arc_dyn:: Gateway Daemon Event Forwarding
   :id: comp_arc_dyn__gatewayd_arch__event_flow
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :belongs_to: comp__gatewayd
   :fulfils: comp_req__gatewayd__starts_from_config

   Runtime sequence for local service event forwarding (Application to ``gatewayd`` to ``someipd``) and remote service event forwarding (``someipd`` to ``gatewayd`` to Application).

   .. uml:: ../models/sequence_diagram_gatewayd.puml
      :align: center
      :caption: Gateway Daemon Event Forwarding Sequence Diagram

Interfaces
----------

``gatewayd`` is an executable daemon process and does not expose a C++ library header to external applications. Internally, ``main.cpp`` uses two adapter factory interfaces in namespace ``score::someip_gateway::gatewayd``.

.. real_arc_int:: LocalServiceInstance Adapter Interface
   :id: real_arc_int__gatewayd_arch__local_svc
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :language: cpp
   :fulfils: comp_req__gatewayd__starts_from_config

   Internal C++ factory and discovery interface declared in ``score/gatewayd/impl/local_service_instance.h`` (namespace ``score::someip_gateway::gatewayd``) for bridging locally provided ``mw::com`` services to ``someipd``.

.. real_arc_int_op:: CreateAsyncLocalServices and Create
   :id: real_arc_int_op__gatewayd_arch__local_svc
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :included_by: real_arc_int__gatewayd_arch__local_svc

   - ``LocalServiceInstance::CreateAsyncLocalServices``: Starts asynchronous ``mw::com::GenericProxy`` discovery for a configured local service instance and instantiates ``LocalServiceInstance`` (stopping discovery) once the provider is found.
   - ``LocalServiceInstance::Create``: Enables the ``socom`` server connector toward ``someipd`` and subscribes to configured ``GenericProxyEvent`` updates to frame, serialize, and forward application samples.

.. real_arc_int:: RemoteServiceInstance Adapter Interface
   :id: real_arc_int__gatewayd_arch__remote_svc
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :language: cpp
   :fulfils: comp_req__gatewayd__starts_from_config

   Internal C++ factory interface declared in ``score/gatewayd/impl/remote_service_instance.h`` (namespace ``score::someip_gateway::gatewayd``) for bridging remotely provided SOME/IP services from ``someipd`` to local ``mw::com`` consumer applications.

.. real_arc_int_op:: CreateAsyncRemoteService and Create
   :id: real_arc_int_op__gatewayd_arch__remote_svc
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :included_by: real_arc_int__gatewayd_arch__remote_svc

   - ``RemoteServiceInstance::CreateAsyncRemoteService``: Queries event type size and alignment from ``score_com_serializer``, creates the local ``mw::com::GenericSkeleton``, and delegates to ``RemoteServiceInstance::Create``.
   - ``RemoteServiceInstance::Create``: Offers the ``mw::com::GenericSkeleton`` to local applications and registers a ``socom`` client connector that subscribes to remote SOME/IP events when ``on_service_state_change(Service_state::available)`` fires, strips the 16-byte header, and publishes deserialized LoLa samples.
