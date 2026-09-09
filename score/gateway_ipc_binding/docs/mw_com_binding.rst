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
- keep the public interface of the component unchanged, including the meaning of ``is_connected()``, so that
  the daemons can be switched over by exchanging a single factory call
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
     - dedicated typed ``SomeipdService``, see :ref:`someipd-service`
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

Because both daemons hold both roles at the same time, for different services, the bridged-service part of the
implementation is a single symmetric class. The only asymmetry left is the ``SomeipdService`` described next,
and it is what distinguishes the client flavour of the public interface from the server flavour.

.. _someipd-service:

SomeipdService and peer liveness
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The bridged services alone cannot tell ``gatewayd`` whether ``someipd`` is running. A remote service instance
only becomes available once the SOME/IP service behind it is available on the network, so its absence is
ambiguous: it means either "``someipd`` is down" or "``someipd`` is up but the network service is not there".
The ``Connect`` / ``Connect_reply`` handshake resolves exactly this ambiguity today and must be preserved.

It is replaced by ``SomeipdService``, a dedicated ``mw::com`` service that represents the ``someipd`` daemon
itself rather than any SOME/IP service:

- ``someipd`` provides one ``SomeipdService`` instance. Its presence *is* the liveness signal.
- ``gatewayd`` consumes it with ``StartFindService`` and creates a ``SomeipdServiceProxy`` for it.
- ``Gateway_ipc_binding_client::is_connected()`` returns true once that proxy has been created, and returns to
  false when the find-service handler reports the instance gone.

Properties of this design:

- it is a genuine peer-liveness signal, not a local self-check, so ``gatewayd``'s existing startup wait loop
  keeps working unchanged, with the same meaning and the same point in the sequence as today
- it recovers automatically. If ``someipd`` restarts, the handler fires with an empty container and then again
  with a handle, so ``is_connected()`` goes false and true again. The bridged services re-establish themselves
  independently and need no ordering guarantee relative to it.
- the instance specifier plays the role that the ``message_passing`` channel name plays today: it must be
  unique per ``gatewayd`` / ``someipd`` pair on a host, and it is the one piece of configuration both daemons
  must agree on.

.. _someipd-service-ordering:

Offering order
^^^^^^^^^^^^^^

``someipd`` offers ``SomeipdService`` **first**, before it creates any bridged-service skeleton and before any
service coming from the network is offered. ``is_connected() == true`` therefore means "the peer's binding is
up and accepting", not "the peer has finished setting up its services". Bridged services arrive
asynchronously afterwards, each signalled through the normal SOCom service state.

This mirrors the current implementation exactly:

- ``someipd`` calls ``binding_server->start()`` before initialising the network stack and before creating any
  ``LocalNetworkService`` or ``RemoteNetworkService`` (``score/someipd/main.cpp:152`` versus ``:159``,
  ``:167`` and ``:198``). ``setup_vsomeip()`` is deferred further still, until vsomeip reports
  ``ST_REGISTERED``.
- ``Connect_reply`` is sent from ``handle_connect_message`` as soon as the client connects, independent of
  which services exist.
- structurally, ``Offer_service`` can only be sent to an already connected client, so the handshake always
  precedes any service offer on the wire.

Offering it first is not just parity, it is required for correct startup. ``gatewayd`` blocks on
``is_connected()`` and only creates its ``LocalServiceInstance`` and ``RemoteServiceInstance`` objects once it
returns true (``score/gatewayd/main.cpp:279`` versus ``:290``). ``is_connected()`` is therefore a gate that
must open **early**. If ``SomeipdService`` were offered last, ``gatewayd``'s binding could already have
discovered bridged services while ``gatewayd`` itself had not yet created the SOCom endpoints behind them, an
inversion that does not exist today.

Typed, not generic
^^^^^^^^^^^^^^^^^^

