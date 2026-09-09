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

This document describes an **alternative implementation** of the Gateway IPC Binding that replaces the
``score::message_passing`` control channel and the hand-rolled shared-memory transport with ``mw::com``
(LoLa).

It is developed side by side with the existing implementation described in :doc:`index`:

- it implements the **same public interfaces** ``Gateway_ipc_binding_client`` and ``Gateway_ipc_binding_server``
- it is constructed through **new factory functions**, not through the existing ``create()`` methods
- it is selected by a **Bazel feature flag** and is not built into the product by default until it is ready

Because ``mw::com`` already provides service discovery, subscription management, shared-memory transport and
slot lifetime management, this implementation has **no IPC protocol of its own**. There are no message ids, no
wire format and no shared-memory slot manager. The entire design question is therefore: *how are SOME/IP
services mapped onto ``mw::com`` features?*

Goals and non-goals
-------------------

Goals:

- transport SOCom event updates between ``gatewayd`` and ``someipd`` over ``mw::com``
- keep the SOME/IP header contiguously in front of every event payload so that E2E can be computed over
  header plus payload without an additional copy
- keep the public interface of the component unchanged so that the daemons can be switched over by
  exchanging a single factory call
- delete, not reimplement, everything that ``mw::com`` already provides

Non-goals:

- method calls. ``GenericProxy`` and ``GenericSkeleton`` are event-only, so methods stay unimplemented, as
  they are today.
- dynamic, runtime-driven service discovery between the two daemons. See decision :ref:`d4-static-config`.
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

D2: every event sample reserves space for the SOME/IP header
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

E2E protection is computed over the SOME/IP header and the payload together and requires contiguous memory.
The event samples exchanged between ``gatewayd`` and ``someipd`` therefore carry the SOME/IP header directly
in front of the payload bytes. ``score::socom::Payload`` already models this: it distinguishes ``header()``
from ``data()`` while both are backed by the same buffer. See :ref:`sample-layout`.

.. _d3-no-slot-manager:

D3: no ``Shared_memory_slot_manager``
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The existing implementation gives every event of a service the same slot size and puts all events of a
service into one shared-memory segment. ``mw::com`` allocates per event, so the sizing is per event as well.
The ``Shared_memory_slot_manager`` family of types is therefore **not** reused. Instead the factory functions
receive a plain configuration structure that describes the ``GenericSkeletonServiceElementInfo`` required for
each service. See :ref:`public-interface`.

.. _d4-static-config:

D4: the set of bridged services is static and known at startup
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

``mw::com`` reads slot counts, subscriber counts and ASIL levels from a ``mw_com_config.json`` that has to
exist before the process starts. A service instance that is not in that file cannot be created at runtime.
Sending a list of needed services from ``gatewayd`` to ``someipd`` over the link, as the ``Connect`` message
does today, would therefore buy nothing: ``someipd`` could not act on it anyway.

Consequently the ``find_service_elements`` feature is **not** carried over. Every SOME/IP service that
``someipd`` offers to ``gatewayd`` over IPC must already be known to ``someipd`` at startup, and the
``On_find_service_change`` callback is not part of the new factory signature.

Mapping SOME/IP services to mw::com features
--------------------------------------------

Concept mapping
~~~~~~~~~~~~~~~

.. list-table::
   :header-rows: 1
   :widths: 34 33 33

   * - Gateway concept
     - Today (message_passing + shm)
     - ``mw::com`` implementation
   * - Bridged service instance
     - ``Service`` + ``Instance_id`` key, ``Connect_service`` handshake
     - one ``mw::com`` service instance, addressed by ``InstanceSpecifier``
   * - Service interface identity and version
     - ``Service_interface_identifier`` on the wire
     - ``serviceTypes`` entry in ``mw_com_config.json``
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
     - ``numberOfSampleSlots`` and event sample size in ``mw_com_config.json``
   * - Peer liveness
     - ``Connect`` / ``Connect_reply`` handshake
     - none; availability is per service instance
   * - Method calls
     - declared, never dispatched
     - not representable, out of scope

Everything in the middle column that has a counterpart in the right column is deleted, not ported.

Role assignment
~~~~~~~~~~~~~~~

A bridged service always has a data source and a data sink. The binding peer on the **source** side owns the
``GenericSkeleton``, the binding peer on the **sink** side owns the ``GenericProxy``. Which peer is which
follows directly from the SOME/IP configuration and is fixed at startup, as required by :ref:`d4-static-config`.

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

Because both daemons hold both roles at the same time, for different services, the implementation is a single
symmetric class. The client and server flavours of the public interface are thin adapters over it, see
:ref:`interface-semantics`.

.. _sample-layout:

Event sample layout
~~~~~~~~~~~~~~~~~~~

