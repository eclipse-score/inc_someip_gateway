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

SOME/IP RPC Wire Format (Section-Level)
=========================================

These are Tier-2, section-level requirements for the ``someip-rpc``
chapter of the Open SOME/IP Specification, covering identifier
definitions through the on-wire RPC header. Each requirement below
refines :need:`feat_req__someip__rpc` and summarizes the normative scope
of one specification section. It does not copy the specification text
word for word.

.. feat_req:: SOME/IP Identifier Definitions
   :id: feat_req__someip__rpc_def_ids
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall identify services, service instances,
   methods, events, and eventgroups using separate 16-bit unsigned
   identifiers (Service ID, Service Instance ID, Method/Event ID,
   Eventgroup ID). Each identifier shall stay unique within its own scope
   across the vehicle. The Gateway shall treat Service ID 0xFFFE as
   meaning a non-SOME/IP service, and shall not use the reserved Service
   Instance ID values 0x0000 and 0xFFFF for an actual service instance.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Definition of terms >
   Definition of Identifiers; covers spec requirement IDs:
   feat_req_someip_538, feat_req_someip_539, feat_req_someip_624,
   feat_req_someip_627, feat_req_someip_541, feat_req_someip_542,
   feat_req_someip_543, feat_req_someip_579, feat_req_someip_544,
   feat_req_someip_625, feat_req_someip_545, feat_req_someip_546,
   feat_req_someip_547.

.. feat_req:: SOME/IP Transport Protocol Selection
   :id: feat_req__someip__rpc_tport_proto
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall transport SOME/IP messages over UDP and/or
   TCP, based on its configuration. It shall get the IP addresses and
   port numbers it uses either from that configuration or from the port
   numbers a remote server announces through SOME/IP-SD. The Gateway
   shall reserve port 30490 (UDP and TCP) only for SOME/IP-SD traffic,
   never for application payloads, unless the configuration explicitly
   overrides that port for discovery.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Transport Protocol;
   covers spec requirement IDs: feat_req_someip_32, feat_req_someip_659,
   feat_req_someip_660, feat_req_someip_658, feat_req_someip_676,
   feat_req_someip_661.

.. feat_req:: SOME/IP Wire Endianness
   :id: feat_req__someip__rpc_endianness
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall encode and decode every RPC header field in
   network byte order (big endian). It shall also encode and decode any
   length or type field inside the payload in network byte order,
   regardless of how the payload's own parameters are ordered by the
   applicable interface specification.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Endianness; covers spec
   requirement IDs: feat_req_someip_42, feat_req_someip_675.

.. feat_req:: SOME/IP Header Layout
   :id: feat_req__someip__rpc_header
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall produce and accept the same SOME/IP header
   field layout on every transport binding. This keeps interoperability
   with other implementations independent of the underlying protocol.
   When End-to-End communication protection is configured for a message,
   the Gateway shall insert the E2E header right after the Return Code
   field, at the position that configuration determines.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header; covers spec
   requirement IDs: feat_req_someip_44, feat_req_someip_45,
   feat_req_someip_102, feat_req_someip_103.

.. feat_req:: SOME/IP Header IP Address and Port Number Handling
   :id: feat_req__someip__rpc_ip_addr_port_numbers
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall apply the basic SOME/IP header layout on top
   of whatever IP address and transport-layer port combination is in use
   for a given message, no matter which transport protocol binding is
   selected.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > IP-Address /
   port numbers; covers spec requirement IDs: feat_req_someip_47.

.. feat_req:: Mapping IP Addresses and Ports in Response and Error Messages
   :id: feat_req__someip__rpc_hdr_ip_resp_map
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall send response and error messages back to the
   originating client using the same IP address and port pairing that
   was set up for the matching request. This lets a client match a reply
   to the call it made, regardless of intermediate routing.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > IP-Address /
   port numbers > Mapping of IP Addresses and Ports in Response and Error
   Messages; covers spec requirement IDs: feat_req_someip_49.

.. feat_req:: SOME/IP Message ID Field
   :id: feat_req__someip__rpc_msg_id
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall use the 32-bit Message ID header field to
   uniquely identify the method or event being called or notified. It
   shall use this field to route an incoming RPC call to the correct
   method handler of the target application.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > Message ID [32
   bit]; covers spec requirement IDs: feat_req_someip_56.

