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
################################

Overview
========

The Gateway IPC Binding (:need:`comp__gateway_ipc_binding`) connects ``gatewayd`` and ``someipd`` over
``mw::com`` (LoLa). It maps bridged SOME/IP services directly onto ``mw::com`` service discovery,
subscription management, event transport, and shared-memory sample management:

- Transports ``socom`` event updates between ``gatewayd`` and ``someipd`` over ``mw::com`` shared memory.
- Reserves an 8-byte binding prefix at the front of each shared-memory sample slot and exposes the remaining
  bytes as a contiguous span so ``gatewayd`` and ``someipd`` keep the 16-byte SOME/IP header and payload
  contiguous for E2E protection without copying.
- Reports daemon connectivity through ``is_connected()`` and registers ``gatewayd`` service configurations
  at runtime via ``SomeipdService`` (see :ref:`someipd-service`).

Description
===========

Rationale Behind Architecture Decomposition
-------------------------------------------

The binding separates daemon-level lifecycle and control signaling from per-service event transport:

- **Transport-independent daemon API** (``Gateway_ipc_binding_client`` and ``Gateway_ipc_binding_server``):
  ``Client_adapter`` and ``Server_adapter`` expose only ``is_connected()`` and ``start()``, keeping
  ``gatewayd`` and ``someipd`` independent of ``mw::com`` types outside the ``create_client()`` and
  ``create_server()`` setup calls.
- **Typed control service vs generic service bindings** (``SomeipdService`` vs ``Service_binding``): Bridged
  SOME/IP services use type-erased ``GenericSkeleton`` and ``GenericProxy`` instances, which do not support
  methods and are only offered while a service is active. A dedicated typed ``SomeipdService``
  (``Someipd_service_provider`` and ``Someipd_service_consumer``) therefore handles daemon liveness and
  runtime configuration registration (``AddServiceConfiguration``), while both daemons reuse
  ``Provider_service_binding``, ``Consumer_service_binding``, and ``sample_layout`` via
  ``make_service_bindings()`` for event transport.
- **Configuration schema isolation** (``score/bridged_services``): Translating ``mw_someip_config`` into
  ``Service_configs`` lives in ``score/bridged_services`` outside ``comp__gateway_ipc_binding``, keeping the
  IPC binding independent of the FlatBuffers gateway configuration schema.
- **All-or-nothing registration**: If any entry of ``Service_configs`` fails setup, ``create_client()`` returns
  ``nullptr`` and ``AddServiceConfiguration`` rolls back and returns ``false`` (see :ref:`gatewayd-services`),
  so partial service configurations never go live.

.. _d1-generic:

Service representation
----------------------

``GenericProxy`` and ``GenericSkeleton`` treat event payloads as opaque binary data and are configured at
runtime from deployment information instead of from generated IDL code. That is exactly what a gateway
needs: the binding forwards bridged event payloads as opaque byte spans. One bridged
SOME/IP service instance therefore becomes one ``mw::com`` service instance.

.. _d2-header:

Event sample contents
---------------------

E2E protection is computed over the SOME/IP header and the payload together and requires contiguous memory.
The event samples exchanged between ``gatewayd`` and ``someipd`` therefore carry the full SOME/IP message,
header included. ``score::socom::Payload::data()`` is a read-only view of it; the sender writes it through
``Writable_payload::wdata()``. See :ref:`sample-layout`.

.. _d3-no-slot-manager:

Sample allocation and lifetime
------------------------------

``mw::com`` allocates samples per event and manages their lifetime. The binding's factory functions
receive a configuration structure that describes the ``GenericSkeletonServiceElementInfo`` required
for each service. See :ref:`public-interface`.

Mapping SOME/IP services to mw::com features
--------------------------------------------

