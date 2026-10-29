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

``comp__gateway_ipc_binding`` is decomposed into units that separate daemon-role asymmetry from symmetric per-service bridging and sample framing:

- ``Client_adapter`` and ``Server_adapter`` implement the public ``Gateway_ipc_binding_client`` and ``Gateway_ipc_binding_server`` interfaces for ``gatewayd`` and ``someipd``, respectively. Both adapters own a collection of ``Service_binding`` instances created via ``make_service_binding()`` and register undeployed bridged service instances with the ``mw::com`` runtime via ``add_to_mw_com_runtime()``. ``Server_adapter`` additionally owns a single-thread ``ThreadPool`` (``ipc_discovery``) to start consumer service discovery off the ``mw::com`` message-passing callback thread after accepting ``AddServiceConfiguration``.
- ``Someipd_service_consumer`` and ``Someipd_service_provider`` isolate the only asymmetric and typed ``mw::com`` contract in the binding (``Someipd_service_interface``). ``Someipd_service_provider`` (in ``someipd``) offers the ``SomeipdService`` skeleton and dispatches incoming ``AddServiceConfiguration`` method calls; ``Someipd_service_consumer`` (in ``gatewayd``) discovers ``SomeipdService`` via ``StartFindService``, sends ``Service_configuration`` on every discovered proxy, and maintains the atomic ``is_connected()`` state.
- ``Provider_service_binding`` and ``Consumer_service_binding`` implement the ``Service_binding`` abstraction for one bridged SOME/IP service instance. Because both ``gatewayd`` and ``someipd`` simultaneously provide some services and consume others over IPC, separating the per-service role (``Role::provider`` vs ``Role::consumer``) from the daemon role (client vs server) eliminates duplication:

  - ``Provider_service_binding`` acts as a local ``socom::Client_connector`` and forwards availability, subscriptions, and allocated event samples to a ``mw::com::GenericSkeleton``.
  - ``Consumer_service_binding`` discovers the peer's ``GenericSkeleton`` via ``mw::com::GenericProxy::StartFindService`` and exposes it locally via a ``socom::Disabled_server_connector`` / ``socom::Enabled_server_connector`` pair.
- ``sample_layout`` encapsulates the 8-byte binding metadata prefix (``payload_length`` + reserved bytes) and 8-byte alignment rounding required to transport variable-length contiguous SOME/IP messages inside fixed-size ``mw::com`` LoLa event slots.
- ``bridged_service_configs`` (in ``score/bridged_services``) translates the FlatBuffers ``mw_someip_config::Root`` configuration and optional ``Event_size_strategy`` into ``Service_configs`` for either ``Daemon::gatewayd`` or ``Daemon::someipd``, keeping FlatBuffers schema parsing decoupled from the ``mw::com`` transport units.

Static View
-----------

.. comp_arc_sta:: Gateway IPC Binding Static Architecture
   :id: comp_arc_sta__gib_arch__static_view
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :belongs_to: comp__gateway_ipc_binding

   Static decomposition of the Gateway IPC Binding into ``Client_adapter``, ``Server_adapter``, ``Someipd_service_consumer``, ``Someipd_service_provider``, ``Provider_service_binding``, ``Consumer_service_binding``, and ``sample_layout``, and their dependencies on ``score::mw::com`` and ``score::socom``.

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

   Dynamic interactions of the Gateway IPC Binding during daemon startup (offering and discovering ``SomeipdService``, registering ``gatewayd`` service configurations and consumer event sizes via ``AddServiceConfiguration``, and starting consumer service discovery on the ``ipc_discovery`` worker thread) and during zero-copy event sample allocation, transmission, and reception across ``mw::com`` (LoLa) and ``score::socom``.

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

   Client-side lifecycle and configuration interface exposed to ``gatewayd`` via ``score/gateway_ipc_binding/gateway_ipc_binding_client.hpp``, ``score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp``, and ``score/bridged_services/bridged_service_configs.h``.

.. real_arc_int_op:: mw_com::create_client and bridged_service_configs
   :id: real_arc_int_op__gib_arch__create_client
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__gib_arch__client

   ``score::bridged_services::bridged_service_configs()`` builds ``Service_configs`` from ``mw_someip_config::Root`` and an ``Event_size_strategy``. ``score::gateway_ipc_binding::mw_com::create_client()`` validates the ``Service_configs``, synthesizes and registers missing ``mw::com`` deployment entries via ``mw::com::runtime::AddConfiguration``, creates and starts all ``Provider_service_binding`` and ``Consumer_service_binding`` instances in the local ``socom::Runtime``, and starts ``Someipd_service_consumer`` discovery to send ``AddServiceConfiguration`` to ``someipd``.

