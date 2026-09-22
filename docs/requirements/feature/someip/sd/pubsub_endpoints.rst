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

SOME/IP-SD Publish/Subscribe and Endpoint Handling (Section-Level)
====================================================================

These are Tier-2, section-level requirements for the ``someip-sd``
chapter of the Open SOME/IP Specification, covering non-SOME/IP service
announcement, the publish/subscribe eventing mechanism, endpoint handling
for services and events, and the mandatory SOME/IP-SD feature set. Each
requirement below refines :need:`feat_req__someip__sd` and summarizes the
normative scope of one specification section. It does not copy the
specification text word for word.

.. feat_req:: Announcing Non-SOME/IP Protocols with SOME/IP-SD
   :id: feat_req__someip__sd_announce_non_protos_with
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall be able to announce a non-SOME/IP service
   instance (for example Network Management, Diagnosis, or Flash Update)
   through SOME/IP-SD, using the reserved Service ID 0xFFFE, the regular
   Instance ID scheme, and a Configuration Option that carries exactly
   one "otherserv" key with a non-empty value. It shall give each
   announced non-SOME/IP service instance its own OfferService entry, so
   each can carry its own TTL. It shall never place the "otherserv" key
   on an actual SOME/IP service's Configuration Option, and shall use
   the "otherserv" value when matching FindService, OfferService, and
   RequestService entries for these instances.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Announcing non-SOME/IP
   protocols with SOME/IP-SD; covers spec requirement IDs:
   feat_req_someipsd_500, feat_req_someipsd_1227, feat_req_someipsd_502,
   feat_req_someipsd_503, feat_req_someipsd_575.

.. feat_req:: Publish/Subscribe with SOME/IP and SOME/IP-SD
   :id: feat_req__someip__sd_pub_sub_with_and
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall implement the SOME/IP-SD publish/subscribe
   protocol for events and fields. A client shall register interest at
   run time using SubscribeEventgroup entries, sent in reaction to a
   server's OfferService, and shall request Initial Events whenever it
   holds no active subscription. It shall avoid delivering the same
   regular event twice across overlapping eventgroup subscriptions. A
   client shall deregister with a StopSubscribeEventgroup entry
   (TTL=0). The server shall keep per-eventgroup subscription state,
   acknowledge each SubscribeEventgroup with a SubscribeEventgroupAck,
   deliver initial field values right after that acknowledgement, and
   never send an unsolicited initial value for a pure event. Both sides
   shall recover from missed acknowledgements by re-issuing a
   Stop/Subscribe pair as one atomic message. The server shall tear
   down subscriptions and TCP connections on link loss or unrecoverable
   transport errors, and re-offer the service once its link returns.
   The client shall re-subscribe after a link comes back up or after a
   missed notification timeout. The gateway shall support both implicit
   (pre-configured) and SD-triggered subscription models, and shall
   support a cleanup mechanism for stale client registrations.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Publish/Subscribe with
   SOME/IP and SOME/IP-SD; covers spec requirement IDs:
   feat_req_someipsd_422, feat_req_someipsd_425, feat_req_someipsd_428,
   feat_req_someipsd_429, feat_req_someipsd_430, feat_req_someipsd_431,
   feat_req_someipsd_1191, feat_req_someipsd_1192, feat_req_someipsd_1193,
   feat_req_someipsd_1168, feat_req_someipsd_632, feat_req_someipsd_432,
   feat_req_someipsd_433, feat_req_someipsd_634, feat_req_someipsd_435,
   feat_req_someipsd_437, feat_req_someipsd_436, feat_req_someipsd_633,
   feat_req_someipsd_439, feat_req_someipsd_440, feat_req_someipsd_1182,
   feat_req_someipsd_767, feat_req_someipsd_441, feat_req_someipsd_844,
   feat_req_someipsd_1178, feat_req_someipsd_1171, feat_req_someipsd_1169,
   feat_req_someipsd_1176, feat_req_someipsd_691, feat_req_someipsd_833,
   feat_req_someipsd_1167, feat_req_someipsd_1166, feat_req_someipsd_625,
   feat_req_someipsd_626, feat_req_someipsd_823, feat_req_someipsd_442,
   feat_req_someipsd_444, feat_req_someipsd_445, feat_req_someipsd_818,
   feat_req_someipsd_828, feat_req_someipsd_829.

.. feat_req:: Endpoint Handling for Services and Events
   :id: feat_req__someip__sd_endpoint_handling
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall get the IP addresses and port numbers it
   uses for a service instance from the Endpoint and Multicast Options
   carried in SOME/IP-SD entries. These shall override any statically
   configured values whenever the two disagree.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Endpoint Handling for
   Services and Events; covers spec requirement IDs:
   feat_req_someipsd_778.

