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

Gateway IPC Binding on mw::com
==============================

This is the Gateway IPC Binding implementation used by ``gatewayd`` and ``someipd``. It uses
``mw::com`` (LoLa) for service discovery, event transport, and shared-memory lifetime management.

Because ``mw::com`` already provides service discovery, subscription management, shared-memory transport and
slot lifetime management, this implementation has **no IPC protocol of its own**. There are no message ids, no
wire format and no shared-memory slot manager. The entire design question is therefore: *how are SOME/IP
services mapped onto ``mw::com`` features?*

The one thing ``mw::com`` does not give away for free is peer liveness, because per-service availability
cannot distinguish "the peer is down" from "the peer is up but the service is not there". That is solved with
a dedicated, typed ``SomeipdService``, see :ref:`someipd-service`.

Goals and non-goals
-------------------

Goals:

- transport SOCom event updates between ``gatewayd`` and ``someipd`` over ``mw::com``
- keep the SOME/IP header contiguously in front of every event payload so that E2E can be computed over
  header plus payload without an additional copy
- provide a peer-liveness signal through ``is_connected()``
- delete, not reimplement, everything that ``mw::com`` already provides

Non-goals:

- method calls. ``GenericProxy`` and ``GenericSkeleton`` are event-only, so methods stay unimplemented, as
  they are in the current implementation.
- dynamic, runtime-driven service discovery between the two daemons.
- ``mw::com`` interoperability for the ``gatewayd`` to application boundary. That boundary already uses
  ``mw::com`` and is not touched by this design.

Design decisions taken up front
-------------------------------

These decisions are inputs to this design, not conclusions of it. They are restated here because the rest of
the document only makes sense with them in mind.

.. _d1-generic:

D1: ``GenericProxy`` and ``GenericSkeleton`` represent a SOME/IP service
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``GenericProxy`` and ``GenericSkeleton`` treat event payloads as opaque binary data and are configured at
runtime from deployment information instead of from generated IDL code. That is exactly what a gateway
needs: the gateway never interprets the payload of a bridged service, it only moves bytes. One bridged
SOME/IP service instance therefore becomes one ``mw::com`` service instance.

.. _d2-header:

D2: every event sample carries the full SOME/IP message
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

E2E protection is computed over the SOME/IP header and the payload together and requires contiguous memory.
The event samples exchanged between ``gatewayd`` and ``someipd`` therefore carry the full SOME/IP message,
header included. ``score::socom::Payload::data()`` is a read-only view of it; the sender writes it through
``Writable_payload::wdata()``. See :ref:`sample-layout`.

.. _d3-no-slot-manager:

D3: no custom shared-memory slot manager
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The former implementation gave every event of a service the same slot size and put all events of a
service into one shared-memory segment. ``mw::com`` allocates per event, so the sizing is per event as well.
The binding relies on ``mw::com`` for sample allocation and lifetime management. Its factory functions
receive a plain configuration structure that describes the ``GenericSkeletonServiceElementInfo`` required
for each service. See :ref:`public-interface`.

Mapping SOME/IP services to mw::com features
--------------------------------------------

