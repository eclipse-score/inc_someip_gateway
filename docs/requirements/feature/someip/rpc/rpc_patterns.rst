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

SOME/IP RPC Transport Bindings and Communication Patterns (Section-Level)
===========================================================================

These are Tier-2, section-level requirements for the ``someip-rpc``
chapter of the Open SOME/IP Specification, covering the transport
protocol bindings, request/response, fire-and-forget, events, fields, and
error handling sections. Each requirement below refines
:need:`feat_req__someip__rpc` and summarizes the normative scope of one
specification section. It does not copy the specification text word for
word.

.. feat_req:: SOME/IP RPC Transport Protocol Bindings Overview
   :id: feat_req__someip__rpc_tport_proto_bindings
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall support both UDP and TCP as transport
   bindings for SOME/IP RPC messages, selecting the right binding for
   each service instance. This lets a service be reached over the
   transport its endpoint configuration names.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Transport Protocol
   Bindings; covers spec requirement IDs: feat_req_someip_648,
   feat_req_someip_702, feat_req_someip_741, feat_req_someip_664,
   feat_req_someip_732, feat_req_someip_733.

.. feat_req:: SOME/IP RPC UDP Binding Behavior
   :id: feat_req__someip__rpc_udp_binding
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   When bound to UDP, the SOME/IP Gateway shall send and receive SOME/IP
   messages as UDP datagram payloads. It shall support packing multiple
   SOME/IP messages into a single UDP datagram when the datagram size
   allows it. If a datagram does not hold a whole number of messages, it
   shall treat that as an error instead of partially processing it.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Transport Protocol
   Bindings, UDP Binding; covers spec requirement IDs:
   feat_req_someip_318, feat_req_someip_319, feat_req_someip_584,
   feat_req_someip_811, feat_req_someip_812, feat_req_someip_814,
   feat_req_someip_813.

.. feat_req:: SOME/IP RPC TCP Binding Behavior
   :id: feat_req__someip__rpc_tcp_binding
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   When bound to TCP, the SOME/IP Gateway shall treat the TCP byte
   stream as a series of SOME/IP messages back to back, each one marked
   off by its own length field. It shall keep connections open where the
   configuration calls for it, and shall recover the message boundary
   after a stream error instead of leaving the connection permanently
   out of sync.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Transport Protocol
   Bindings, TCP Binding; covers spec requirement IDs:
   feat_req_someip_585, feat_req_someip_325, feat_req_someip_326,
   feat_req_someip_644, feat_req_someip_645, feat_req_someip_646,
   feat_req_someip_647, feat_req_someip_678, feat_req_someip_679,
   feat_req_someip_680.

.. feat_req:: SOME/IP RPC TCP Stream Resynchronization via Magic Cookies
   :id: feat_req__someip__rpc_tcp_magic_cookies
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall recognize the reserved SOME/IP-TP Magic
   Cookie message on a TCP stream, and use it to restore message
   boundary alignment once it detects a desync. It shall also send
   Magic Cookies periodically on outbound TCP streams, so peers can
   resynchronize against this gateway's stream.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Transport Protocol
   Bindings, TCP Binding, Allowing resync to TCP stream using Magic
   Cookies; covers spec requirement IDs: feat_req_someip_586,
   feat_req_someip_591, feat_req_someip_592, feat_req_someip_593,
   feat_req_someip_594, feat_req_someip_609, feat_req_someip_607,
   feat_req_someip_589.

.. feat_req:: SOME/IP RPC Multiple Service-Instance Handling
   :id: feat_req__someip__rpc_multi_svc_instances
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall support multiple instances of the same
   service, each reachable through its own endpoint. It shall address
   each instance without ambiguity, so RPC traffic for one instance is
   never sent to another.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Transport Protocol
   Bindings, Multiple Service-Instances; covers spec requirement IDs:
   feat_req_someip_636, feat_req_someip_1079, feat_req_someip_445,
   feat_req_someip_967.

