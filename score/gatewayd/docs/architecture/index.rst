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

Overview and Safety Partitioning
--------------------------------

``gatewayd`` (:need:`comp__gatewayd`) is the ``ASIL_B`` SOME/IP Gateway Daemon in the S-CORE communication stack. It bridges local ``ASIL_B`` and ``QM`` applications communicating over ``score::mw::com`` (LoLa shared-memory IPC) with the ``QM`` ``someipd`` network daemon (:need:`comp__someipd`).

To prevent freedom-from-interference violations between the third-party ``QM`` SOME/IP stack (``vsomeip``) and ``ASIL_B`` applications, the gateway architecture splits responsibilities across two OS processes:

- **gatewayd (ASIL_B)**: Holds zero dependency on ``vsomeip``. It attaches to application-facing ``mw::com`` service instances using type-erased ``GenericProxy`` and ``GenericSkeleton`` instances, invokes the generated ``score_com_serializer`` plugin to serialize and deserialize structured C++ samples, constructs or strips the 16-byte SOME/IP header contiguously in shared memory (enabling future end-to-end protection calculation within the ``ASIL_B`` context), and exchanges full SOME/IP messages with ``someipd`` through ``gateway_ipc_binding_mw_com``.
- **someipd (QM)**: Receives the raw FlatBuffers configuration and consumer event sizes from ``gatewayd`` over ``SomeipdService::AddServiceConfiguration`` and routes full SOME/IP frames between the ``mw::com`` IPC bridge and ``vsomeip``.

Rationale for Architecture Decomposition
----------------------------------------

``gatewayd`` is decomposed into three cohesive implementation units aligned with its Bazel target structure in ``score/gatewayd/BUILD.bazel``:

1. **Main Startup Orchestrator (``main_orchestrator``, ``score/gatewayd/main.cpp``)**:
   Parses command-line paths for the FlatBuffers gateway configuration (``--configuration``) and ``mw::com`` manifest (``--service_instance_manifest``), initializes the ``score_com_serializer`` C plugin and ``score::mw::com`` runtime, creates the ``score::socom::Runtime``, computes per-event shared-memory slot sizes via ``event_slot_size()`` (``score_com_serializer_get_max_serialized_size() + kSomeipFullHeaderSize``), establishes the ``mw::com`` IPC binding client to ``someipd`` via ``gateway_ipc_binding::mw_com::create_client()``, waits for ``binding_client->is_connected()``, and launches asynchronous discovery and offering for all configured local and remote service instances.

2. **LocalServiceInstance Adapter (``local_service_instance``, ``score/gatewayd/impl/local_service_instance.h/.cpp``)**:
   Implements the *Application-to-Network* bridge for a locally provided service instance. It discovers the local application's service via ``mw::com::GenericProxy::StartFindService``, creates and enables a ``socom::Enabled_server_connector`` (which ``gateway_ipc_binding`` bridges to ``someipd`` as a ``Role::provider`` ``GenericSkeleton``), subscribes to application ``GenericProxyEvent`` updates, and on each sample allocates a contiguous shared-memory ``socom::Writable_payload``, writes the 16-byte SOME/IP header followed by the serialized sample via ``score_com_serializer_serialize()``, shrinks the payload to the exact byte length, and publishes it via ``Enabled_server_connector::update_event()``.

3. **RemoteServiceInstance Adapter (``remote_service_instance``, ``score/gatewayd/impl/remote_service_instance.h/.cpp``)**:
   Implements the *Network-to-Application* bridge for a remotely provided SOME/IP service instance. It queries ``score_com_serializer`` for the application C++ type size and alignment (``score_com_serializer_get_sizeof_type`` / ``score_com_serializer_get_alignof_type``), creates and offers a local ``mw::com::GenericSkeleton`` to consumer applications on the ECU, registers a ``socom::Client_connector`` (which ``gateway_ipc_binding`` bridges to ``someipd`` as a ``Role::consumer`` ``GenericProxy``), subscribes to all configured SOME/IP events when the service becomes available, and in ``forward_event()`` strips the 16-byte SOME/IP header (``payload.data().subspan(kSomeipFullHeaderSize)``), allocates a LoLa sample via ``GenericSkeletonEvent::Allocate()``, deserializes the payload via ``score_com_serializer_deserialize()``, and sends it to local consumers via ``GenericSkeletonEvent::Send()``.

Static View
-----------

.. comp_arc_sta:: Gateway Daemon Static Architecture
   :id: comp_arc_sta__gatewayd_arch__static_view
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :belongs_to: comp__gatewayd

   Static decomposition of ``gatewayd`` into ``main_orchestrator``, ``local_service_instance``, and ``remote_service_instance`` units and their dependencies on ``socom``, ``gateway_ipc_binding``, ``bridged_services``, ``config``, ``serializer``, and ``mw::com``.

   .. uml:: ../models/component_diagram_gatewayd.puml
      :align: center
      :caption: Gateway Daemon Static Component Diagram

Dynamic View
------------

.. comp_arc_dyn:: Gateway Daemon Startup and Event Forwarding
   :id: comp_arc_dyn__gatewayd_arch__event_flow
   :version: 1
   :security: NO
   :safety: ASIL_B
   :status: valid
   :belongs_to: comp__gatewayd

   Runtime sequence covering ``gatewayd`` startup and configuration registration with ``someipd``, local service discovery and event forwarding (Application to ``gatewayd`` to ``someipd``), and remote service offering and event forwarding (``someipd`` to ``gatewayd`` to Application).

   .. uml:: ../models/sequence_diagram_gatewayd.puml
      :align: center
      :caption: Gateway Daemon Startup and Event Forwarding Sequence Diagram