Concept mapping
~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 34 33 33

   * - Gateway concept
     - Former binding
     - ``mw::com`` implementation
   * - Bridged service instance
     - ``Service`` + ``Instance_id`` key, ``Connect_service`` handshake
     - one ``mw::com`` service instance, addressed by ``InstanceSpecifier``
   * - Service interface identity and version
     - ``Service_interface_identifier`` on the wire
     - ``serviceTypes`` entry, generated, see :ref:`bridged-deployment`
   * - Event identity
     - ``socom::Event_id`` (index) on the wire
     - named ``mw::com`` event; the binding maps name to index
   * - Service becomes available
     - ``Offer_service`` message
     - ``GenericSkeleton::OfferService()`` seen via ``StartFindService``
   * - Service goes away
     - ``Offer_service(offered=false)``
     - ``GenericSkeleton::StopOfferService()`` seen via ``StartFindService``
   * - Event subscription propagation
     - ``Subscribe_event`` message
     - ``GenericProxyEvent::Subscribe`` plus
       ``GenericSkeletonEvent::SetReceiveHandlerRegistrationChangedHandler``
   * - Payload buffer allocation
     - ``Shared_memory_slot_manager::allocate_slot``
     - ``GenericSkeletonEvent::Allocate()``
   * - Event transmission
     - ``Event_update`` message plus slot handle
     - ``GenericSkeletonEvent::Send()``
   * - Event reception
     - open peer segment read-only, build payload from slot index
     - ``GenericProxyEvent::SetReceiveHandler`` plus ``GetNewSamples``
   * - Payload lifetime
     - ``Payload_consumed`` message, manual refcount
     - ``SamplePtr`` / ``SampleAllocateePtr`` ownership, handled by LoLa
   * - Slot count and slot size
     - ``Shared_memory_metadata`` sent in ``Connect``
     - ``numberOfSampleSlots`` of the generated deployment, sample size per event, see :ref:`event-sizes`
   * - Peer liveness
     - ``Connect`` / ``Connect_reply`` handshake
     - dedicated typed ``SomeipdService``, see :ref:`someipd-service`
   * - Method calls
     - declared, never dispatched
     - not representable, out of scope

Everything in the middle column that has a counterpart in the right column is deleted, not ported.

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

.. uml:: models/mw_com_service_role_mapping.puml
   :align: center
   :caption: Role assignment for both data directions

Because both daemons hold both roles at the same time, for different services, the bridged-service part of the
implementation is a single symmetric class. The only asymmetry left is the ``SomeipdService`` described next,
and it is what distinguishes the client flavour of the public interface from the server flavour.

.. _someipd-service:

SomeipdService and peer liveness
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The bridged services alone cannot tell ``gatewayd`` whether ``someipd`` is running. A remote service instance
only becomes available once the SOME/IP service behind it is available on the network, so its absence is
ambiguous: it means either "``someipd`` is down" or "``someipd`` is up but the network service is not there".
The former ``Connect`` / ``Connect_reply`` handshake resolved this ambiguity. The current binding
preserves the peer-liveness signal with ``SomeipdService``.

``SomeipdService`` is a dedicated ``mw::com`` service that represents the ``someipd`` daemon itself rather
than any SOME/IP service:

- ``someipd`` provides one ``SomeipdService`` instance. Its presence *is* the liveness signal.
- ``gatewayd`` consumes it with ``StartFindService`` and creates a ``SomeipdServiceProxy`` for it.
- ``Gateway_ipc_binding_client::is_connected()`` returns true once that proxy has been created and ``someipd``
  accepted ``AddServiceConfiguration`` on it. It returns to false when the find-service handler reports the
  instance gone.
- ``gatewayd`` calls ``AddServiceConfiguration`` with its services on every new proxy, so a restarted
  ``someipd`` gets them again. See :ref:`gatewayd-services`.

Properties of this design:

- it is a genuine peer-liveness signal, not a local self-check, so ``gatewayd``'s existing startup wait loop
  keeps working with the same meaning and point in the sequence
- it recovers automatically. If ``someipd`` restarts, the handler fires with an empty container and then again
  with a handle, so ``is_connected()`` goes false and true again. The bridged services re-establish themselves
  independently and need no ordering guarantee relative to it.
- the instance specifier replaces the former ``message_passing`` channel name: it must be
  unique per ``someipd`` on a host, and it is the one piece of configuration ``someipd`` and all its
  ``gatewayd`` instances must agree on.
- every ``gatewayd`` of a ``someipd`` MUST have its own ``mw::com`` ``applicationID``, or its own UID if it
  configures none. LoLa treats a second ``gatewayd`` with the same one as a restart of the first.

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

- ``size`` of the ``DataTypeMetaInfo`` is ``8 + max_payload_size`` rounded up to a multiple of
  8, ``alignment`` is 8. The 8-byte prefix keeps the payload 8-byte aligned; the rounding is
  required because ``mw::com`` rejects a size that is not an integer multiple of its alignment. The padding
  at the end is never addressed, so ``Payload::data()`` is exactly ``max_payload_size`` long.
