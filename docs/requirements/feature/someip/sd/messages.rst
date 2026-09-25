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

SOME/IP-SD Messages and Communication Behavior (Section-Level)
================================================================

These are Tier-2, section-level requirements for the ``someip-sd``
chapter of the Open SOME/IP Specification, covering the Service Discovery
entry types (FindService, OfferService, StopOfferService,
SubscribeEventgroup, StopSubscribeEventgroup, SubscribeEventgroupAck,
SubscribeEventgroupNack) and the Service Discovery communication behavior
(startup, response, shutdown, and error handling). Each requirement below
refines :need:`feat_req__someip__sd` and summarizes the normative scope
of one specification section. It does not copy the specification text
word for word.

.. feat_req:: Service Discovery Messages
   :id: feat_req__someip__sd_svc_discovery_msgs
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   For every Service Discovery entry it produces, the SOME/IP Gateway
   shall set the entry's Index First Option Run, Index Second Option
   Run, Number of Options 1, and Number of Options 2 fields to the
   values that correctly reference the option runs chained to that
   entry.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Messages; covers spec requirement IDs: feat_req_someipsd_256.

.. feat_req:: FindService Entry
   :id: feat_req__someip__sd_findsvc_entry
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall send a FindService entry only while it does
   not know the state of a service instance (no still-valid OfferService
   has been received). It shall set that entry's Type field to 0x00, its
   Service ID to the identifier of the service being searched for, and
   its Instance ID to either a specific instance identifier or the
   wildcard 0xFFFF when any instance is acceptable.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Messages > Service Entries > FindService Entry; covers spec requirement
   IDs: feat_req_someipsd_238, feat_req_someipsd_239.

.. feat_req:: OfferService Entry
   :id: feat_req__someip__sd_offersvc_entry
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall use the OfferService entry type to announce
   a service instance to other communication partners. It shall set the
   entry's Type, Service ID, Instance ID, Major Version, Minor Version,
   and TTL fields from the offered instance's own identification and
   lifetime. It shall reference at least one IPv4 or IPv6 Endpoint
   Option per supported transport layer protocol (UDP and/or TCP), so
   the entry always states how the service can be reached. It shall
   reuse the same endpoint addresses and ports announced in that
   Endpoint Option as the source of the service's events and
   notification events, whether delivered over UDP or over a TCP
   connection the client opens to that endpoint.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Messages > Service Entries > OfferService Entry; covers spec
   requirement IDs: feat_req_someipsd_252, feat_req_someipsd_253,
   feat_req_someipsd_681, feat_req_someipsd_756, feat_req_someipsd_757,
   feat_req_someipsd_858, feat_req_someipsd_758, feat_req_someipsd_762.

.. feat_req:: StopOfferService Entry
   :id: feat_req__someip__sd_stopoffersvc_entry
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall use the StopOfferService entry type to
   announce that it is no longer offering a service instance. It shall
   set that entry to the same field values as the OfferService entry it
   replaces, except that the TTL field shall be set to 0x000000.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Messages > Service Entries > StopOfferService Entry; covers spec
   requirement IDs: feat_req_someipsd_261, feat_req_someipsd_262.

.. feat_req:: Eventgroup Entries
   :id: feat_req__someip__sd_eg_entries
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall encode every eventgroup-related Service
   Discovery entry (SubscribeEventgroup, StopSubscribeEventgroup,
   SubscribeEventgroupAck, SubscribeEventgroupNack) using the Eventgroup
   Entry Type wire format defined for Service Discovery entries.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Messages > Eventgroup Entries; covers spec requirement IDs:
   feat_req_someipsd_237.

.. feat_req:: SubscribeEventgroup Entry
   :id: feat_req__someip__sd_subeg_entry
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall use the SubscribeEventgroup entry type to
   subscribe to an eventgroup. It shall set the entry's Type field to
   0x06, and its Service ID, Instance ID, Major Version, and Eventgroup
   ID fields from the eventgroup being subscribed to. It shall
   reference one or two IPv4 and/or one or two IPv6 Endpoint Options on
   that entry, covering UDP and/or TCP delivery as needed.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Messages > Eventgroup Entries > SubscribeEventgroup Entry; covers spec
   requirement IDs: feat_req_someipsd_321, feat_req_someipsd_322,
   feat_req_someipsd_682.

