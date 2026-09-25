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

SOME/IP-SD Message Format (Section-Level)
==========================================

These are Tier-2, section-level requirements for the ``someip-sd``
chapter of the Open SOME/IP Specification, covering the ECU-internal
SOME/IP-SD interface and the wire-level SOME/IP-SD message format
(header, entries, and options). Each requirement below refines
:need:`feat_req__someip__sd` and summarizes the normative scope of one
specification section. It does not copy the specification text word for
word.

.. feat_req:: SOME/IP-SD ECU-internal Interface
   :id: feat_req__someip__sd_ecu_internal_interface
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall expose an internal interface for local
   software components. This interface shall report the up/down status
   of remote services and eventgroups, let a local component require or
   release a remote service instance, tell local components the
   require/release status of local services, and let a local component
   set the up/down status of a service it offers locally. The Gateway
   shall also track the link-up and link-down state of the communication
   interfaces SOME/IP-SD uses, including when an Ethernet switch port is
   gated by a security function such as 802.1X. It shall treat such a
   port as down until the security function lets traffic through.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD ECU-internal
   Interface; covers spec requirement IDs: feat_req_someipsd_17,
   feat_req_someipsd_14, feat_req_someipsd_18, feat_req_someipsd_16,
   feat_req_someipsd_22, feat_req_someipsd_203, feat_req_someipsd_204,
   feat_req_someipsd_23, feat_req_someipsd_1184, feat_req_someipsd_1221.

.. feat_req:: SOME/IP-SD Message Format General Requirements
   :id: feat_req__someip__sd_general_reqs
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall support transporting SOME/IP-SD messages
   over UDP. It shall structure every SOME/IP-SD message as an SD header,
   followed by an entries section and an options section, as the
   specification's SOME/IP-SD header format figure shows.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD Message
   Format > General Requirements; covers spec requirement IDs:
   feat_req_someipsd_27, feat_req_someipsd_26, feat_req_someipsd_205.

.. feat_req:: SOME/IP-SD Header
   :id: feat_req__someip__sd_header
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall place the SOME/IP-SD Header right after the
   SOME/IP header. This starts with an 8-bit Flags field, then a 24-bit
   Reserved field set to 0, then the Entries Array and the Options Array,
   each with a uint32 byte-length field in front of it. Within Flags, it
   shall maintain the Reboot Flag (set to 1 until the SOME/IP header's
   Session ID wraps back to 1, then set to 0), the Unicast Flag (always
   set to 1), and the Explicit Initial Data Control Flag (set to 1 only
   when it supports processing the "Initial Data Requested" flag inside
   Eventgroup Entries, and otherwise ignored on receive). It shall set
   any undefined flag bits to 0 when sending and ignore them when
   receiving. It shall track the Reboot Flag and Session ID state
   separately for each multicast/unicast channel and each sender-receiver
   pair, and use that state to reliably detect when a peer reboots. When
   it detects a reboot, it shall expire stale service and subscription
   state, reset the TCP connection state to that peer, and later
   reestablish any TCP connection as the Publish/Subscribe process
   requires. It shall process entries in the Entries Array strictly in
   the order they arrive.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD Message
   Format > SOME/IP-SD Header; covers spec requirement IDs:
   feat_req_someipsd_38, feat_req_someipsd_39, feat_req_someipsd_40,
   feat_req_someipsd_41, feat_req_someipsd_765, feat_req_someipsd_863,
   feat_req_someipsd_871, feat_req_someipsd_872, feat_req_someipsd_87,
   feat_req_someipsd_100, feat_req_someipsd_1187, feat_req_someipsd_1188,
   feat_req_someipsd_1180, feat_req_someipsd_42, feat_req_someipsd_101,
   feat_req_someipsd_862, feat_req_someipsd_1170, feat_req_someipsd_103,
   feat_req_someipsd_44.

