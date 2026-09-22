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

SOME/IP-TP Segmentation (Section-Level)
=========================================

These are Tier-2, section-level requirements for the ``someip-tp`` chapter
of the Open SOME/IP Specification. Each requirement below refines
:need:`feat_req__someip__tp` and summarizes the normative scope of one
specification section. It does not copy the specification text word for
word.

.. feat_req:: SOME/IP-TP General Segmentation Behavior
   :id: feat_req__someip__tp_general
   :status: valid
   :version: 1
   :tags: someip_tp
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall segment SOME/IP messages that exceed the UDP
   payload size limit, using the SOME/IP Transport Protocol (SOME/IP-TP)
   segment header. It shall also reassemble incoming SOME/IP-TP segments
   back into the original message before further processing. This lets
   oversized payloads be sent over UDP without exceeding the size limit
   of a single datagram.

   Refines :need:`feat_req__someip__tp`.

   Cf. Open SOME/IP Specification, someip-tp.rst, Transporting large
   SOME/IP messages over UDP (SOME/IP-TP); covers spec requirement IDs:
   feat_req_someiptp_760, feat_req_someiptp_764, feat_req_someiptp_762,
   feat_req_someiptp_763, feat_req_someiptp_765, feat_req_someiptp_766,
   feat_req_someiptp_832, feat_req_someiptp_768, feat_req_someiptp_767,
   feat_req_someiptp_769, feat_req_someiptp_770, feat_req_someiptp_771,
   feat_req_someiptp_772, feat_req_someiptp_773, feat_req_someiptp_774,
   feat_req_someiptp_801.

.. feat_req:: SOME/IP-TP Sender Specific Behavior
   :id: feat_req__someip__tp_sender_specific_behav
   :status: valid
   :version: 1
   :tags: someip_tp
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   As a SOME/IP-TP sender, the SOME/IP Gateway shall split an oversized
   message into correctly ordered, non-duplicated segments of an allowed
   size. It shall mark the final segment so a receiver can tell when the
   segmented message ends.

   Refines :need:`feat_req__someip__tp`.

   Cf. Open SOME/IP Specification, someip-tp.rst, Sender specific
   behavior; covers spec requirement IDs: feat_req_someiptp_788,
   feat_req_someiptp_777, feat_req_someiptp_778, feat_req_someiptp_779,
   feat_req_someiptp_780, feat_req_someiptp_786.

.. feat_req:: SOME/IP-TP Receiver Specific Behavior
   :id: feat_req__someip__tp_receiver_specific_behav
   :status: valid
   :version: 1
   :tags: someip_tp
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   As a SOME/IP-TP receiver, the SOME/IP Gateway shall buffer incoming
   segments that belong to the same segmented message. Once the final
   segment arrives, it shall reassemble them in the correct order. If a
   segment is out of order, duplicated, or an error leaves the message
   incomplete, it shall discard that partial reassembly buffer without
   corrupting any other reassembly in progress.

   Refines :need:`feat_req__someip__tp`.

   Cf. Open SOME/IP Specification, someip-tp.rst, Receiver specific
   behavior; covers spec requirement IDs: feat_req_someiptp_781,
   feat_req_someiptp_794, feat_req_someiptp_787, feat_req_someiptp_795,
   feat_req_someiptp_793, feat_req_someiptp_782, feat_req_someiptp_783,
   feat_req_someiptp_784, feat_req_someiptp_785, feat_req_someiptp_789,
   feat_req_someiptp_796, feat_req_someiptp_802, feat_req_someiptp_803,
   feat_req_someiptp_810, feat_req_someiptp_792.