.. feat_req:: StopSubscribeEventgroup Entry
   :id: feat_req__someip__sd_stopsubeg_entry
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall use the StopSubscribeEventgroup entry type
   to end an existing eventgroup subscription. It shall set that entry
   to the same field values as the SubscribeEventgroup entry it ends,
   except that the TTL field shall be set to 0x000000. It shall
   reference the same options (including but not limited to Endpoint
   and Configuration options) that the original SubscribeEventgroup
   entry referenced.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Messages > Eventgroup Entries > StopSubscribeEventgroup Entry; covers
   spec requirement IDs: feat_req_someipsd_332, feat_req_someipsd_333,
   feat_req_someipsd_1177.

.. feat_req:: SubscribeEventgroupAck Entry
   :id: feat_req__someip__sd_subeg_ack_entry
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall respond to an accepted SubscribeEventgroup
   entry with a SubscribeEventgroupAck entry. It shall copy the Service
   ID, Instance ID, Major Version, Eventgroup ID, TTL, Initial Data
   Requested Flag, and Counter fields from the request it is
   acknowledging. Whenever the subscribed events or notification events
   are delivered by multicast, it shall reference an IPv4 and/or IPv6
   Multicast Option on that entry, stating the multicast address and
   port they will be sent to.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Messages > Eventgroup Entries > SubscribeEventgroupAck Entry; covers
   spec requirement IDs: feat_req_someipsd_613, feat_req_someipsd_614,
   feat_req_someipsd_763.

.. feat_req:: SubscribeEventgroupNack Entry
   :id: feat_req__someip__sd_subeg_nack_entry
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall respond to a rejected SubscribeEventgroup
   entry with a SubscribeEventgroupNack entry. It shall copy the
   Service ID, Instance ID, Major Version, Eventgroup ID, and Counter
   fields from the rejected request, and set the TTL field to
   0x000000. Reasons for rejecting can include, but are not limited to,
   an unknown service/instance/eventgroup/version combination, a
   required TCP connection that is not open, problems with referenced
   options, server resource limits, or denial by a security/ACL policy.
   When it receives a SubscribeEventgroupNack for a subscription that
   needed a TCP connection, the Gateway shall check that connection and
   restart it if needed.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Messages > Eventgroup Entries > SubscribeEventgroupNack Entry; covers
   spec requirement IDs: feat_req_someipsd_618, feat_req_someipsd_1137,
   feat_req_someipsd_619, feat_req_someipsd_869.

.. feat_req:: Service Discovery Startup Behavior
   :id: feat_req__someip__sd_startup_behav
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   When starting Service Discovery for a service instance or eventgroup,
   the SOME/IP Gateway shall move it through an Initial Wait Phase, a
   Repetition Phase, and a Main Phase. It shall enter the Initial Wait
   Phase once the relevant network interface is up (as a server) or the
   instance is requested (as a client). Before sending its first
   entries, it shall wait a random duration between the configured
   minimum and maximum INITIAL_DELAY, reusing that same random value
   across entries of different types sent together so they can be
   packed into one message. After the first message, it shall enter the
   Repetition Phase, waiting an interval that starts at
   REPETITIONS_BASE_DELAY and doubles after each following message,
   sending no more than REPETITIONS_MAX such messages. If
   REPETITIONS_MAX is set to 0, it shall skip the Repetition Phase
   entirely. It shall stop sending FindService entries once it receives
   a matching OfferService. It shall enter the Main Phase after the
   Repetition Phase (or right after the Initial Wait Phase if
   REPETITIONS_MAX is 0). In the Main Phase, it shall wait one
   CYCLIC_OFFER_DELAY before its first message and one
   CYCLIC_OFFER_DELAY between later messages, sending OfferService
   entries on a cycle for as long as a CYCLIC_OFFER_DELAY is configured
   and the instance stays available. In the Main Phase, it shall never
   send FindService entries on a cycle, and shall never trigger
   subscription entries on a cyclic timer, only in response to received
   OfferService entries.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Communication Behavior > Startup Behavior; covers spec requirement IDs:
   feat_req_someipsd_68, feat_req_someipsd_72, feat_req_someipsd_62,
   feat_req_someipsd_63, feat_req_someipsd_64, feat_req_someipsd_65,
   feat_req_someipsd_836, feat_req_someipsd_66, feat_req_someipsd_67,
   feat_req_someipsd_76, feat_req_someipsd_73, feat_req_someipsd_867,
   feat_req_someipsd_74, feat_req_someipsd_75, feat_req_someipsd_80,
   feat_req_someipsd_79, feat_req_someipsd_81, feat_req_someipsd_866,
   feat_req_someipsd_631.

