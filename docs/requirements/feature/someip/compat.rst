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

SOME/IP Migration and Compatibility (Section-Level)
=====================================================

These are Tier-2, section-level requirements for the ``someip-compat``
chapter of the Open SOME/IP Specification. Each requirement below refines
:need:`feat_req__someip__compat` and summarizes the normative scope of one
specification section. It does not copy the specification text word for
word.

.. feat_req:: SOME/IP Forward Compatibility Handling
   :id: feat_req__someip__compat_forward
   :status: valid
   :version: 1
   :tags: someip_compat
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall tolerate forward-compatible variation in
   received SOME/IP messages. It shall not treat a message as malformed
   only because it is longer than expected, omits an optional parameter,
   or has an unknown message type. This lets the gateway keep working
   with peers that use a newer, backward-compatible version of a
   service.

   Refines :need:`feat_req__someip__compat`.

   Cf. Open SOME/IP Specification, someip-compat.rst, Supporting forward
   compatibility; covers spec requirement IDs: feat_req_someipcompat_1197,
   feat_req_someipcompat_1216, feat_req_someipcompat_1198,
   feat_req_someipcompat_1199, feat_req_someipcompat_1200,
   feat_req_someipcompat_1201, feat_req_someipcompat_1202.

.. feat_req:: SOME/IP Multiple Major Version Support
   :id: feat_req__someip__compat_multi_version
   :status: valid
   :version: 1
   :tags: someip_compat
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall support running multiple major protocol
   versions of the same service at the same time. Each version shall use
   its own communication endpoint. This lets clients built against
   different major versions of a service definition be served at the
   same time, without the versions interfering with each other.

   Refines :need:`feat_req__someip__compat`.

   Cf. Open SOME/IP Specification, someip-compat.rst, Supporting multiple
   versions of the same service; covers spec requirement IDs:
   feat_req_someipcompat_714, feat_req_someipcompat_799,
   feat_req_someipcompat_800, feat_req_someipcompat_802,
   feat_req_someipcompat_804, feat_req_someipcompat_803,
   feat_req_someipcompat_801.
