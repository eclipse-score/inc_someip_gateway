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

``gatewayd`` (:need:`comp__gatewayd`) is the ``ASIL_B`` SOME/IP Gateway Daemon in the S-CORE communication stack. It bridges local ``ASIL_B`` and ``QM`` applications communicating over ``score::mw::com`` (LoLa shared-memory IPC) with the ``QM`` ``someipd`` network daemon (:need:`comp__someipd`).

Description
-----------

To prevent freedom-from-interference violations between the third-party ``QM`` SOME/IP stack (``vsomeip``) and ``ASIL_B`` applications, the gateway architecture splits responsibilities across two OS processes:

- **gatewayd (ASIL_B)**: Holds zero dependency on ``vsomeip``. It connects to local ``mw::com`` services via type-erased ``GenericProxy`` and ``GenericSkeleton`` instances, serializes and deserializes application samples through the ``score_com_serializer`` plugin, frames or strips the 16-byte SOME/IP header contiguously in shared memory (keeping future end-to-end protection inside the ``ASIL_B`` domain), and exchanges complete SOME/IP frames with ``someipd`` over ``gateway_ipc_binding``.
- **someipd (QM)**: Receives the gateway configuration and consumer event slot sizes from ``gatewayd`` at startup and routes complete SOME/IP frames between the ``mw::com`` IPC bridge and ``vsomeip``.

Rationale Behind Architecture Decomposition
*******************************************

``gatewayd`` is decomposed into three cohesive implementation units aligned with its Bazel target structure in ``score/gatewayd/BUILD.bazel``:

1. **Main Startup Orchestrator** (``gatewayd``, ``score/gatewayd/main.cpp``):
   Loads the FlatBuffers gateway configuration and ``mw::com`` manifest, initializes the serializer plugin, LoLa runtime, and in-process ``socom::Runtime``, sizes the shared-memory IPC slots (maximum serialized payload plus the 16-byte SOME/IP header), connects the ``gateway_ipc_binding`` client to ``someipd``, and starts all configured local and remote service instances.

2. **LocalServiceInstance Adapter** (``local_service_instance``, ``score/gatewayd/impl/local_service_instance.h/.cpp``):
   Bridges locally provided services from the ECU to the network (*Application-to-Network*). It discovers the local application's service via ``mw::com::GenericProxy``, enables a ``socom`` server connector toward ``someipd``, and subscribes to configured application events. For each received sample, it allocates a shared-memory payload, writes the 16-byte SOME/IP header followed by the serialized sample bytes, and publishes the frame to ``someipd``.

3. **RemoteServiceInstance Adapter** (``remote_service_instance``, ``score/gatewayd/impl/remote_service_instance.h/.cpp``):
   Bridges remotely provided SOME/IP services from the network to local consumers (*Network-to-Application*). It queries the serializer plugin for event type size and alignment, offers a local ``mw::com::GenericSkeleton`` on the ECU, and connects a ``socom`` client connector to ``someipd``. When a SOME/IP event arrives, it strips the 16-byte header, deserializes the payload into an allocated LoLa sample, and sends it to local consumer applications.

Static Architecture
-------------------

.. comp_arc_sta:: Gateway Daemon Static Architecture
   :id: comp_arc_sta__gatewayd_arch__static_view
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :belongs_to: comp__gatewayd

   Static decomposition of ``gatewayd`` into ``gatewayd`` (``main.cpp``), ``LocalServiceInstance``, and ``RemoteServiceInstance`` units and their dependencies on ``mw::com``, ``socom``, ``gateway_ipc_binding``, and ``serializer``.

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

   Runtime sequence for local service event forwarding (Application to ``gatewayd`` to ``someipd``) and remote service event forwarding (``someipd`` to ``gatewayd`` to Application).

   .. uml:: ../models/sequence_diagram_gatewayd.puml
      :align: center
      :caption: Gateway Daemon Event Forwarding Sequence Diagram

Interfaces
----------

``gatewayd`` is an executable daemon process and does not expose a C++ library header to external applications. Internally, the startup orchestrator interacts with two adapter interfaces implemented by ``LocalServiceInstance`` and ``RemoteServiceInstance``.

.. real_arc_int:: LocalServiceInstance Adapter Interface
   :id: real_arc_int__gatewayd_arch__local_svc
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :language: cpp

   Internal C++ factory and discovery interface declared in ``score/gatewayd/impl/local_service_instance.h`` (namespace ``score::someip_gateway::gatewayd``) for bridging locally provided ``mw::com`` services to ``someipd``.

.. real_arc_int_op:: CreateAsyncLocalServices and Create
   :id: real_arc_int_op__gatewayd_arch__local_svc
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :included_by: real_arc_int__gatewayd_arch__local_svc

   - ``LocalServiceInstance::CreateAsyncLocalServices``: Starts asynchronous ``mw::com::GenericProxy`` discovery for a configured local service instance and instantiates ``LocalServiceInstance`` once the provider is found.
   - ``LocalServiceInstance::Create``: Enables the ``socom`` server connector toward ``someipd`` and subscribes to configured ``GenericProxyEvent`` updates to frame, serialize, and forward application samples.

.. real_arc_int:: RemoteServiceInstance Adapter Interface
   :id: real_arc_int__gatewayd_arch__remote_svc
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :language: cpp

   Internal C++ factory interface declared in ``score/gatewayd/impl/remote_service_instance.h`` (namespace ``score::someip_gateway::gatewayd``) for bridging remotely provided SOME/IP services from ``someipd`` to local ``mw::com`` consumer applications.

.. real_arc_int_op:: CreateAsyncRemoteService and Create
   :id: real_arc_int_op__gatewayd_arch__remote_svc
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :included_by: real_arc_int__gatewayd_arch__remote_svc

   - ``RemoteServiceInstance::CreateAsyncRemoteService``: Queries event type size and alignment from ``score_com_serializer``, creates the local ``mw::com::GenericSkeleton``, and delegates to ``RemoteServiceInstance::Create``.
   - ``RemoteServiceInstance::Create``: Offers the ``mw::com::GenericSkeleton`` to local applications and registers a ``socom`` client connector that subscribes to remote SOME/IP events, strips the 16-byte header, and publishes deserialized LoLa samples.