A ``mw::com`` event sample is a fixed-size, type-erased buffer described by ``DataTypeMetaInfo{size, alignment}``.
SOME/IP payloads are variable length and, per :ref:`d2-header`, need reserved space in front. The binding
therefore defines the following layout for every event sample it exchanges::

   +----------------------+------------------------+-----------------------------+
   | binding prefix (8 B) | reserved header space  | payload bytes               |
   |  u32 payload_length  |  e.g. SOME/IP header,  |  up to max_payload_size     |
   |  u32 reserved        |  16 B                  |                             |
   +----------------------+------------------------+-----------------------------+
   ^                      ^                        ^
   sample base            Payload::header()        Payload::data()

- ``size`` of the ``DataTypeMetaInfo`` is ``8 + header_size + max_payload_size``, ``alignment`` is 8. The
  8-byte prefix keeps the reserved header space 8-byte aligned.
- the binding constructs ``socom::Payload{span, slot_handle, destroyer, header_size, lead_offset = 8}``, so
  the producing and consuming applications see exactly the ``header()`` / ``data()`` split they already use
  today, and E2E can run over ``header()`` immediately followed by ``data()``.
- ``payload_length`` is the only piece of binding metadata on the link. It is required because LoLa samples
  are fixed size while SOME/IP payloads are not. The producer shrinks its ``Writable_payload``; the binding
  writes ``payload.data().size()`` into the prefix just before ``Send()``; the consumer side reads it back
  and sizes the ``socom::Payload`` accordingly.

Two alternatives were considered and rejected:

- deriving the length from the SOME/IP header length field. That would make ``gateway_ipc_binding`` parse
  SOME/IP, which it deliberately does not do today, and the field is currently not filled in by ``gatewayd``.
- one ``mw::com`` event per length class. That multiplies the deployment configuration and gains nothing.

``header_size`` is a per-event configuration value and not hard-coded to ``someip::kSomeipFullHeaderSize``.
The daemons pass the SOME/IP value; the binding stays SOME/IP agnostic, exactly as it is today.

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
  event update, as it does today when the slot pool is exhausted.
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

- ``Mw_com_binding`` is the single symmetric implementation. It owns one ``Service_binding`` per configured
  service instance and nothing else.
- ``Provider_service_binding`` holds the ``GenericSkeleton``, its per-event allocation table, and the SOCom
  ``Client_connector``.
- ``Consumer_service_binding`` holds the ``FindServiceHandle``, the ``GenericProxy`` once found, and the SOCom
  ``Disabled_server_connector`` / ``Enabled_server_connector``.
- ``Client_adapter`` and ``Server_adapter`` implement the two public interfaces on top of ``Mw_com_binding``.

What is gone compared to :doc:`index`:

- ``impl/ipc_messages.hpp``, ``impl/gateway_ipc_binding_util.hpp``, ``impl/reply_channel.hpp``,
  ``impl/connections.hpp``, ``impl/pending_connects.hpp``, ``impl/connection_metadata.hpp`` — no protocol
- ``impl/shared_memory_slot_manager.cpp``, ``impl/shared_memory_managers.hpp`` — no shared-memory bookkeeping
- ``Runtime::register_service_bridge`` — with static roles the connectors are created eagerly at startup, so
  there is no on-demand ``request_service`` path to hook into
- the client/server asymmetry itself. It only existed because ``message_passing`` needs a listener and a
  connector.

.. _public-interface:

Public interface
----------------

A new public header ``gateway_ipc_binding_mw_com.hpp`` adds the configuration types and two free factory
functions. The existing headers are not modified.