Provided and Internal Component Interfaces
------------------------------------------

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

   - ``LocalServiceInstance::CreateAsyncLocalServices(service_instance_config, service_type_config, socom_runtime, instances)``: Starts asynchronous ``mw::com::GenericProxy::StartFindService`` discovery for a configured local service instance and appends the created ``LocalServiceInstance`` upon discovery.
   - ``LocalServiceInstance::Create(service_instance_config, service_type_config, ipc_proxy, socom_runtime)``: Creates and enables the ``socom::Enabled_server_connector``, looks up event serializers via ``score_com_serializer_get()``, and registers ``GenericProxyEvent`` receive handlers that frame the 16-byte SOME/IP header, serialize application samples, and call ``update_event()``.

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

   - ``RemoteServiceInstance::CreateAsyncRemoteService(service_instance_config, service_type_config, socom_runtime, instances)``: Queries ``score_com_serializer`` for event type size and alignment metadata, constructs a ``mw::com::GenericSkeleton`` for the configured instance specifier, and delegates to ``RemoteServiceInstance::Create()``.
   - ``RemoteServiceInstance::Create(service_instance_config, service_type_config, ipc_skeleton, socom_runtime)``: Offers the ``mw::com::GenericSkeleton`` to local applications, creates a ``socom::Client_connector`` that subscribes to events upon service availability, and forwards incoming SOME/IP payloads to local consumers by stripping the 16-byte SOME/IP header and deserializing into allocated LoLa samples.

Required Interfaces
-------------------

``gatewayd`` consumes the following internal and external interfaces (referenced via ``:uses:`` on :need:`comp__gatewayd`):

- **Gateway IPC Binding (``score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp``)**:

  - ``score::gateway_ipc_binding::mw_com::create_client`` (:need:`real_arc_int_op__gib_arch__create_client`): Instantiates the ``mw::com``-based IPC bridge client, synthesizes ``bridged_ipc/<service_type_name>`` deployments, and sends the FlatBuffers configuration and consumer event slot sizes to ``someipd`` via ``SomeipdService::AddServiceConfiguration``.
  - ``score::gateway_ipc_binding::Gateway_ipc_binding_client::is_connected`` (:need:`real_arc_int_op__gib_arch__is_connected`): Polled during startup to block service instance creation until ``someipd`` has accepted ``AddServiceConfiguration``.

- **SOCom In-Process Runtime and Connectors (``score/socom/``)**:

  - ``score::socom::Runtime::make_client_connector`` (:need:`real_arc_int_op__socom_design__make_client`) and ``score::socom::Runtime::make_server_connector`` (:need:`real_arc_int_op__socom_design__make_server`): Create in-process connectors that couple ``LocalServiceInstance`` and ``RemoteServiceInstance`` to the ``gateway_ipc_binding`` bridge.
  - ``score::socom::Disabled_server_connector::enable`` (:need:`real_arc_int_op__socom_design__enable_srv`): Enables the server connector once a local application service is discovered.
  - ``score::socom::Enabled_server_connector::allocate_event_payload`` (:need:`real_arc_int_op__socom_design__alloc_payload`) and ``update_event`` (:need:`real_arc_int_op__socom_design__update_event`): Allocate zero-copy shared-memory slots and publish framed SOME/IP events to ``someipd``.
  - ``score::socom::Client_connector::subscribe_event`` (:need:`real_arc_int_op__socom_design__sub_event`): Subscribes to remote SOME/IP events when the bridged service becomes available.
  - ``score::socom::Payload::data`` / ``Writable_payload::wdata`` / ``shrink`` (:need:`real_arc_int_op__socom_design__payload_span`): Provide byte-span access for SOME/IP header framing/stripping and payload sizing.

- **Bridged Services Configuration Helper (``score/bridged_services/bridged_service_configs.h``)**:

  - ``score::bridged_services::bridged_service_configs(config, Daemon::gatewayd, event_slot_size)``: Builds the ``Service_configs`` vector mapping local service types to ``Role::provider`` and remote service types to ``Role::consumer``, sizing each event slot via ``event_slot_size``.

- **Serializer Plugin C ABI (``score/serializer/serializer.h``)**:

  - ``score_com_serializer_init`` / ``score_com_serializer_deinit``: Initializes and cleans up the serializer plugin.
  - ``score_com_serializer_get``: Looks up the ``score_com_serializer`` descriptor for a ``(service_type, event_name)`` pair.
  - ``score_com_serializer_get_max_serialized_size`` / ``score_com_serializer_get_sizeof_type`` / ``score_com_serializer_get_alignof_type``: Queries maximum wire size (for IPC slot sizing) and C++ sample size/alignment (for ``mw::com::GenericSkeleton`` creation).
  - ``score_com_serializer_serialize`` / ``score_com_serializer_deserialize``: Converts between in-memory C++ application samples and wire-format SOME/IP payloads.

- **LoLa IPC Middleware (``@score_communication//score/mw/com``)**:

  - ``score::mw::com::runtime::InitializeRuntime``: Initializes the LoLa runtime from ``--service_instance_manifest``.
  - ``score::mw::com::GenericProxy`` / ``GenericProxyEvent``: Discovers local application services (``StartFindService``, ``StopFindService``) and consumes event samples (``Subscribe``, ``SetReceiveHandler``, ``GetNewSamples``).
  - ``score::mw::com::GenericSkeleton`` / ``GenericSkeletonEvent``: Offers remote services locally (``Create``, ``OfferService``) and publishes deserialized event samples (``Allocate``, ``Send``).

- **FlatBuffers Gateway Configuration Schema (``score/config/mw_someip_config_generated.h``)**:

  - ``score::mw_someip_config::GetRoot``: Parses ``ServiceType``, ``ServiceInstance`` (``local_service_instances`` and ``remote_service_instances``), and ``Event`` metadata.