Service mapping
~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Gateway concept
     - ``mw::com`` design
   * - Bridged service instance
     - one ``mw::com`` service instance, addressed by ``InstanceSpecifier``
   * - Service interface identity and version
     - ``serviceTypes`` entry, generated, see :ref:`bridged-deployment`
   * - Event identity
     - named ``mw::com`` event; the binding maps name to index
   * - Service becomes available
     - ``GenericSkeleton::OfferService()`` seen via ``StartFindService``
   * - Service goes away
     - ``GenericSkeleton::StopOfferService()`` seen via ``StartFindService``
   * - Event subscription propagation
     - ``GenericProxyEvent::Subscribe`` plus
       ``GenericSkeletonEvent::SetReceiveHandlerRegistrationChangedHandler``
   * - Payload buffer allocation
     - ``GenericSkeletonEvent::Allocate()``
   * - Event transmission
     - ``GenericSkeletonEvent::Send()``
   * - Event reception
     - ``GenericProxyEvent::SetReceiveHandler`` plus ``GetNewSamples``
   * - Payload lifetime
     - ``SamplePtr`` / ``SampleAllocateePtr`` ownership is handled by LoLa
   * - Slot count and slot size
     - ``numberOfSampleSlots`` of the generated deployment, sample size per event, see :ref:`event-sizes`
   * - Daemon connectivity
     - dedicated typed ``SomeipdService``, see :ref:`someipd-service`
   * - Method calls
     - not supported on bridged services (see :ref:`limitations`)

Role assignment
~~~~~~~~~~~~~~~

A bridged service always has a data source and a data sink. The binding peer on the **source** side owns the
``GenericSkeleton``, the binding peer on the **sink** side owns the ``GenericProxy``.

.. list-table::
   :header-rows: 1
   :widths: 24 19 19 19 19

   * - Service kind
     - Data direction
     - ``gatewayd`` mw::com role
     - ``someipd`` mw::com role
     - Binding SOCom role on the provider side
   * - local service instance (application provides, network consumes)
     - application to network
     - provider, ``GenericSkeleton``
     - consumer, ``GenericProxy``
     - ``Client_connector`` in ``gatewayd``
   * - remote service instance (network provides, application consumes)
     - network to application
     - consumer, ``GenericProxy``
     - provider, ``GenericSkeleton``
     - ``Client_connector`` in ``someipd``

Read as a rule:

- **provider role**: the binding is a SOCom ``Client_connector`` of the locally offered SOCom service and
  pushes what it consumes into a ``GenericSkeleton``.
- **consumer role**: the binding is a SOCom ``Server_connector`` for the same service and publishes what it
  receives from a ``GenericProxy``.

.. uml:: ../models/mw_com_service_role_mapping.puml
   :align: center
   :caption: Role assignment for both data directions

Because both daemons hold both roles at the same time, for different services, both adapters share the same
``Service_binding`` implementations (``Provider_service_binding`` and ``Consumer_service_binding``). The only
asymmetry left is the ``SomeipdService`` described next, and it is what distinguishes the client flavour of the
public interface from the server flavour.

.. _someipd-service:

SomeipdService and daemon connectivity
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The bridged services alone cannot tell ``gatewayd`` whether ``someipd`` is running. A remote service instance
only becomes available once the SOME/IP service behind it is available on the network, so its absence is
ambiguous: it means either "``someipd`` is down" or "``someipd`` is up but the network service is not there".
The binding resolves this ambiguity with ``SomeipdService``.

``SomeipdService`` is a dedicated ``mw::com`` service that represents the ``someipd`` daemon itself rather
than any SOME/IP service:

- ``someipd`` provides one ``SomeipdService`` instance. Its presence signals that ``someipd`` is running and
  ready to accept service configurations.
- ``gatewayd`` consumes it with ``StartFindService`` and creates a ``Someipd_service_proxy`` for it.
- ``Gateway_ipc_binding_client::is_connected()`` returns true once that proxy has been created and ``someipd``
  accepted ``AddServiceConfiguration`` on it. It returns to false when the find-service handler reports the
  instance gone.
- ``gatewayd`` calls ``AddServiceConfiguration`` with its services on every new proxy, so a restarted
  ``someipd`` gets them again. See :ref:`gatewayd-services`.

