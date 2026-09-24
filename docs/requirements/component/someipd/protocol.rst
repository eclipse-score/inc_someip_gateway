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

SOME/IP Network Daemon Protocol Requirements
=============================================

Component-level requirements for ``someipd``'s implementation of the Open
SOME/IP Specification RPC and Service Discovery chapters. Each requirement
derives from the corresponding feature requirement in
:doc:`/requirements/feature/someip/protocol` and is satisfied by
:need:`comp__someipd`.

Only the ``someip-rpc`` and ``someip-sd`` chapters are covered here: they are
the only chapters with a concrete ``someipd`` implementation and TC8
conformance test coverage today. The ``someip-tp``, ``someip-compat``, and
``someip-ids`` feature requirements have no component-level requirements yet.

Component Requirements: SOME/IP RPC
-----------------------------------

.. comp_req:: SOME/IP RPC Wire Header Handling
   :id: comp_req__someipd__rpc_header
   :status: valid
   :version: 1
   :tags: someip, rpc, wire_protocol, header
   :derived_from: feat_req__someip__rpc
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   ``someipd`` shall encode and decode the 16-byte SOME/IP message header
   (Message ID, Length, Request ID, Protocol Version, Interface Version,
   Message Type, Return Code) for every SOME/IP message it sends or
   receives, and shall reject messages whose header fields are structurally
   invalid (truncated below the minimum header size, unsupported protocol
   version, or a declared length exceeding the received payload) without
   crashing.

.. comp_req:: SOME/IP RPC Transport Bindings
   :id: comp_req__someipd__rpc_transport
   :status: valid
   :version: 1
   :tags: someip, rpc, wire_protocol, transport
   :derived_from: feat_req__someip__rpc
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   ``someipd`` shall support both the UDP and TCP transport bindings for
   SOME/IP RPC communication, including advertisement of the correct
   transport in Service Discovery endpoint options and correct parsing of
   multiple SOME/IP messages carried consecutively within a single UDP
   datagram.

.. comp_req:: SOME/IP RPC Request/Response and Fire-and-Forget Dispatch
   :id: comp_req__someipd__rpc_request_response
   :status: valid
   :version: 1
   :tags: someip, rpc, wire_protocol, request_response
   :derived_from: feat_req__someip__rpc
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   ``someipd`` shall dispatch REQUEST messages to the addressed method and
   return a RESPONSE message carrying the matching Client ID and Session ID
   from the originating Request ID, and shall process REQUEST_NO_RETURN
   (fire-and-forget) messages without emitting a response.

.. comp_req:: SOME/IP RPC Event and Field Notification Delivery
   :id: comp_req__someipd__rpc_events_fields
   :status: valid
   :version: 1
   :tags: someip, rpc, wire_protocol, events, fields
   :derived_from: feat_req__someip__rpc
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   ``someipd`` shall deliver NOTIFICATION messages carrying the correct
   event ID only to endpoints with an active eventgroup subscription, shall
   deliver an initial field notification to a new field subscriber
   immediately upon subscription, and shall dispatch field getter and
   setter method invocations, notifying active subscribers when a setter
   changes the stored field value.

.. comp_req:: SOME/IP RPC Error Return Codes
   :id: comp_req__someipd__rpc_error_handling
   :status: valid
   :version: 1
   :tags: someip, rpc, wire_protocol, error_handling
   :derived_from: feat_req__someip__rpc
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   ``someipd`` shall return the SOME/IP return code that matches the error
   condition of a processed REQUEST, including ``E_UNKNOWN_SERVICE`` for
   requests to a service it does not host, ``E_UNKNOWN_METHOD`` for an
   unrecognized method ID, and ``E_WRONG_INTERFACE_VERSION`` for an
   interface version mismatch.

Component Requirements: SOME/IP Service Discovery
--------------------------------------------------

.. comp_req:: SOME/IP-SD Message and Options Format
   :id: comp_req__someipd__sd_message_format
   :status: valid
   :version: 1
   :tags: someip, service_discovery, wire_protocol, format
   :derived_from: feat_req__someip__sd
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   ``someipd`` shall encode SOME/IP-SD messages with a correctly formatted
   SD header, Entry array, and Options array, including IPv4 endpoint and
   IPv4 multicast options, so that Service, Eventgroup, and Endpoint Option
   entries are structurally valid per the Open SOME/IP-SD specification.

.. comp_req:: SOME/IP-SD Offer and Find Service Discovery
   :id: comp_req__someipd__sd_offer_discovery
   :status: valid
   :version: 1
   :tags: someip, service_discovery, wire_protocol, offer, find
   :derived_from: feat_req__someip__sd
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   ``someipd`` shall transmit OfferService entries for every service it
   hosts, carrying the correct service ID, instance ID, major/minor
   version, TTL, and endpoint option; shall respond to a FindService entry
   for a known service with a unicast OfferService and shall not respond
   for an unknown service; and shall transmit a StopOfferService entry
   when it withdraws a service.

.. comp_req:: SOME/IP-SD Eventgroup Subscription Lifecycle
   :id: comp_req__someipd__sd_subscription
   :status: valid
   :version: 1
   :tags: someip, service_discovery, wire_protocol, eventgroup, subscription
   :derived_from: feat_req__someip__sd
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   ``someipd`` shall acknowledge a SubscribeEventgroup entry for a known
   eventgroup with a SubscribeEventgroupAck, reject a SubscribeEventgroup
   entry for an unknown eventgroup with a SubscribeEventgroupNack (TTL =
   0), cease event delivery upon receiving a StopSubscribeEventgroup entry,
   and remove a subscription once its TTL elapses without renewal.

.. comp_req:: SOME/IP-SD Startup and Cyclic Offer Timing
   :id: comp_req__someipd__sd_timing
   :status: valid
   :version: 1
   :tags: someip, service_discovery, wire_protocol, timing
   :derived_from: feat_req__someip__sd
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   ``someipd`` shall observe the SOME/IP-SD initial-wait, repetition, and
   main-phase timing state machine when offering a service, repeating
   OfferService entries at the configured cyclic offer interval during the
   main phase, and shall reset its SD session ID and set the reboot flag
   on the first SD message sent after a restart.

.. comp_req:: SOME/IP-SD Malformed Packet Robustness
   :id: comp_req__someipd__sd_robustness
   :status: valid
   :version: 1
   :tags: someip, service_discovery, wire_protocol, robustness
   :derived_from: feat_req__someip__sd
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   ``someipd`` shall remain functional and continue to respond to valid
   Service Discovery requests after receiving a malformed SOME/IP-SD
   message, including messages with inconsistent entries/options length
   fields, unknown option types, or entries referencing options that are
   absent or out of range.