.. feat_req:: Structure of the Message ID
   :id: feat_req__someip__rpc_struct_msg_id
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall build the Message ID from the Service ID and
   a Method/Event ID subfield. It shall keep method identifiers and
   event/notification identifiers in their own numeric ranges, so a
   receiver can tell a method call apart from an event notification
   using only the Message ID.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > Message ID [32
   bit] > Structure of the Message ID; covers spec requirement IDs:
   feat_req_someip_59, feat_req_someip_60, feat_req_someip_67.

.. feat_req:: SOME/IP Length Field
   :id: feat_req__someip__rpc_length
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall compute the 32-bit Length header field as
   the number of bytes from the start of the Request ID field to the end
   of the message. It shall treat any received SOME/IP message with a
   Length field value smaller than 8 bytes as invalid and discard it.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > Length [32
   bit]; covers spec requirement IDs: feat_req_someip_77,
   feat_req_someip_798.

.. feat_req:: SOME/IP Request ID Field
   :id: feat_req__someip__rpc_request_id
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall use the Request ID header field to let a
   client tell apart concurrent, outstanding calls to the same method.
   This ID only needs to be unique for a given client-server pair. The
   Gateway shall echo the Request ID of a request unchanged in the
   matching response.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > Request ID [32
   bit]; covers spec requirement IDs: feat_req_someip_79.

.. feat_req:: Structure of the Request ID
   :id: feat_req__someip__rpc_struct_request_id
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall build the Request ID from a Client ID that
   identifies the calling client (optionally with a configurable prefix
   for uniqueness across the vehicle) and a Session ID chosen per call.
   It shall set the Session ID to 0x0000 when session handling is
   disabled. When session handling is enabled, it shall start counting
   from 0x0001 and wrap back to 0x0001 after reaching 0xFFFF. The Gateway
   shall always use session handling for Request/Response methods.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > Request ID [32
   bit] > Structure of the Request ID; covers spec requirement IDs:
   feat_req_someip_83, feat_req_someip_699, feat_req_someip_701,
   feat_req_someip_88, feat_req_someip_700, feat_req_someip_649,
   feat_req_someip_677, feat_req_someip_669, feat_req_someip_667.

.. feat_req:: SOME/IP Protocol Version Field
   :id: feat_req__someip__rpc_proto_ver
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall set the 8-bit Protocol Version header field
   to the current SOME/IP protocol version value (0x01), and shall check
   this field on received messages.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > Protocol
   Version [8 bit]; covers spec requirement IDs: feat_req_someip_90.

.. feat_req:: SOME/IP Interface Version Field
   :id: feat_req__someip__rpc_interface_ver
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall set the 8-bit Interface Version header field
   to the major version of the service interface being addressed, and
   shall use it to tell incompatible major revisions of the same service
   apart.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > Interface
   Version [8 bit]; covers spec requirement IDs: feat_req_someip_92.

.. feat_req:: SOME/IP Message Type Field
   :id: feat_req__someip__rpc_msg_type
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall set the 8-bit Message Type header field to
   tell request, response, error/exception, notification, and
   Fire-and-Forget messages apart. It shall answer a regular request
   with a matching response (using a non-zero Return Code on error, or a
   dedicated exception message type where configured). It shall set the
   TP-Flag bit of this field when the message being sent is a
   SOME/IP-TP segment.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > Message Type [8
   bit]; covers spec requirement IDs: feat_req_someip_95,
   feat_req_someip_141, feat_req_someip_726, feat_req_someip_761.

.. feat_req:: SOME/IP Return Code Field
   :id: feat_req__someip__rpc_return_code
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall set the 8-bit Return Code header field on
   every outgoing SOME/IP message to show whether the matching request
   was processed successfully. This lets a receiver tell success from
   failure without looking at the payload.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > Return Code [8
   bit]; covers spec requirement IDs: feat_req_someip_144.

.. feat_req:: SOME/IP Payload Field
   :id: feat_req__someip__rpc_payload
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall carry the RPC call's or event's parameters
   in the variable-size Payload field that follows the fixed header. It
   shall serialize and deserialize that payload according to the rules
   defined for parameter serialization.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Header > Payload
   [variable size]; covers spec requirement IDs: feat_req_someip_165.