.. feat_req:: SOME/IP-SD Entry Format
   :id: feat_req__someip__sd_entry_format
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall encode and decode two entry types in the
   SOME/IP-SD Entries Array: a Service Entry Type for service-level
   entries (Find/Offer/StopOffer), and an Eventgroup Entry Type for
   eventgroup-level entries (Subscribe/StopSubscribe/Ack/Nack). It shall
   follow the layouts the specification's Service Entry Type and
   Eventgroup Entry Type figures show.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD Message
   Format > Entry Format; covers spec requirement IDs:
   feat_req_someipsd_46, feat_req_someipsd_47, feat_req_someipsd_208,
   feat_req_someipsd_109, feat_req_someipsd_209.

.. feat_req:: SOME/IP-SD Options Format
   :id: feat_req__someip__sd_opts_format
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall encode the length field of every SOME/IP-SD
   option to cover all bytes of the option, except for that length field
   itself and the option's type field.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD Message
   Format > Options Format; covers spec requirement IDs:
   feat_req_someipsd_133.

.. feat_req:: SOME/IP-SD Configuration Option
   :id: feat_req__someip__sd_cfg_opt
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall encode and decode the Configuration Option
   as a DNS-SD/DNS-TXT-style list of length-prefixed key/value character
   sequences, ending in a zero length field. Each sequence may split a
   key and a value with a single "=" character, as long as it is not the
   sequence's first character, using printable US-ASCII for the key
   (excluding "="). It shall read a sequence without "=" as a key that
   is present, and a sequence ending in "=" as present with an empty
   value. It shall support multiple entries sharing the same key within
   one Configuration Option, and shall also use the Configuration Option
   to encode hostname, servicename, and instancename where needed,
   following the specification's Configuration Option format and example
   figures.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD Message
   Format > Options Format > Configuration Option; covers spec requirement
   IDs: feat_req_someipsd_149, feat_req_someipsd_150,
   feat_req_someipsd_151, feat_req_someipsd_158, feat_req_someipsd_159,
   feat_req_someipsd_157, feat_req_someipsd_162, feat_req_someipsd_161,
   feat_req_someipsd_160, feat_req_someipsd_217, feat_req_someipsd_684,
   feat_req_someipsd_218, feat_req_someipsd_201, feat_req_someipsd_144,
   feat_req_someipsd_147.

.. feat_req:: SOME/IP-SD IPv4 Endpoint Option
   :id: feat_req__someip__sd_ipv4_ep_opt
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall encode and decode the IPv4 Endpoint Option
   using Type 0x04, carrying an IPv4 address, a transport-layer protocol,
   and a port number, following the specification's IPv4 Endpoint Option
   format figure. As a server, it shall reference this option from
   OfferService entries to advertise up to one UDP and one TCP endpoint,
   and reuse those same endpoints as the source address and port of the
   events it sends. As a client, it shall reference this option from
   SubscribeEventgroup entries to advertise the IPv4 address and UDP
   and/or TCP port where it is ready to receive events.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD Message
   Format > Options Format > IPv4 Endpoint Option; covers spec requirement
   IDs: feat_req_someipsd_127, feat_req_someipsd_128,
   feat_req_someipsd_129, feat_req_someipsd_199, feat_req_someipsd_141,
   feat_req_someipsd_849, feat_req_someipsd_850, feat_req_someipsd_848.

.. feat_req:: SOME/IP-SD IPv4 Multicast Option
   :id: feat_req__someip__sd_ipv4_mcast_opt
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall encode and decode the IPv4 Multicast Option
   using Type 0x14, carrying an IPv4 address, a transport-layer protocol,
   and a port number, following the specification's IPv4 Multicast Option
   format figure. It shall reference this option, not the IPv4 Endpoint
   Option, from SubscribeEventgroupAck entries. When a service supports
   IPv4 multicast, it shall also reference this option from the matching
   OfferService entry, to advertise the multicast address and port where
   it sends multicast events and notification events.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD Message
   Format > Options Format > IPv4 Multicast Option; covers spec
   requirement IDs: feat_req_someipsd_854, feat_req_someipsd_723,
   feat_req_someipsd_724, feat_req_someipsd_725, feat_req_someipsd_733,
   feat_req_someipsd_734, feat_req_someipsd_855.