``SomeipdService`` is the one service in this design that is **not** generic. Unlike a bridged SOME/IP service
its content is known at compile time, it is defined by this repository rather than by a customer's SOME/IP
deployment, and it is not a pass-through for opaque bytes. A typed skeleton and proxy is therefore the right
tool, and it buys three things a ``GenericSkeleton`` cannot:

- **methods**. ``GenericProxy`` and ``GenericSkeleton`` are event-only. A typed interface can carry
  ``Trait::Method``, which is what a real control API for ``someipd`` would need. This is the decisive reason.
- **fields**, with ``Get`` / ``Set`` / notification semantics, which are the natural way to expose a daemon
  state such as "network up" without inventing an event protocol.
- **compile-time checked payload types** shared by both daemons from one header, instead of a runtime-checked
  ``DataTypeMetaInfo`` and a manual byte layout.

The interface is defined once and shared by both daemons, following the trait pattern that
``tests/benchmarks/echo_service.h`` already uses in this repository:

.. code-block:: cpp

   // score/someip/someipd_service.hpp, shared by gatewayd and someipd
   namespace score::someip {

   template <typename Trait>
   class SomeipdServiceInterface : public Trait::Base {
      public:
       using Trait::Base::Base;

       // Intentionally empty for now. The presence of the offered instance is the
       // liveness signal. Events, fields and methods to control someipd are added here.
   };

   using SomeipdServiceProxy = score::mw::com::AsProxy<SomeipdServiceInterface>;
   using SomeipdServiceSkeleton = score::mw::com::AsSkeleton<SomeipdServiceInterface>;

   }  // namespace score::someip

Usage is the standard typed API, ``SomeipdServiceSkeleton::Create(specifier)`` plus ``OfferService()`` on the
``someipd`` side, and ``SomeipdServiceProxy::StartFindService(handler, specifier)`` plus
``SomeipdServiceProxy::Create(handle)`` on the ``gatewayd`` side.

Extending SomeipdService
^^^^^^^^^^^^^^^^^^^^^^^^

Adding control capability later is a change to this one header plus the corresponding
``mw_com_config.json`` entries. No change to the binding's public interface and no wire format to version:

.. code-block:: cpp

   template <typename Trait>
   class SomeipdServiceInterface : public Trait::Base {
      public:
       using Trait::Base::Base;

       // Daemon state, readable and change-notified.
       typename Trait::template Field<SomeipdStatus, score::mw::com::WithGetter,
                                      score::mw::com::WithNotifier>
           status_{*this, "status"};

       // Control operations.
       typename Trait::template Method<void()> stop_offering_all_{*this, "stop_offering_all"};
       typename Trait::template Method<SomeipdStatistics()> get_statistics_{*this, "get_statistics"};
   };

Because this path exists, the ``find_service_elements`` feature that :ref:`d4-static-config` drops does not
have to be replaced by a new ad-hoc mechanism if it is ever needed again. It becomes a method or a field here.

The interface is empty today, so the deployed instance has no service elements. This is explicitly supported:
the ``mw_com_config.json`` schema documents ``events`` as optional, and the typed wrapper simply has no
elements to bind. The residual risk is that an element-less instance is an unusual deployment shape, so the
first integration test to write is "offer and find ``SomeipdService``". If a real LoLa instance turns out to
need at least one service element, the fallback is to add the ``status_`` field above, which is wanted anyway.

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

- ``Mw_com_binding`` owns one ``Service_binding`` per configured service instance plus one
  ``Someipd_service_binding``.