.. feat_req:: Service Discovery Response Behavior
   :id: feat_req__someip__sd_response_behav
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall delay its response to an entry it received
   in a multicast or broadcast Service Discovery message by a random
   duration between the configured minimum and maximum
   REQUEST_RESPONSE_DELAY, to avoid bursts of multicast responses. This
   applies both to directly requested responses (for example
   OfferService in reply to FindService) and to unicast responses
   triggered by a multicast message (for example SubscribeEventgroup in
   reply to OfferService). It shall not apply this delay when responding
   to a unicast message with a unicast message. It shall respond to
   every FindService entry with a unicast OfferService. It may instead
   choose a unicast or multicast response, based on how recently it sent
   the last cyclic offer for that instance and on the FindService
   entry's Unicast Flag. It shall keep its Service Discovery messages
   small by not needlessly duplicating referenced options.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Communication Behavior > Response Behavior; covers spec requirement
   IDs: feat_req_someipsd_83, feat_req_someipsd_766, feat_req_someipsd_624,
   feat_req_someipsd_84, feat_req_someipsd_85, feat_req_someipsd_824,
   feat_req_someipsd_826, feat_req_someipsd_89, feat_req_someipsd_90,
   feat_req_someipsd_91.

.. feat_req:: Service Discovery Shutdown Behavior
   :id: feat_req__someip__sd_shutdown_behav
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall send a StopOfferService entry when it stops
   a server service instance, and shall then delete every subscription
   it holds for that instance. It shall send StopSubscribeEventgroup
   entries for every eventgroup it has subscribed to when it releases a
   client service instance. When the whole ECU shuts down, it shall
   send both StopOfferService and StopSubscribeEventgroup entries for
   all of its offered instances and subscribed eventgroups. On
   receiving a StopOfferService entry, it shall delete its
   subscriptions to that instance and release the resources involved
   (including closing sockets and resetting to default/wildcard state).
   It shall not send FindService entries again until it receives a new
   OfferService or a relevant status change (application, network
   management, or link state). On receiving a StopSubscribeEventgroup
   entry, it shall remove that client from the eventgroup's
   subscription list and release the resources involved.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Communication Behavior > Shutdown Behavior; covers spec requirement
   IDs: feat_req_someipsd_820, feat_req_someipsd_830,
   feat_req_someipsd_1297, feat_req_someipsd_831, feat_req_someipsd_834,
   feat_req_someipsd_822, feat_req_someipsd_821.

.. feat_req:: Service Discovery Error Handling
   :id: feat_req__someip__sd_error_hdl
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall validate every received Service Discovery
   message using the same SOME/IP header checks it applies to any other
   SOME/IP message. It shall then check that enough bytes are present
   for an empty Service Discovery message and for the declared entries
   and options arrays. For each entry it can parse, it shall check that
   the Service ID and Instance ID are known, and that its referenced
   options exist, are complete for what the entry needs, and are of a
   supported kind. Where security applies, it shall also check whether a
   security association already exists for the peer. If a received
   entry fails these checks, the Gateway shall discard that entry.
   However, if only its referenced Endpoint or Multicast Options are the
   cause, it shall discard only those options and keep processing the
   entry itself.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Service Discovery
   Communication Behavior > Error Handling; covers spec requirement IDs:
   feat_req_someipsd_1220, feat_req_someipsd_1164, feat_req_someipsd_1163,
   feat_req_someipsd_102, feat_req_someipsd_105, feat_req_someipsd_106.