Properties of this design:

- **Actual daemon connectivity**: Reflects whether ``someipd`` is running and has accepted ``gatewayd``'s
  configuration rather than a local self-check, so ``gatewayd``'s startup wait loop proceeds only once
  ``someipd`` is ready.
- **Automatic recovery**: If ``someipd`` restarts, the find-service handler fires with an empty container and
  then again with a handle, so ``is_connected()`` transitions to ``false`` and back to ``true``. The bridged
  services re-establish themselves independently and need no ordering guarantee relative to
  ``SomeipdService``.
- **Unique instance specifier per daemon**: The ``SomeipdService`` instance specifier must be unique per
  ``someipd`` on a host, and it is the one piece of configuration ``someipd`` and all its ``gatewayd``
  instances must agree on.
- **Distinct application identity per client**: Every ``gatewayd`` connected to a ``someipd`` MUST have its
  own ``mw::com`` ``applicationID``, or its own UID if it configures none, because LoLa treats a second
  ``gatewayd`` with the same identity as a restart of the first.

.. _someipd-service-ordering:

Offering order
^^^^^^^^^^^^^^

``someipd`` offers ``SomeipdService`` **first**. It has no bridged services until a ``gatewayd`` calls
``AddServiceConfiguration``. ``is_connected()`` turns true only once ``someipd`` accepted the call, so
``is_connected() == true`` means "the peer's binding is up, accepting, and has the services of this
``gatewayd``", not "the peer has finished setting up its services". Bridged services arrive asynchronously
afterwards, each signalled through the normal SOCom service state.

.. _sample-layout:

Event sample layout
~~~~~~~~~~~~~~~~~~~

A ``mw::com`` event sample is a fixed-size, type-erased buffer described by ``DataTypeMetaInfo{size, alignment}``.
SOME/IP messages are variable length and, per :ref:`d2-header`, travel whole. The binding therefore defines
the following layout for every event sample it exchanges::

   +----------------------+-----------------------------+
   | binding prefix (8 B) | payload bytes               |
   |  u32 payload_length  |  up to max_payload_size,    |
   |  u32 reserved        |  e.g. a SOME/IP message     |
   +----------------------+-----------------------------+
   ^                      ^
   sample base            Payload::data()

- **Size and alignment**: ``size`` of the ``DataTypeMetaInfo`` is ``8 + max_payload_size`` rounded up to a
  multiple of 8, and ``alignment`` is 8. The 8-byte prefix keeps the payload 8-byte aligned; the rounding is
  required because ``mw::com`` rejects a size that is not an integer multiple of its alignment. The padding
  at the end is never addressed, so ``Writable_payload::wdata()`` starts at ``max_payload_size`` bytes and
  ``Payload::data()`` spans ``payload_length`` bytes (up to ``max_payload_size``).
- **Zero-copy payload views**: The binding builds ``socom::Payload`` and ``socom::Writable_payload`` over the
  bytes after the prefix, so the producing and consuming applications see the whole SOME/IP message in
  ``data()`` and E2E protection can run over it in place.
- **Length framing**: ``payload_length`` is the only piece of binding metadata on the link. It is required
  because LoLa samples are fixed size while SOME/IP payloads are not. The producer shrinks its
  ``Writable_payload``; the binding writes ``payload.data().size()`` into the prefix just before ``Send()``;
  the consumer side reads it back and sizes the ``socom::Payload`` accordingly.

.. _event-sizes:

Event sizes
~~~~~~~~~~~

Only ``gatewayd`` knows the event sizes (serialized payload plus SOME/IP header), from its generated
serializers. ``someipd`` is type-agnostic and configures ``max_payload_size`` as 0. In ``mw::com``, the
``Role::provider`` side allocates the shared-memory sample slots when it creates the ``GenericSkeleton``, so
the provider needs the size up front. How ``someipd`` gets the size depends on which daemon provides the
bridged service:

- **gatewayd provides, someipd consumes**: ``gatewayd`` creates the ``GenericSkeleton`` with its configured
  size. When ``someipd``'s ``Consumer_service_binding`` attaches to the peer, it reads the sample size from
  ``GenericProxyEvent::GetDataTypeSizeInfo().Size()`` and subtracts ``kSample_prefix_size``. On any consumer, if
  the peer's sample size is smaller than the configured ``max_payload_size``, the binding logs an error and
  leaves the service unavailable.
- **someipd provides, gatewayd consumes**: ``someipd`` must size its ``GenericSkeleton`` before ``gatewayd``
  attaches to it. ``gatewayd`` therefore sends ``{service_id, event_id, max_payload_size}`` for every event of
  its ``Role::consumer`` services in ``AddServiceConfiguration`` (at most 1024 events, in any order). Those
  services are ``Role::provider`` services in ``someipd``. The call MUST supply one size for every provider
  event it adds to ``someipd``, with no missing, unknown, or duplicate events. Otherwise ``someipd`` rejects
  the call and logs an error.

.. _gatewayd-services:

Registering gatewayd services with someipd
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``someipd`` starts with no service configuration of its own. Instead, each ``gatewayd`` registers its services
at runtime via ``AddServiceConfiguration``:

- **Payload**: ``gatewayd`` sends the raw FlatBuffers bytes of its ``mw_someip_config`` (up to 64 KiB) together
  with its consumer event sizes in a single fixed-size, trivially copyable ``AddServiceConfiguration`` call.
  ``gatewayd`` fails at startup if its configuration exceeds 64 KiB.
- **Setup in someipd**: the IPC binding treats the configuration bytes as opaque and passes them to ``someipd``'s
  handler. ``someipd`` validates the buffer with the FlatBuffers ``Verifier``, builds the IPC bridged services
  via ``bridged_service_configs()``, and creates its SOME/IP network services from the same buffer, keeping the
  buffer alive for as long as the services exist.
- **All-or-nothing rollback**: if any step fails, ``someipd`` destroys the service bindings created during the
  call and returns ``false``. ``gatewayd`` logs the failure and stays disconnected. Any ``mw::com`` deployment
  already registered with ``runtime::AddConfiguration`` stays in ``someipd`` until ``someipd`` restarts, because
  ``mw::com`` cannot remove a configuration.
- **Restarts and ownership**: ``someipd`` keeps a ``gatewayd``'s services alive when that ``gatewayd`` stops.
  When a restarted ``gatewayd`` reconnects and sends the exact same configuration bytes and event sizes,
  ``someipd`` accepts the call immediately without recreating the services. Any *different* call that reuses a
  ``service_id`` from an already-accepted call is rejected: each service belongs to one ``gatewayd`` and cannot
  change while ``someipd`` runs.
- **Starting service discovery for consumer services**: every ``Role::consumer`` service starts ``GenericProxy``
  discovery to detect when the peer daemon offers its ``GenericSkeleton``. In ``gatewayd``, consumer discovery
  starts during ``create_client()``, while in ``someipd`` ``Server_adapter`` dispatches ``start()`` onto its
  ``ipc_discovery`` worker thread after accepting ``AddServiceConfiguration`` because ``StartFindService`` must
  not be called from an ``mw::com`` message-passing callback thread.

Availability propagation
~~~~~~~~~~~~~~~~~~~~~~~~

Availability is managed through ``mw::com`` service discovery:

- **provider side**: the ``GenericSkeleton`` is created eagerly at startup but only offered while the local
  SOCom service is available. ``Client_connector::on_service_state_change(available)`` triggers
  ``OfferService()``, ``not_available`` triggers ``StopOfferService()``.
- **consumer side**: ``GenericProxy::StartFindService`` is started at startup. A non-empty handle container
  triggers ``GenericProxy::Create`` and enabling of the SOCom ``Disabled_server_connector``; an empty
  container disables it again.

When the local SOCom service becomes unavailable, ``Provider_service_binding`` calls ``StopOfferService()``;
if no peer holds a subscription, it also destroys the ``GenericSkeleton`` and recreates it on the next offer,
whereas if a peer still holds a subscription, the ``GenericSkeleton`` is kept alive so the shared-memory
subscription state remains valid.