.. feat_req:: SOME/IP-SD Service Endpoints
   :id: feat_req__someip__sd_svc_eps
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall reference at most one UDP Endpoint Option
   and at most one TCP Endpoint Option of a single IP version in each
   OfferService entry. It shall use those options both as the address
   and port where the service instance accepts requests, and as the
   address and port it sends its events from. It shall never send an
   event of that service instance from any other endpoint. When
   multiple service instances are offered from the same ECU, the
   gateway shall tell their SOME/IP traffic apart using the Endpoint
   Options referenced by their respective OfferService entries, not an
   Instance ID carried in the header. A FindService entry shall never
   reference an Endpoint or Multicast Option. The gateway shall ignore
   any such option if present, while still accepting other, unrelated
   options on that entry.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Endpoint Handling for
   Services and Events > Service Endpoints; covers spec requirement IDs:
   feat_req_someipsd_780, feat_req_someipsd_779, feat_req_someipsd_781,
   feat_req_someipsd_797, feat_req_someipsd_782, feat_req_someipsd_877,
   feat_req_someipsd_878, feat_req_someipsd_879.

.. feat_req:: SOME/IP-SD Eventgroup Endpoints
   :id: feat_req__someip__sd_eg_eps
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall reference at most one UDP Endpoint Option
   and at most one TCP Endpoint Option of a single IP version in each
   SubscribeEventgroup entry. These shall be treated as the client-side
   address and port where it receives unicast events for that service
   instance. TCP events shall be carried on the TCP connection the
   client already opened before sending the SubscribeEventgroup entry.
   The gateway shall deliver initial events by unicast from server to
   client. A SubscribeEventgroupAck entry shall reference at most one
   Multicast Option of a single IP version, using UDP as its transport.
   A client receiving such an option shall open the advertised
   multicast endpoint as quickly as it can, so it does not miss
   multicast events.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Endpoint Handling for
   Services and Events > Eventgroup Endpoints; covers spec requirement
   IDs: feat_req_someipsd_786, feat_req_someipsd_787,
   feat_req_someipsd_798, feat_req_someipsd_788, feat_req_someipsd_793,
   feat_req_someipsd_789, feat_req_someipsd_790, feat_req_someipsd_791.

.. feat_req:: SOME/IP-SD Endpoint Security Considerations
   :id: feat_req__someip__sd_security_consid
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   By default, the SOME/IP Gateway shall check that any IP address
   carried in an Endpoint Option or SD Endpoint Option belongs to the
   subnet SOME/IP-SD operates on. If this check fails, it shall discard
   both the address and the SD entry that references it, so only
   clients and servers on the same subnet can be reached. The gateway
   shall let this check be turned off through configuration.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Endpoint Handling for
   Services and Events > Security Considerations; covers spec requirement
   IDs: feat_req_someipsd_1135, feat_req_someipsd_1149.

.. feat_req:: SOME/IP-SD Mandatory Feature Set and Basic Behavior
   :id: feat_req__someip__sd_mandatory_features
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   As its baseline SOME/IP-SD compliance level, the SOME/IP Gateway
   shall implement all seven entry types (FindService, OfferService,
   StopOfferService, SubscribeEventgroup, StopSubscribeEventgroup,
   SubscribeEventgroupAck, SubscribeEventgroupNack), the Endpoint,
   Multicast, and Configuration Options for each IP version it
   supports, and at least the receiving side of the matching SD
   Endpoint Option. On the server side, it shall offer services through
   the Initial Wait, Repetition, and Main Phases as configured, offer
   over multicast during the Repetition and Main Phases, answer a
   Main-Phase FindService with a unicast OfferService, send a
   StopOfferService on shutdown, process Subscribe/StopSubscribeEventgroup
   entries, unicast the matching Ack/Nack, and control (fan out) event
   delivery, including initial events, based on current SD
   subscriptions. On the client side, it shall discover services by
   multicast FindService only during the Repetition Phase, stop
   searching once a regular OfferService arrives, respond to an
   OfferService with a unicast SD message covering every eventgroup it
   still wants, interpret Ack/Nack entries as specified, work correctly
   when only a TTL is configured for an eventgroup's SD timings, and
   respond to an OfferService without waiting, even when no
   Request-Response-Delay is configured. Both sides shall implement
   Session ID and Reboot Flag handling, including separate multicast and
   per-unicast-relation Session ID counters, as specified.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, Mandatory Feature Set
   and Basic Behavior; covers spec requirement IDs:
   feat_req_someipsd_1195, feat_req_someipsd_808, feat_req_someipsd_809,
   feat_req_someipsd_810, feat_req_someipsd_857, feat_req_someipsd_811,
   feat_req_someipsd_812, feat_req_someipsd_816, feat_req_someipsd_813,
   feat_req_someipsd_814, feat_req_someipsd_1194.