- ``Someipd_service_binding`` implements one of the two halves of :ref:`someipd-service`: the provider half
  owns the ``SomeipdServiceSkeleton``, the consumer half owns the ``FindServiceHandle``, the
  ``SomeipdServiceProxy`` and the ``std::atomic<bool>`` behind ``is_connected()``. It is the only place in
  the binding that touches a typed proxy or skeleton.
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
- the client/server asymmetry for bridged services. What remains of it is ``SomeipdService``: the client
  flavour consumes it, the server flavour provides it.

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
       /// \brief mw::com InstanceSpecifier of the gatewayd/someipd instance for this bridged service.
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
   /// \details Consumes SomeipdService and reports it through is_connected().
   /// \param runtime SOCom runtime used to create the connectors
   /// \param someipd_service_specifier mw::com InstanceSpecifier of the SomeipdService instance that
   ///        the peer provides. Must be unique per gatewayd/someipd pair on a host, and must be the
   ///        same value that the peer passes to create_server().
   /// \param services Bridged service instances, see D4: this set is fixed for the process lifetime
   /// \param identifier Optional string used for logging only
   /// \return Nullptr if any configured service could not be set up
   std::unique_ptr<Gateway_ipc_binding_client> create_client(
       score::socom::Runtime& runtime, std::string someipd_service_specifier,
       Service_configs services, std::string_view identifier = {}) noexcept;

   /// \brief Create the mw::com backed binding behind the server interface.
   /// \details Provides SomeipdService. Setup is deferred to Gateway_ipc_binding_server::start(),
   ///          which offers SomeipdService first, before any bridged service is set up.
   /// \param runtime SOCom runtime used to create the connectors
   /// \param someipd_service_specifier mw::com InstanceSpecifier of the SomeipdService instance to
   ///        provide, see create_client()
   /// \param services Bridged service instances
   std::unique_ptr<Gateway_ipc_binding_server> create_server(
       score::socom::Runtime& runtime, std::string someipd_service_specifier,
       Service_configs services) noexcept;

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

The public interfaces are unchanged. Their behaviour is defined as follows and must be documented at the
factory functions.

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Member
     - Behaviour
   * - ``Gateway_ipc_binding_client::is_connected()``
     - True while the ``SomeipdServiceProxy`` exists, i.e. while the peer's binding is up and accepting.
       See :ref:`someipd-service`. This keeps the meaning and the sequencing it has today, so
       ``gatewayd``'s startup wait loop is unaffected. Unlike today it can also go back to false, when the
       peer stops.
   * - ``Gateway_ipc_binding_server::start()``
     - Offers ``SomeipdService`` first, then sets up all bridged services, matching the order in which
       ``someipd`` starts its IPC server before its network services today. Returns the first error
       encountered. Calling it twice returns an error, as today.
   * - ``Gateway_ipc_binding_server::get_client_identifiers()``
     - Returns an empty map. ``mw::com`` does not expose consumer identities to a skeleton, and no
       production code uses this today.

``is_connected()`` is read from a different thread than the find-service handler that maintains it, so it is
backed by an ``std::atomic<bool>``.

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

Both daemons need a ``mw_com_config.json`` that describes the instances on the ``gatewayd`` / ``someipd``
link. ``gatewayd`` already has one for its application-facing instances and gains additional entries;
``someipd`` needs one for the first time, and has to call ``InitializeRuntime``.

Per bridged service instance the deployment must provide:

- a ``serviceTypes`` entry whose ``serviceTypeName`` and ``version`` match the SOCom
  ``Service_interface_identifier``, with one event entry per bridged event
- a ``serviceInstances`` entry with the ``instanceSpecifier`` used in ``Service_config``
- ``numberOfSampleSlots`` per event, sized as described in :ref:`sample-layout`
- ``maxSubscribers: 1``. There is exactly one peer daemon per instance.
- ``asil-level: "QM"``. ``someipd`` is a QM component, so these instances are QM on both ends even though
  ``gatewayd`` is ASIL-B. This is precisely why the SOME/IP header travels with the payload: E2E is what makes
  the QM data usable in the ASIL-B context.

These instances must not collide with the ``gatewayd`` to application instances. The proposed convention is to
prefix their specifiers, for example ``ipc/<service_type>_<instance_id>``, and to derive them in
``mw_someip_config`` rather than in code.