- the binding builds ``socom::Payload`` and ``socom::Writable_payload`` over the bytes after the prefix, so
  the producing and consuming applications see the whole message in ``data()``, and E2E can run over it.
- ``payload_length`` is the only piece of binding metadata on the link. It is required because LoLa samples
  are fixed size while SOME/IP payloads are not. The producer shrinks its ``Writable_payload``; the binding
  writes ``payload.data().size()`` into the prefix just before ``Send()``; the consumer side reads it back
  and sizes the ``socom::Payload`` accordingly.

Two alternatives were considered and rejected:

- deriving the length from the SOME/IP header length field. That would make ``gateway_ipc_binding`` parse
  SOME/IP, which it deliberately does not do today, and the field is currently not filled in by ``gatewayd``.
- one ``mw::com`` event per length class. That multiplies the deployment configuration and gains nothing.

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
- **Starting service discovery for consumer services**: every ``Role::consumer`` service
  (``Consumer_service_binding``) calls ``GenericProxy::StartFindService`` to discover when the peer daemon's
  ``GenericSkeleton`` offers that service. In ``gatewayd``, ``create_client()`` calls ``StartFindService``
  directly on the startup thread. In ``someipd``, services are created inside the ``AddServiceConfiguration``
  handler on an ``mw::com`` message-passing thread, which must not call ``StartFindService``. ``Server_adapter``
  therefore posts ``StartFindService`` for ``someipd``'s ``Role::consumer`` services (services flowing from
  ``gatewayd`` to ``someipd``) to a dedicated worker thread once the call is accepted. If that fails, the
  binding logs the error and leaves the service unavailable.

Availability propagation
~~~~~~~~~~~~~~~~~~~~~~~~

``Request_service``, ``Offer_service``, ``Connect_service`` and ``Connect_service_reply`` are replaced by
``mw::com`` service discovery:

- **provider side**: the ``GenericSkeleton`` is created eagerly at startup but only offered while the local
  SOCom service is available. ``Client_connector::on_service_state_change(available)`` triggers
  ``OfferService()``, ``not_available`` triggers ``StopOfferService()``.
- **consumer side**: ``GenericProxy::StartFindService`` is started at startup. A non-empty handle container
  triggers ``GenericProxy::Create`` and enabling of the SOCom ``Disabled_server_connector``; an empty
  container disables it again.

The ``GenericSkeleton`` object is **kept alive** across ``StopOfferService``, matching the guidance in the
LoLa gateway documentation: destroying it would zero the shared-memory subscription control block underneath
consumers that still hold a subscription.

Subscription propagation
~~~~~~~~~~~~~~~~~~~~~~~~

``Subscribe_event`` is replaced by a symmetric pair of ``mw::com`` callbacks, so that data is only produced
while somebody actually wants it:

- **consumer side**: SOCom ``on_event_subscription_change(event_id, subscribed)`` maps to
  ``GenericProxyEvent::Subscribe(max_sample_count)`` plus ``SetReceiveHandler``, and to ``UnsetReceiveHandler``
  plus ``Unsubscribe`` on ``not_subscribed``.
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

``Payload_consumed`` and the manual reference counting disappear. LoLa owns the slots:

- **provider side**: ``GenericSkeletonEvent::Allocate()`` returns a ``SampleAllocateePtr<void>``. SOCom only
  hands the binding a ``Writable_payload``, so the binding keeps a small per-event table of in-flight
  allocations, keyed by the ``slot_handle`` it mints and stores in the payload. On
  ``on_event_update(event_id, payload)`` the binding extracts the entry, writes the length prefix and calls
  ``Send(std::move(sample))``. If the producer drops the payload without sending it, the payload destructor
  removes the entry and the ``SampleAllocateePtr`` releases the slot.
- **consumer side**: ``GetNewSamples`` yields a ``SamplePtr<void>`` per sample. The binding moves it into the
  destruction callback of the ``socom::Payload`` it builds. When the consuming application drops the payload,
  the slot is returned to LoLa.

Consequences:

- back pressure is expressed as ``Allocate()`` failing when all slots are held. The binding logs and drops the
  event update when sample allocation fails.
- ``numberOfSampleSlots`` must cover the samples in flight plus the samples the peer holds. The peer holds at
  most ``max_sample_count`` per event, which the binding passes to ``Subscribe``.
- all ``SampleAllocateePtr`` and ``SamplePtr`` instances must be destroyed before the owning skeleton or proxy
  event. The binding tears down in the order: unsubscribe, drain in-flight tables, destroy proxies and
  skeletons.

Architecture
------------

.. uml:: models/component_diagram_gateway_ipc_binding_mw_com.puml
   :align: center
   :caption: Gateway IPC Binding on mw::com, component view

- ``Client_adapter`` and ``Server_adapter`` implement the two public interfaces. Each owns one
  ``Service_binding`` per configured service instance plus one ``Someipd_service_binding``. Only the
  ``Someipd_service_binding`` half and the point in time at which the setup runs differ between them, so
  they share the factory ``make_service_binding()`` instead of a common aggregate class.
- ``Someipd_service_binding`` implements one of the two halves of :ref:`someipd-service`: the provider half
  owns the ``SomeipdServiceSkeleton``, the consumer half owns the ``FindServiceHandle``, the
  ``SomeipdServiceProxy`` and the ``std::atomic<bool>`` behind ``is_connected()``. It is the only place in
  the binding that touches a typed proxy or skeleton.
- ``Provider_service_binding`` holds the ``GenericSkeleton``, its per-event allocation table, and the SOCom
  ``Client_connector``.
- ``Consumer_service_binding`` holds the ``FindServiceHandle``, the ``GenericProxy`` once found, and the SOCom
  ``Disabled_server_connector`` / ``Enabled_server_connector``.
- Setting a bridged service up is all-or-nothing: if any entry of ``Service_configs`` fails,
  ``create_client()`` returns ``nullptr`` instead of leaving a link that silently carries only part of the
  configured services. On the ``someipd`` side ``AddServiceConfiguration`` returns false, see
  :ref:`gatewayd-services`.

What is gone compared to :doc:`index`:

- ``impl/ipc_messages.hpp``, ``impl/gateway_ipc_binding_util.hpp``, ``impl/reply_channel.hpp``,
  ``impl/connections.hpp``, ``impl/pending_connects.hpp``, ``impl/connection_metadata.hpp`` — no protocol
- ``impl/shared_memory_slot_manager.cpp``, ``impl/shared_memory_managers.hpp`` — no shared-memory bookkeeping
- ``Runtime::register_service_bridge`` — with static roles the connectors are created eagerly at startup, so
  there is no on-demand ``request_service`` path to hook into
- the client/server asymmetry for bridged services. What remains of it is ``SomeipdService``: the client
  flavour consumes it, the server flavour provides it.

.. _public-interface:

Public interface
----------------

The public ``Gateway_ipc_binding_client`` and ``Gateway_ipc_binding_server`` interfaces are declared in
``gateway_ipc_binding_client.hpp`` and ``gateway_ipc_binding_server.hpp``. They expose only
implementation-independent lifecycle operations. The ``gateway_ipc_binding_mw_com.hpp`` header adds the
mw::com configuration types and the mw::com-specific free factory functions.

.. _interface-semantics:

Interface semantics under mw::com
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The client and server interfaces expose the binding's lifecycle and connection state:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Member
     - Behaviour
   * - ``Gateway_ipc_binding_client::is_connected()``
     - True while the ``SomeipdServiceProxy`` exists and the peer accepted ``AddServiceConfiguration``, i.e.
       while the peer's binding is up and accepting. See :ref:`someipd-service`. This keeps the meaning and
       the sequencing, so ``gatewayd``'s startup wait loop is unaffected. It can also go back to false,
       when the peer stops.
   * - ``Gateway_ipc_binding_server::start()``
     - Offers ``SomeipdService``. Bridged services are set up later via ``AddServiceConfiguration``, see
       :ref:`gatewayd-services`. Calling ``start()`` twice returns an error.

