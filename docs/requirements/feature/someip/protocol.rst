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

Open SOME/IP Specification Requirements
========================================

These are feature-level requirements taken from the Open SOME/IP
Specification (`open-someip-spec
<https://github.com/some-ip-com/open-someip-spec>`_, license
``Community-Spec-1.0``). Each requirement below summarizes the normative
scope of one specification chapter and names the chapter or section it
comes from. It does not copy the specification text word for word.

These requirements are the main source of truth, at the requirements
level, for the SOME/IP wire protocol. Only the ``someip-rpc`` and
``someip-sd`` chapters have component-level (``someipd``) requirements
and TC8 test coverage today. The other chapters are imported for
completeness and have no ``comp_req`` or TC8 links yet.

.. feat_req:: SOME/IP RPC Protocol
   :id: feat_req__someip__rpc
   :status: valid
   :version: 1
   :tags: someip, rpc, wire_protocol
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall implement the SOME/IP RPC wire protocol as
   defined by the Open SOME/IP Specification. This covers the 16-byte
   message header (Message ID, Length, Request ID, Protocol/Interface
   Version, Message Type, Return Code). It also covers parameter
   serialization for basic types, structs, strings, arrays, enums,
   bitfields, unions, and optionals; the UDP and TCP transport bindings;
   and the request/response, fire-and-forget, event, and field
   communication patterns, including error handling through return codes.

   Cf. Open SOME/IP Specification, ``someip-rpc.rst`` (SOME/IP RPC Protocol
   Specification).

.. feat_req:: SOME/IP Service Discovery Protocol
   :id: feat_req__someip__sd
   :status: valid
   :version: 1
   :tags: someip, service_discovery, wire_protocol
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall implement the SOME/IP Service Discovery
   (SOME/IP-SD) protocol as defined by the Open SOME/IP Specification.
   This covers the SD message format: the SD header, the Entry format,
   and the Options format (including IPv4/IPv6 endpoint, multicast,
   SD-endpoint, configuration, load-balancing, and MAC-groupcast
   options). It also covers the Find, Offer, StopOffer, Subscribe,
   StopSubscribe, Ack, and Nack entry types, and the startup, cyclic
   offer, response, and shutdown timing behavior, along with the state
   machines behind it.

   Cf. Open SOME/IP Specification, ``someip-sd.rst`` (SOME/IP Service
   Discovery).

.. feat_req:: SOME/IP-TP Segmentation
   :id: feat_req__someip__tp
   :status: valid
   :version: 1
   :tags: someip, tp, segmentation, wire_protocol
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall implement the SOME/IP Transport Protocol
   (SOME/IP-TP) as defined by the Open SOME/IP Specification, for SOME/IP
   messages that are too large for the UDP payload limit. This covers
   sender-side segmentation (segment ordering, sizing, and
   non-duplication), and receiver-side reassembly, buffering, and error
   handling for segments that arrive out of order or incomplete.

   Cf. Open SOME/IP Specification, ``someip-tp.rst`` (Transporting large
   SOME/IP messages over UDP).

.. feat_req:: SOME/IP Migration and Compatibility
   :id: feat_req__someip__compat
   :status: valid
   :version: 1
   :tags: someip, compatibility, versioning, wire_protocol
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall support forward-compatible handling of
   SOME/IP messages, as defined by the Open SOME/IP Specification. This
   includes tolerating messages that are longer than expected, missing
   optional parameters, and unknown message types. The Gateway shall also
   support running multiple major protocol versions of the same service
   at the same time, using a separate communication endpoint for each
   version.

   Cf. Open SOME/IP Specification, ``someip-compat.rst`` (Migration and
   Compatibility).

.. feat_req:: SOME/IP Reserved Identifiers
   :id: feat_req__someip__ids
   :status: valid
   :version: 1
   :tags: someip, ids, wire_protocol
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The ``someip-ids`` chapter of the Open SOME/IP Specification is
   informative reference content, not a set of normative requirements.
   Every block in that chapter is a reserved-identifier table tagged
   ``Information``, and none of them uses normative wording. So this node
   does not derive from a specification requirement. Instead, it records
   a project-derived design constraint: the SOME/IP Gateway shall not
   assign the reserved Service ID, Instance ID, Method ID, Event ID, or
   Eventgroup ID values from that chapter (including ``0x0000``,
   ``0xFFFF``, and the Enhanced Testability service ID ``0x0101``) to
   application-defined SOME/IP identifiers.

   Cf. Open SOME/IP Specification, ``someip-ids.rst`` (Reserved and
   Special Identifiers for SOME/IP and SOME/IP-SD): eight informative
   reserved-identifier tables, zero normative requirement blocks.