In addition, both daemons need one entry for :ref:`someipd-service`. It is the only entry that both files must
spell identically, and it is the replacement for the shared ``ipc_channel_name`` command line option:

.. code-block:: json

   {
     "serviceTypes": [
       {
         "serviceTypeName": "/score/someip/SomeipdService",
         "version": { "major": 1, "minor": 0 },
         "bindings": [ { "binding": "SHM", "serviceId": 6400 } ]
       }
     ],
     "serviceInstances": [
       {
         "instanceSpecifier": "someipd/daemon",
         "serviceTypeName": "/score/someip/SomeipdService",
         "version": { "major": 1, "minor": 0 },
         "instances": [ { "instanceId": 1, "asil-level": "QM", "binding": "SHM" } ]
       }
     ]
   }

The ``events`` arrays are omitted on purpose: ``SomeipdServiceInterface`` declares no service elements yet, and
the schema marks ``events`` optional. Every element added to the interface later needs a matching entry in
both files, which is the usual typed-service deployment workflow.

Because both the sample size and the slot count now live in ``mw_com_config.json``, the size computation that
``gatewayd`` does today in ``event_slot_size()`` moves into config generation. The binding validates that the
configured sample size matches ``GenericSkeletonEvent::GetSizeInfo()`` and fails setup on mismatch.

Testing strategy
----------------

- unit tests for the pure logic: ``Service_config`` to ``GenericSkeletonServiceElementInfo`` conversion,
  sample size computation, prefix encode and decode, the subscription reconciliation state machine
- component tests against the SOCom mock runtime for the connector wiring, mirroring
  ``test/client_server_test.cpp``
- an integration test for ``SomeipdService`` first, since it is both the peer-liveness contract and the proof
  that an element-less typed instance works: offer, find, ``is_connected()`` true, peer stops,
  ``is_connected()`` false, peer restarts, true again
- integration tests with a real ``mw::com`` runtime and a generated ``mw_com_config.json``, covering both
  roles, subscription, event round trip and service disappearance; these replace
  ``test/bidirectional_*_int_test.cpp`` and run under ``--config=qemu-integration``
- the existing benchmarks are re-pointed at the new target so that throughput and latency can be compared
  against the current implementation before the flag is flipped

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
- **dynamic service sets**: adding a bridged service requires a configuration change and a restart of both
  daemons. This is a direct consequence of :ref:`d4-static-config`.
- **peer identity**: ``get_client_identifiers()`` returns nothing. ``mw::com`` does not tell a skeleton who
  its consumers are, so a future authorisation check on the link would have to use LoLa's ``allowedConsumer``
  deployment lists instead.
- **liveness granularity**: ``SomeipdService`` reports that the peer's binding is up and accepting, not that
  any given bridged service is usable, because it is offered before them, see
  :ref:`someipd-service-ordering`. That is the same guarantee ``Connect_reply`` gives today, but it is worth
  stating because per-service availability now arrives through a second, independent discovery path.
- **const-correctness**: turning a ``SamplePtr<void>`` into a writable ``socom::Payload`` needs a
  ``const_cast``, the same wart the current shared-memory read path already carries.

Open points
-----------

- Where the per-bridged-service ``InstanceSpecifier`` is authored: extending ``mw_someip_config`` with an
  explicit field is the safer option, deriving it by convention from the service type name is the cheaper one.
- Whether ``mw_com_config.json`` for the bridged instances should be generated from ``mw_someip_config`` at
  build time. Hand-maintaining sample sizes in two places will drift.
- Whether an element-less typed instance works end to end on a real runtime. The API and the schema both allow
  it, but it needs to be proven by the first integration test; see :ref:`someipd-service` for the fallback.
- Where ``someipd_service.hpp`` should live. ``score/someip/`` is proposed because both daemons already depend
  on it and ``gateway_ipc_binding`` should not own a ``someipd`` control API, but that puts an ``mw::com``
  dependency into ``score/someip``.