.. real_arc_int_op:: Gateway_ipc_binding_client::is_connected
   :id: real_arc_int_op__gib_arch__is_connected
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__gib_arch__client

   Returns ``true`` while the peer ``someipd`` daemon's ``SomeipdService`` instance is available and has accepted this ``gatewayd``'s ``AddServiceConfiguration`` call, and ``false`` otherwise.

.. real_arc_int:: Gateway IPC Binding Server Interface
   :id: real_arc_int__gib_arch__server
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :language: cpp

   Server-side lifecycle and configuration handler interface exposed to ``someipd`` via ``score/gateway_ipc_binding/gateway_ipc_binding_server.hpp`` and ``score/gateway_ipc_binding/gateway_ipc_binding_mw_com.hpp``.

.. real_arc_int_op:: mw_com::create_server
   :id: real_arc_int_op__gib_arch__create_server
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__gib_arch__server

   Creates a ``Server_adapter`` bound to the local ``socom::Runtime``, the ``SomeipdService`` ``InstanceSpecifier``, and the ``Gateway_config_handler`` callback invoked when a ``gatewayd`` peer registers its configuration.

.. real_arc_int_op:: Gateway_ipc_binding_server::start
   :id: real_arc_int_op__gib_arch__start_server
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__gib_arch__server

   Creates the ``Someipd_service_provider`` skeleton, registers the ``AddServiceConfiguration`` method handler, and offers ``SomeipdService`` over ``mw::com`` so ``gatewayd`` peers can discover ``someipd`` and register their bridged services.

.. real_arc_int:: SomeipdService Control Interface
   :id: real_arc_int__gib_arch__someipd_svc
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :language: cpp

   Typed ``mw::com`` service interface (``Someipd_service_interface`` with service type ``/score/someip/SomeipdService``) used between ``Client_adapter`` (``Someipd_service_consumer``) and ``Server_adapter`` (``Someipd_service_provider``) for peer liveness detection and runtime service configuration registration.

.. real_arc_int_op:: AddServiceConfiguration
   :id: real_arc_int_op__gib_arch__add_svc_cfg
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__gib_arch__someipd_svc

   Transfers a ``Service_configuration`` struct containing the raw FlatBuffers bytes of ``gatewayd``'s ``mw_someip_config`` (up to 64 KiB) and the ``Bridged_event_sizes`` (up to 1024 ``{service_id, event_id, max_payload_size}`` entries) from ``gatewayd`` to ``someipd``, triggering all-or-nothing creation of the corresponding bridged services in ``someipd``.

Required Interfaces
-------------------

``comp__gateway_ipc_binding`` depends on the following internal and external interfaces:

- ``score::socom`` (tracked via ``:uses:`` on :need:`comp__gateway_ipc_binding`):

  - :need:`real_arc_int_op__socom_design__make_client` and :need:`real_arc_int_op__socom_design__make_server` on ``socom::Runtime`` to create intra-process ``Client_connector`` and ``Disabled_server_connector`` endpoints for each bridged service instance.
  - :need:`real_arc_int_op__socom_design__sub_event` on ``socom::Client_connector`` to subscribe and unsubscribe to local events when remote ``mw::com`` receive-handler registrations change.
  - :need:`real_arc_int_op__socom_design__enable_srv`, :need:`real_arc_int_op__socom_design__alloc_payload`, and :need:`real_arc_int_op__socom_design__update_event` on ``socom::Disabled_server_connector`` / ``socom::Enabled_server_connector`` to propagate service availability, allocate sample buffers, and publish incoming ``mw::com`` event samples.
  - :need:`real_arc_int_op__socom_design__payload_span` on ``socom::Payload`` and ``socom::Writable_payload`` to wrap LoLa shared-memory sample buffers behind the 8-byte ``sample_layout`` prefix.

- ``score::mw::com`` (external ``@score_communication//score/mw/com`` library):

  - ``mw::com::GenericSkeleton`` and ``mw::com::GenericSkeletonEvent`` (``Create``, ``OfferService``, ``StopOfferService``, ``Allocate``, ``Send``, ``SetReceiveHandlerRegistrationChangedHandler``) for provider-role bridged service transport.
  - ``mw::com::GenericProxy`` and ``mw::com::GenericProxyEvent`` (``StartFindService``, ``StopFindService``, ``Create``, ``Subscribe``, ``Unsubscribe``, ``SetReceiveHandler``, ``UnsetReceiveHandler``, ``GetNewSamples``) for consumer-role bridged service discovery and reception.
  - ``mw::com::AsSkeleton<Someipd_service_interface>`` and ``mw::com::AsProxy<Someipd_service_interface>`` for offering, discovering, and invoking ``AddServiceConfiguration`` on ``SomeipdService``.
  - ``mw::com::runtime::AddConfiguration`` and ``mw::com::runtime::ResolveInstanceIDs`` for registering synthesized ``bridged_ipc/<service_type_name>`` deployment configurations at runtime.