.. feat_req:: SOME/IP-SD IPv4 SD Endpoint Option
   :id: feat_req__someip__sd_ipv4_sd_ep_opt
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall encode and decode the IPv4 SD Endpoint
   Option using Type 0x24, carrying an IPv4 address, a transport-layer
   protocol, and a port number, following the specification's IPv4 SD
   Endpoint Option format figure. It shall include this option at most
   once, only when the SOME/IP-SD message is transported over IPv4, and
   place it first in the options array when present. It shall not
   reference this option from any SD entry. On reception, if more than
   one IPv4 SD Endpoint Option is present, it shall process only the
   first and ignore the rest. When the option is present, it shall use
   its address and port, instead of the message's source address and
   port, both to address any response to the received SD message and for
   reboot detection.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD Message
   Format > Options Format > IPv4 SD Endpoint Option; covers spec
   requirement IDs: feat_req_someipsd_1082, feat_req_someipsd_1156,
   feat_req_someipsd_1151, feat_req_someipsd_1152, feat_req_someipsd_1114,
   feat_req_someipsd_1084, feat_req_someipsd_1085, feat_req_someipsd_1086,
   feat_req_someipsd_1087, feat_req_someipsd_1095, feat_req_someipsd_1096.

.. feat_req:: SOME/IP-SD MAC-Groupcast Endpoint Option
   :id: feat_req__someip__sd_mac_groupcast_ep_opt
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall encode and decode the MAC-Groupcast Endpoint
   Option using Type 0x15, carrying a MAC groupcast address, a data-link
   layer protocol identifier, and a variable-length protocol-specific
   identifier, following the specification's MAC-Groupcast Endpoint
   Option format figure. Since this option announces a non-SOME/IP
   service instance, the Gateway shall only use it together with a
   Configuration Option that has an otherserv key.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD Message
   Format > Options Format > MAC-Groupcast Endpoint Option; covers spec
   requirement IDs: feat_req_someipsd_1250, feat_req_someipsd_1251,
   feat_req_someipsd_1252, feat_req_someipsd_1253, feat_req_someipsd_1262.

.. feat_req:: Referencing Options from Entries
   :id: feat_req__someip__sd_opt_referencing
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall reference options from an SD entry using an
   option run that names the first referenced option and how many
   options the run contains. It shall treat a run with a count of zero as
   empty and set its index to zero in that case. It shall accept and
   process an incoming SD message whose option run has a zero length but
   a non-zero index. It shall keep SD messages small by not duplicating
   an option across entries without a reason.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD Message
   Format > Referencing Options from Entries; covers spec requirement IDs:
   feat_req_someipsd_336, feat_req_someipsd_342, feat_req_someipsd_343,
   feat_req_someipsd_348, feat_req_someipsd_347, feat_req_someipsd_900.

.. feat_req:: Handling Missing, Redundant and Conflicting Options
   :id: feat_req__someip__sd_opt_conflicts
   :status: valid
   :version: 1
   :tags: someip_sd
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall ignore an option referenced by an entry that
   it does not recognize, and likewise ignore an option referenced by an
   entry that does not need it. When an entry references two or more
   options that conflict, the Gateway shall respond to that entry
   negatively where a negative response is possible (for example a
   SubscribeEventgroup); otherwise it shall ignore the entry and its
   options. When an entry references two distinct Configuration Options,
   it shall merge their key/value sets. Where the two sets have
   conflicting items that share the same key, it shall keep all of those
   items rather than try to merge the duplicates.

   Refines :need:`feat_req__someip__sd`.

   Cf. Open SOME/IP Specification, someip-sd.rst, SOME/IP-SD Message
   Format > Referencing Options from Entries > Handling missing, redundant
   and conflicting Options; covers spec requirement IDs:
   feat_req_someipsd_1142, feat_req_someipsd_1141, feat_req_someipsd_1144,
   feat_req_someipsd_1146, feat_req_someipsd_1147.