Subscription propagation
~~~~~~~~~~~~~~~~~~~~~~~~

Subscription is propagated through a symmetric pair of ``mw::com`` callbacks, so that data is only produced
while somebody actually wants it:

- **consumer side**: SOCom ``on_event_subscription_change(event_id, subscribed)`` maps to
  ``GenericProxyEvent::Subscribe(max_sample_count)`` plus ``SetReceiveHandler``, and to ``UnsetReceiveHandler``
  plus ``Unsubscribe`` on ``unsubscribed``.
- **provider side**: ``GenericSkeletonEvent::SetReceiveHandlerRegistrationChangedHandler`` fires when the
  first remote receive handler is registered or the last one is removed. It maps to
  ``Client_connector::subscribe_event(event_id, Event_mode::update)`` and ``unsubscribe_event``.

This callback exists on ``GenericSkeletonEvent`` only, and was added upstream for exactly this gateway use
case.

The two events can race: the remote receive handler may be registered before the local SOCom service is
available. The provider side therefore keeps a per-event desired-subscription flag and reconciles it whenever
either input changes, rather than calling SOCom directly from the ``mw::com`` callback.

Payload lifetime and flow control
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

LoLa owns the shared-memory sample slots:

- **provider side**: ``Provider_service_binding::on_event_payload_allocate()`` allocates a LoLa sample slot and
  stores the ``SampleAllocateePtr<void>`` in ``Allocations`` keyed by its ``slot_handle`` until
  ``on_event_update()`` writes the payload-length prefix and sends it, or releases the slot back to LoLa if the
  ``Writable_payload`` is dropped unsent.
- **consumer side**: ``GetNewSamples`` yields a ``SamplePtr<void>`` per sample. The binding moves it into the
  release callback of the ``socom::Payload`` it builds. When the consuming application destroys the payload,
  the ``SamplePtr<void>`` is destroyed and the slot is returned to LoLa.

Slot exhaustion and sizing:

- If all sample slots of an event are currently in use, ``GenericSkeletonEvent::Allocate()`` fails and
  ``on_event_payload_allocate()`` logs a warning and returns ``runtime_error_sample_allocation_failed``.
- ``numberOfSampleSlots`` (``kNumber_of_sample_slots = 20``) must cover both allocated samples awaiting
  ``Send()`` on the provider side and received samples still held by the consumer (up to
  ``max_sample_count`` per event, passed to ``Subscribe``).

Configuration
-------------

Both daemons need a ``mw_com_config.json`` and must call ``InitializeRuntime``. The configuration for the
``gatewayd`` / ``someipd`` link contains only the entry for :ref:`someipd-service`; both files must spell it
identically:

.. code-block:: json

   {
     "serviceTypes": [
       {
         "serviceTypeName": "/score/someip/SomeipdService",
         "version": { "major": 1, "minor": 0 },
         "bindings": [ {
           "binding": "SHM",
           "serviceId": 6400,
           "methods": [ { "methodName": "AddServiceConfiguration", "methodId": 1 } ]
         } ]
       }
     ],
     "serviceInstances": [
       {
         "instanceSpecifier": "someipd/daemon",
         "serviceTypeName": "/score/someip/SomeipdService",
         "version": { "major": 1, "minor": 0 },
         "instances": [ {
           "instanceId": 1,
           "asil-level": "QM",
           "binding": "SHM",
           "methods": [ { "methodName": "AddServiceConfiguration", "queueSize": 1 } ]
         } ]
       }
     ]
   }

.. _bridged-deployment:

Bridged deployment
~~~~~~~~~~~~~~~~~~

