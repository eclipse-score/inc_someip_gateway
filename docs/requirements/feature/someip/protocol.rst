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

Feature-level requirements derived from the Open SOME/IP Specification
(`open-someip-spec <https://github.com/some-ip-com/open-someip-spec>`_,
license ``Community-Spec-1.0``). Each requirement below paraphrases the
normative scope of one specification chapter and cites the chapter/section
it is derived from; it does not reproduce specification prose verbatim.

These requirements are the SOME/IP Gateway's requirements-level source of
truth for the SOME/IP wire protocol. Only the ``someip-rpc`` and
``someip-sd`` chapters currently have component-level (``someipd``) and
TC8 test coverage; the remaining chapters are imported for completeness
and currently have no ``comp_req`` or TC8 linkage.

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
   defined by the Open SOME/IP Specification, covering the 16-byte message
   header (Message ID, Length, Request ID, Protocol/Interface Version,
   Message Type, Return Code), parameter serialization for basic types,
   structs, strings, arrays, enums, bitfields, unions and optionals, the
   UDP and TCP transport bindings, and the request/response,
   fire-and-forget, event, and field communication patterns including
   error handling via return codes.

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
   (SOME/IP-SD) protocol as defined by the Open SOME/IP Specification,
   covering the SD message format (SD header, Entry format, and Options
   format including IPv4/IPv6 endpoint, multicast, SD-endpoint,
   configuration, load-balancing, and MAC-groupcast options), the
   Find/Offer/StopOffer/Subscribe/StopSubscribe/Ack/Nack entry types, and
   the startup, cyclic offer, response, and shutdown timing behavior and
   associated state machines.

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
   (SOME/IP-TP) as defined by the Open SOME/IP Specification when
   transporting SOME/IP messages that exceed the UDP payload size limit,
   covering sender-side segmentation (segment ordering, sizing, and
   non-duplication) and receiver-side reassembly, buffering, and error
   handling for out-of-order or incomplete segments.

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
   SOME/IP messages as defined by the Open SOME/IP Specification,
   including tolerance of longer-than-expected messages, missing optional
   parameters, and unknown message types, and shall support concurrent
   operation of multiple major protocol versions of the same service via
   distinct communication endpoints.

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

   The SOME/IP Gateway shall respect the reserved and special identifier
   values for Service IDs, Instance IDs, Method IDs, Event IDs, and
   Eventgroup IDs defined by the Open SOME/IP Specification, including the
   reserved values ``0x0000`` and ``0xFFFF`` and the Enhanced Testability
   service ID ``0x0101``, and shall not assign these reserved values to
   application-defined SOME/IP identifiers.

   Cf. Open SOME/IP Specification, ``someip-ids.rst`` (Reserved and
   Special Identifiers for SOME/IP and SOME/IP-SD).