``is_connected()`` is read from a different thread than the find-service handler that maintains it, so it is
backed by an ``std::atomic<bool>``.

Behavioural views
-----------------

Startup
~~~~~~~

.. uml:: models/mw_com_startup.puml
   :align: center
   :caption: ``someipd`` and ``gatewayd`` startup: ``SomeipdService`` before bridged services

Event flow
~~~~~~~~~~

The flow for one bridged service is split into availability, subscription lifecycle and event delivery.
Availability precedes subscription; event delivery takes place between subscription and unsubscription.

.. uml:: models/mw_com_event_availability.puml
   :align: center
   :caption: Availability for one bridged service

.. uml:: models/mw_com_event_subscription.puml
   :align: center
   :caption: Subscription lifecycle for one bridged service

.. uml:: models/mw_com_event_flow.puml
   :align: center
   :caption: Event delivery for one bridged service

Teardown
~~~~~~~~

Ordering matters because LoLa terminates the process if a ``SampleAllocateePtr`` or ``SamplePtr`` outlives its
event:

1. consumer side: ``UnsetReceiveHandler``, ``Unsubscribe``, then ``StopFindService``
2. destroy the SOCom connectors, which guarantees no further SOCom callback can run
3. drop all payloads still held in the in-flight allocation tables
4. destroy the ``GenericProxy`` and ``GenericSkeleton`` objects

Threading and concurrency
-------------------------

- SOCom callbacks and ``mw::com`` receive handlers run on different threads. Every ``Service_binding`` guards
  its state with a mutex, plus one outer mutex per blocking transition it has to serialize.
- Both stacks call the binding back **while holding an internal lock of their own**, and both take that same
  internal lock from the API the binding calls into. A binding mutex that a callback takes may therefore
  never be held across such a call, or the two lock orders form a cycle and the daemon deadlocks against a
  stack-owned thread. Concretely:

  - ``mw::com`` holds its service discovery lock across the find-service handler, and creating or destroying
    a ``GenericProxy`` takes that same lock. Proxies are therefore constructed before, and destroyed after,
    every binding mutex is released.
  - ``mw::com`` holds its event notification registry lock across the receive-handler-registration handler,
    and ``OfferService()`` / ``StopOfferService()`` take that same lock. The provider offers under
    ``m_offer_mutex``, which that handler does not take.
  - SOCom holds connector locks across the subscription callbacks, and ``enable()`` / ``disable()`` take
    those same locks. The consumer enables and disables under ``m_lifecycle_mutex``, which no callback takes.

  The outer mutexes exist purely to keep those transitions serialized against each other while the inner
  ones are released.
- Neither a SOCom callback nor a ``mw::com`` handler may block. The binding does no I/O and no allocation
  beyond the in-flight table inside them. The ``AddServiceConfiguration`` handler is the exception: it sets
  up services, but never waits for a reply, see :ref:`gatewayd-services`.
- ``GetNewSamples`` must not run concurrently for the same ``GenericProxyEvent``. Because the only caller is
  the receive handler registered for that event, and ``mw::com`` serialises those, this holds without extra
  locking; the mutex is still taken for the SOCom-visible state.
- The SOCom deadlock detector terminates the process if a connector is destroyed from inside one of its own
  callbacks. Teardown therefore never runs from a callback; the ``mw::com`` find-service handler schedules
  the disable and destroys the connector outside the handler, exactly as the current implementation does for
  ``Connect_service_reply``.

Configuration
-------------

Both daemons need a ``mw_com_config.json``. ``someipd`` needs one for the first time, and has to call
``InitializeRuntime``. Of the ``gatewayd`` / ``someipd`` link it holds only the entry for
:ref:`someipd-service`. Both files must spell it identically. It replaces the shared ``ipc_channel_name``
command line option:

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

Bridged services are not listed in ``mw_com_config.json``. Instead, each daemon builds its ``Service_configs``
from ``mw_someip_config`` using ``bridged_service_configs()`` in ``score/bridged_services``: ``gatewayd``
builds them from its own file, and ``someipd`` builds them from the bytes each ``gatewayd`` sends, see
:ref:`gatewayd-services`. The binding then synthesizes their ``serviceTypes`` and ``serviceInstances`` entries
and registers them at runtime with ``mw::com::runtime::AddConfiguration``:

- **Naming**: both ``serviceTypeName`` and ``instanceSpecifier`` are ``bridged_ipc/<service_type_name>`` (with
  version ``1.0``), so they cannot collide with the ``gatewayd``-to-application instances.
- **IDs**: ``serviceId`` and ``eventId`` are taken directly from the SOME/IP ``service_id`` and ``event_id`` in
  ``mw_someip_config``.
- **Instance ID**: ``instanceId`` is ``ipc_instance_id`` from ``mw_someip_config``, which defaults to ``0xFFFF``
  (the SOME/IP "any instance" ID) to avoid colliding with local application instance IDs. All ``gatewayd``
  instances of the same ``someipd`` MUST use the same ``ipc_instance_id``; a host running more than one
  ``someipd`` needs a distinct value per ``someipd``.
- **Sample slots and subscribers**: ``numberOfSampleSlots`` is ``kNumber_of_sample_slots`` (20, see
  :ref:`sample-layout`) and ``maxSubscribers`` is ``1``, because only the single peer daemon subscribes.
- **ASIL level**: ``asil-level`` is ``"QM"``, because ``someipd`` is a QM process and ``mw::com`` requires both
  ends of a shared-memory instance to use ``"QM"`` when either peer is QM.
- **Validation and existing entries**: ``mw::com::runtime::AddConfiguration`` terminates the process on a
  duplicate or malformed entry. ``add_to_mw_com_runtime()`` therefore validates every ``Service_config`` first,
  checks ``runtime::ResolveInstanceIDs``, keeps any ``instanceSpecifier`` already present in
  ``mw_com_config.json``, and adds only the missing ones.

Event sample sizes are not part of the JSON deployment; they come from ``Service_config`` (see
:ref:`event-sizes`) and are passed to ``GenericSkeleton::Create``. On the provider side, the binding checks
that ``GenericSkeletonEvent::GetSizeInfo().size`` matches ``sample_size(event)`` and fails setup on mismatch.

Known gaps
----------

- **method calls on bridged services** are not representable, because ``GenericProxy`` and ``GenericSkeleton``
  are event-only. They are declared but never dispatched today, so this is not a regression, but it does close
  the door on the incremental path that the current ``Connect`` shared-memory configuration was designed for.
  Note that this limitation applies to *bridged* services only; ``SomeipdService`` is typed and can carry
  methods, see :ref:`someipd-service`.
- **requested event updates**: SOCom ``on_event_update_request`` has no LoLa counterpart. LoLa has no pull
  API on the proxy side, so field-style initial values cannot be served on demand. The request is logged and
  ignored.
- **re-offering a provider service**: if the local SOCom service of a provider-role bridge goes away and
  comes back while a peer still holds an event subscription, ``Provider_service_binding`` keeps the
  ``GenericSkeleton`` alive across ``StopOfferService()``/``OfferService()``, as the LoLa gateway
  documentation demands, because destroying it would zero the shared-memory subscription control block
  underneath that peer. Re-offering the same skeleton in that case still aborts inside LoLa (``PrepareOffer()``
  calls ``RemoveStaleArtefacts()`` on a region the skeleton itself still owns), so that case has to be fixed
  upstream rather than worked around here.
  When no peer holds a subscription, the binding instead fully destroys and recreates the
  ``GenericSkeleton`` on the next offer, which LoLa's own stale-shared-memory detection (an flock on a
  per-instance usage marker file) supports. A restart of the *providing application* while a peer is
  actively subscribed is therefore still not covered.
- **changing an event size**: requires restarting both daemons. ``someipd`` rejects a modified configuration
  from a restarted ``gatewayd``, and LoLa reuses a provider's existing shared-memory segment while a consumer
  still holds it open.

The former ``message_passing`` implementation is not retained behind a switch; the daemons use this
``mw::com`` implementation directly.