Each daemon builds its ``Service_configs`` from ``mw_someip_config`` using ``bridged_service_configs()`` in
``score/bridged_services``: ``gatewayd`` builds them from its own configuration file, and ``someipd`` builds
them from the configuration bytes received in ``AddServiceConfiguration`` (see :ref:`gatewayd-services`).
``add_to_mw_com_runtime()`` then synthesizes the ``mw::com`` SHM ``serviceTypes`` and ``serviceInstances``
deployment JSON in memory for any service instance not already present in ``mw_com_config.json`` and
registers it via ``mw::com::runtime::AddConfiguration``:

- **Naming**: both ``serviceTypeName`` and ``instanceSpecifier`` are ``bridged_ipc/<service_type_name>`` (with
  version ``1.0``), keeping bridged instances distinct from ``gatewayd``-to-application instances.
- **IDs**: ``serviceId`` and ``eventId`` are taken directly from the SOME/IP ``service_id`` and ``event_id`` in
  ``mw_someip_config``.
- **Instance ID**: ``instanceId`` is ``ipc_instance_id`` from ``mw_someip_config``, which defaults to ``0xFFFF``
  (the SOME/IP "any instance" ID) to avoid colliding with local application instance IDs. All ``gatewayd``
  instances of the same ``someipd`` MUST use the same ``ipc_instance_id``; a host running more than one
  ``someipd`` needs a distinct value per ``someipd``.
- **Sample slots and subscribers**: ``numberOfSampleSlots`` is ``kNumber_of_sample_slots`` (20, see
  :ref:`sample-layout`) and ``maxSubscribers`` is ``1`` for the single peer daemon.
- **ASIL level**: ``asil-level`` is ``"QM"``, because ``someipd`` is a QM process and ``mw::com`` requires both
  ends of a shared-memory instance to use ``"QM"`` when either peer is QM.

.. _limitations:

Limitations
-----------

- **Method calls**: Bridged services support events only (``mw::com::GenericProxy`` and ``GenericSkeleton`` do
  not support methods). ``SomeipdService`` is a typed ``mw::com`` service and supports the
  ``AddServiceConfiguration`` method (see :ref:`someipd-service`).
- **Requested event updates**: On-demand event update requests (``socom`` ``on_event_update_request``) are
  logged and ignored because the IPC bridge uses push-only ``mw::com`` event notifications.
- **Re-offering a provider service**: If a local SOCom service stops and is re-offered while no peer holds an
  event subscription, ``Provider_service_binding`` destroys and recreates the ``GenericSkeleton``. However,
  restarting a provider while a peer actively holds an event subscription is not currently supported due to an
  upstream ``mw::com`` limitation when re-offering an existing shared-memory skeleton with active subscribers.
- **Changing an event size**: Modifying a registered service configuration or event payload size requires
  restarting both ``gatewayd`` and ``someipd``, because ``someipd`` rejects modified configurations for an
  already-registered ``service_id`` and ``mw::com`` shared-memory segments have a fixed slot size once created.

Static Architecture
===================

.. comp_arc_sta:: Gateway IPC Binding Static Architecture
   :id: comp_arc_sta__gib_arch__static_view
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :belongs_to: comp__gateway_ipc_binding

   Component structure of ``gateway_ipc_binding_mw_com`` and its dependencies on ``mw::com`` and ``socom``.

   .. uml:: ../models/component_diagram_gateway_ipc_binding_mw_com.puml
      :align: center
      :caption: Gateway IPC Binding on mw::com, component view

Dynamic Architecture
====================

.. comp_arc_dyn:: Gateway IPC Binding Startup and Event Flow
   :id: comp_arc_dyn__gib_arch__startup_and_events
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :belongs_to: comp__gateway_ipc_binding

   Runtime sequences for ``SomeipdService`` registration, service availability, subscription propagation, and zero-copy event delivery.

   .. uml:: ../models/mw_com_startup.puml
      :align: center
      :caption: ``someipd`` and ``gatewayd`` startup: ``SomeipdService`` before bridged services

   .. uml:: ../models/mw_com_event_availability.puml
      :align: center
      :caption: Availability for one bridged service

   .. uml:: ../models/mw_com_event_subscription.puml
      :align: center
      :caption: Subscription lifecycle for one bridged service

   .. uml:: ../models/mw_com_event_flow.puml
      :align: center
      :caption: Event delivery for one bridged service