.. code-block:: cpp

   namespace score::gateway_ipc_binding::mw_com {

   /// \brief Bytes reserved at the front of every event sample for binding metadata.
   inline constexpr std::size_t kSample_prefix_size = 8U;
   /// \brief Alignment requested for every event sample.
   inline constexpr std::size_t kSample_alignment = 8U;

   /// \brief Which side of a bridged service this peer implements.
   enum class Role : std::uint8_t {
       /// This peer consumes the service locally via SOCom and offers it via a GenericSkeleton.
       provider,
       /// This peer receives the service via a GenericProxy and offers it locally via SOCom.
       consumer,
   };

   /// \brief One event of a bridged service. The position in Service_config::events is the socom::Event_id.
   struct Event_config {
       /// \brief mw::com event name, must match mw_com_config.json.
       std::string name;
       /// \brief Reserved leading bytes, e.g. score::someip::kSomeipFullHeaderSize.
       std::size_t header_size;
       /// \brief Largest payload this event can carry.
       std::size_t max_payload_size;
   };

   /// \brief One bridged service instance.
   struct Service_config {
       socom::Service_interface_identifier interface;
       socom::Service_instance instance;
       /// \brief mw::com InstanceSpecifier of the gatewayd/someipd link instance.
       std::string instance_specifier;
       Role role;
       /// \brief Events in socom::Event_id order.
       std::vector<Event_config> events;
       /// \brief max_sample_count passed to GenericProxyEvent::Subscribe on the consumer side.
       std::size_t max_sample_count;
   };

   using Service_configs = std::vector<Service_config>;

   /// \brief Sample size derived from an Event_config, i.e. the DataTypeMetaInfo::size.
   std::size_t sample_size(Event_config const& event) noexcept;

   /// \brief Create the mw::com backed binding behind the client interface.
   /// \param runtime SOCom runtime used to create the connectors
   /// \param services Bridged service instances, see D4: this set is fixed for the process lifetime
   /// \param identifier Optional string used for logging only
   /// \return Nullptr if any configured service could not be set up
   std::unique_ptr<Gateway_ipc_binding_client> create_client(
       score::socom::Runtime& runtime, Service_configs services,
       std::string_view identifier = {}) noexcept;

   /// \brief Create the mw::com backed binding behind the server interface.
   /// \details Setup is deferred to Gateway_ipc_binding_server::start().
   std::unique_ptr<Gateway_ipc_binding_server> create_server(
       score::socom::Runtime& runtime, Service_configs services) noexcept;

   }  // namespace score::gateway_ipc_binding::mw_com

``Service_configs`` is an owning value type. Unlike ``Find_service_elements`` and ``Shared_memory_configs`` it
never travels over a link, so there is no reason for it to be trivially copyable or bounded by
``kMax_find_service_elements``. That also removes today's hard limit of four service instances.

``mw::com`` itself is only reached through ``score::mw::com::runtime::InitializeRuntime``, which the daemons
already call for ``gatewayd`` and will have to start calling for ``someipd``. The binding does not own the
``mw::com`` runtime.

.. _interface-semantics:

Interface semantics under mw::com
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The public interfaces are unchanged, but three members have no direct counterpart in a connectionless
transport. Their behaviour is defined as follows and must be documented at the factory functions.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Member
     - Behaviour
   * - ``Gateway_ipc_binding_client::is_connected()``
     - True once setup of all configured services succeeded, i.e. all skeletons were created and all
       find-service searches were started. It does **not** mean the peer daemon is running. There is no
       peer handshake any more; availability is per service instance and is signalled to the applications
       through the normal SOCom service state.
   * - ``Gateway_ipc_binding_server::start()``
     - Performs the setup described above and returns the first error encountered. Calling it twice returns
       an error, as today.

The behaviour of ``is_connected()`` is a genuine semantic change. ``gatewayd`` currently blocks in a loop
until it becomes true in order to wait for ``someipd``. With this implementation that loop completes
immediately. Callers that need to wait for a specific service must wait for that service's SOCom state
instead. If a real readiness signal turns out to be needed, the follow-up is a new interface method rather
than an overloaded ``is_connected()``.

Behavioural views
-----------------

Startup
~~~~~~~

.. uml:: models/mw_com_startup.puml
   :align: center
   :caption: Startup of one provider-role and one consumer-role service

Event flow
~~~~~~~~~~

.. uml:: models/mw_com_event_flow.puml
   :align: center
   :caption: Subscription and event flow for one bridged service

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
  its state with one mutex.
- Neither a SOCom callback nor a ``mw::com`` handler may block. The binding does no I/O and no allocation
  beyond the in-flight table inside them.
- ``GetNewSamples`` must not run concurrently for the same ``GenericProxyEvent``. Because the only caller is
  the receive handler registered for that event, and ``mw::com`` serialises those, this holds without extra
  locking; the mutex is still taken for the SOCom-visible state.
- The SOCom deadlock detector terminates the process if a connector is destroyed from inside one of its own
  callbacks. Teardown therefore never runs from a callback; the ``mw::com`` find-service handler schedules
  the disable and destroys the connector outside the handler, exactly as the current implementation does for
  ``Connect_service_reply``.

Configuration
-------------

Both daemons need a ``mw_com_config.json`` that describes the link instances. ``gatewayd`` already has one for
its application-facing instances and gains additional entries; ``someipd`` needs one for the first time, and
has to call ``InitializeRuntime``.

Per bridged service instance the deployment must provide:

- a ``serviceTypes`` entry whose ``serviceTypeName`` and ``version`` match the SOCom
  ``Service_interface_identifier``, with one event entry per bridged event
- a ``serviceInstances`` entry with the ``instanceSpecifier`` used in ``Service_config``
- ``numberOfSampleSlots`` per event, sized as described in :ref:`sample-layout`
- ``maxSubscribers: 1``. There is exactly one peer daemon per link instance.
- ``asil-level: "QM"``. ``someipd`` is a QM component, so the link instances are QM on both ends even though
  ``gatewayd`` is ASIL-B. This is precisely why the SOME/IP header travels with the payload: E2E is what makes
  the QM data usable in the ASIL-B context.