.. feat_req:: SOME/IP Request/Response Communication Pattern
   :id: feat_req__someip__rpc_request_response_comm
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall support the Request/Response communication
   pattern by pairing each REQUEST message with exactly one RESPONSE (or
   ERROR) message that carries the matching Client ID and Session ID.
   This lets a caller match the reply to the request it sent.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Request/Response
   Communication; covers spec requirement IDs: feat_req_someip_329,
   feat_req_someip_338.

.. feat_req:: SOME/IP Fire and Forget Communication Pattern
   :id: feat_req__someip__rpc_fire_forget_comm
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall support the Fire-and-Forget communication
   pattern by delivering a REQUEST_NO_RETURN message to its destination,
   without generating or expecting a RESPONSE message in reply.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Fire&Forget
   Communication; covers spec requirement IDs: feat_req_someip_345,
   feat_req_someip_348.

.. feat_req:: SOME/IP Event Notification Delivery
   :id: feat_req__someip__rpc_events
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall deliver event notifications as one-way
   NOTIFICATION messages to subscribed consumers, without expecting a
   response. It shall link each event to the eventgroup it was
   subscribed under, for delivery routing.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Events; covers spec
   requirement IDs: feat_req_someip_354, feat_req_someip_804,
   feat_req_someip_806, feat_req_someip_807.

.. feat_req:: SOME/IP Event Publish/Subscribe Handling
   :id: feat_req__someip__rpc_pub_sub_hdl
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall deliver an event to a consumer only while
   that consumer has an active subscription to the event's eventgroup.
   It shall stop delivering the event once the subscription ends.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Events,
   Publish/Subscribe Handling; covers spec requirement IDs:
   feat_req_someip_361.

.. feat_req:: SOME/IP Field Getter, Setter, and Notification Handling
   :id: feat_req__someip__rpc_fields
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall represent a field's current value through
   its getter/setter RPC methods and its notification event. It shall
   keep all three access paths consistent with the same underlying
   value.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Fields; covers spec
   requirement IDs: feat_req_someip_631, feat_req_someip_632,
   feat_req_someip_633, feat_req_someip_634, feat_req_someip_635.

.. feat_req:: SOME/IP Application Error Code and Exception Transport
   :id: feat_req__someip__rpc_err_app_codes
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall carry application-level error information
   (application-defined error codes and exceptions) inside an ERROR
   message's Return Code and payload, kept separate from protocol-level
   errors. This lets the caller tell a rejected request apart from a
   failure the application itself signaled.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Error Handling,
   Transporting Application Error Codes and Exceptions; covers spec
   requirement IDs: feat_req_someip_367, feat_req_someip_106,
   feat_req_someip_107, feat_req_someip_101.

.. feat_req:: SOME/IP Error Handling Return Code Semantics
   :id: feat_req__someip__rpc_err_return_code
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall set the Return Code of an ERROR message to
   the value that names the specific protocol-level failure it
   encountered (such as an unknown service, unknown method, or
   malformed message), instead of a generic failure indicator. This lets
   a caller tell different failure causes apart.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Error Handling,
   Return Code; covers spec requirement IDs: feat_req_someip_727,
   feat_req_someip_597, feat_req_someip_654, feat_req_someip_655,
   feat_req_someip_703, feat_req_someip_371, feat_req_someip_598,
   feat_req_someip_721, feat_req_someip_704.

.. feat_req:: SOME/IP Error Processing Overview
   :id: feat_req__someip__rpc_error_proc_overview
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall detect protocol-level errors while
   processing a message, and respond with an ERROR message that carries
   the matching Client ID and Session ID of the request that caused it.
   It shall not silently drop the request or fall back to unrelated
   behavior instead.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Error Handling,
   Error Processing Overview; covers spec requirement IDs:
   feat_req_someip_719, feat_req_someip_718, feat_req_someip_816,
   feat_req_someip_818.