Teardown
--------

On destruction, each service binding tears down its endpoints in order so no callback or shared-memory sample outlives its owner:

- ``Provider_service_binding``: Unsets ``GenericSkeletonEvent`` receive-handler registration callbacks, resets the ``socom::Client_connector``, clears ``Allocations`` (before skeleton destruction, as LoLa aborts if a ``SampleAllocateePtr`` outlives its event), and stops and destroys the ``GenericSkeleton``.
- ``Consumer_service_binding``: Calls ``GenericProxy::StopFindService``, disables the ``socom`` server connector, unsets receive handlers and unsubscribes active ``GenericProxyEvent`` instances, and destroys the ``GenericProxy`` and disabled connector.

Threading and concurrency
-------------------------

``socom`` and ``mw::com`` callbacks run on arbitrary caller and message-passing threads:

- **Lock ordering across callbacks**: ``Provider_service_binding`` orders ``m_offer_mutex`` outside ``m_mutex`` (with a separate mutex in ``Allocations``), and ``Consumer_service_binding`` orders ``m_lifecycle_mutex`` -> ``m_subscription_mutex`` -> ``m_mutex`` so ``mw::com`` and ``socom`` calls that acquire internal framework locks or wait for in-flight callbacks never deadlock with ``m_mutex``. ``mw::com`` proxies and skeletons are constructed and destroyed outside ``m_mutex``.
- **Daemon registration and discovery**: ``Server_adapter`` serializes ``AddServiceConfiguration`` calls with ``m_mutex`` and posts ``start()`` onto its single-thread ``ipc_discovery`` ``ThreadPool``. ``Someipd_service_consumer`` guards its proxy and find handle with ``m_mutex`` and stores ``is_connected()`` in a ``std::atomic<bool>``.

.. _public-interface:

Interfaces
==========

The public API separates transport-independent lifecycle interfaces from ``mw::com``-specific configuration
types and factory functions across three headers:

- ``gateway_ipc_binding_client.hpp`` (``Gateway_ipc_binding_client``): Client-side lifecycle interface
  exposing ``is_connected()`` to ``gatewayd``.
- ``gateway_ipc_binding_server.hpp`` (``Gateway_ipc_binding_server``): Server-side lifecycle interface
  exposing ``start()`` to ``someipd``.
- ``gateway_ipc_binding_mw_com.hpp``: ``mw::com`` configuration structures (``Event_config``,
  ``Service_config``, ``Service_configs``), the ``Gateway_config_handler`` and ``Add_bridged_services``
  callbacks, and the ``mw_com::create_client()`` and ``mw_com::create_server()`` factory functions.

.. _interface-semantics:

Interface semantics under mw::com
---------------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Member
     - Behaviour
   * - ``Gateway_ipc_binding_client::is_connected()``
     - True while the ``Someipd_service_proxy`` exists and the peer accepted ``AddServiceConfiguration``, i.e.
       while the peer's binding is up and accepting. See :ref:`someipd-service`. It can return to false when
       the peer stops.
   * - ``Gateway_ipc_binding_server::start()``
     - Offers ``SomeipdService``. Bridged services are set up later via ``AddServiceConfiguration``, see
       :ref:`gatewayd-services`. Calling ``start()`` twice returns an error.

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

   Typed ``mw::com`` control service (``/score/someip/SomeipdService``) used between ``gatewayd`` and ``someipd`` for daemon connectivity detection and runtime configuration registration.

.. real_arc_int_op:: AddServiceConfiguration
   :id: real_arc_int_op__gib_arch__add_svc_cfg
   :version: 1
   :security: NO
   :safety: QM
   :status: valid
   :included_by: real_arc_int__gib_arch__someipd_svc

   Transfers ``gatewayd``'s gateway configuration bytes and consumer event payload sizes to ``someipd`` for all-or-nothing creation of the corresponding bridged services.