The link instances must not collide with the ``gatewayd`` to application instances. The proposed convention is
to prefix the link instance specifiers, for example ``ipc/<service_type>_<instance_id>``, and to derive them
in ``mw_someip_config`` rather than in code.

Because both the sample size and the slot count now live in ``mw_com_config.json``, the size computation that
``gatewayd`` does today in ``event_slot_size()`` moves into config generation. The binding validates that the
configured sample size matches ``GenericSkeletonEvent::GetSizeInfo()`` and fails setup on mismatch.

Feature flag and build integration
----------------------------------

The implementation ships as a separate Bazel target so that nothing changes for the default build.

.. code-block:: python

   # score/gateway_ipc_binding/flags/BUILD
   string_flag(
       name = "implementation",
       build_setting_default = "message_passing",
       values = ["message_passing", "mw_com"],
       visibility = ["//visibility:public"],
   )

   config_setting(
       name = "mw_com",
       flag_values = {":implementation": "mw_com"},
       visibility = ["//visibility:public"],
   )

.. code-block:: python

   # score/gateway_ipc_binding/BUILD
   cc_library(
       name = "gateway_ipc_binding_mw_com",
       srcs = glob(["impl_mw_com/**"]),
       hdrs = ["gateway_ipc_binding_mw_com.hpp"],
       deps = [
           ":gateway_ipc_binding",
           "//score/socom",
           "@score_communication//score/mw/com",
       ],
   )

The daemons select the implementation on their dependency edge and guard the differing setup code, which is
roughly fifteen lines in each ``main.cpp``:

.. code-block:: python

   # score/gatewayd/BUILD.bazel, score/someipd/BUILD.bazel
   deps = [...] + select({
       "//score/gateway_ipc_binding/flags:mw_com": [
           "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
       ],
       "//conditions:default": [],
   }),
   defines = select({
       "//score/gateway_ipc_binding/flags:mw_com": ["SCORE_GATEWAY_IPC_BINDING_MW_COM=1"],
       "//conditions:default": [],
   }),

This follows the existing ``string_flag`` plus ``flag_values`` pattern used by
``quality/integration_testing/flags``. A preprocessor guard is unavoidable here because the two factories take
different arguments: the ``mw::com`` variant needs neither a ``message_passing`` connection nor a
``Shared_memory_manager_factory``. Once the ``mw::com`` implementation is the only one, the flag, the guard
and the old target are deleted together.

Testing strategy
----------------

- unit tests for the pure logic: ``Service_config`` to ``GenericSkeletonServiceElementInfo`` conversion,
  sample size computation, prefix encode and decode, the subscription reconciliation state machine
- component tests against the SOCom mock runtime for the connector wiring, mirroring
  ``test/client_server_test.cpp``
- integration tests with a real ``mw::com`` runtime and a generated ``mw_com_config.json``, covering both
  roles, subscription, event round trip and service disappearance; these replace
  ``test/bidirectional_*_int_test.cpp`` and run under ``--config=qemu-integration``
- the existing benchmarks are re-pointed at the new target so that throughput and latency can be compared
  against the current implementation before the flag is flipped

Known gaps
----------

- **method calls** are not representable with ``GenericProxy`` and ``GenericSkeleton``. They are declared but
  never dispatched today, so this is not a regression, but it does close the door on the incremental path
  that the current ``Connect`` shared-memory configuration was designed for.
- **requested event updates**: SOCom ``on_event_update_request`` has no LoLa counterpart. LoLa has no pull
  API on the proxy side, so field-style initial values cannot be served on demand. The request is logged and
  ignored.
- **dynamic service sets**: adding a bridged service requires a configuration change and a restart of both
  daemons. This is a direct consequence of :ref:`d4-static-config`.
- **const-correctness**: turning a ``SamplePtr<void>`` into a writable ``socom::Payload`` needs a
  ``const_cast``, the same wart the current shared-memory read path already carries.

Open points
-----------

- Where the link ``InstanceSpecifier`` is authored: extending ``mw_someip_config`` with an explicit field is
  the safer option, deriving it by convention from the service type name is the cheaper one.
- Whether ``mw_com_config.json`` for the link instances should be generated from ``mw_someip_config`` at build
  time. Hand-maintaining sample sizes in two places will drift.
- Whether the provider side should offer the ``GenericSkeleton`` unconditionally at startup instead of
  gating it on SOCom service availability. Unconditional offering makes ``is_connected()`` meaningful again
  but loses the availability propagation described above.
