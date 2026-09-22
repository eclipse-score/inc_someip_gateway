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

SOME/IP-RPC Serialization of Parameters and Data Structures (Section-Level)
=============================================================================

These are Tier-2, section-level requirements for the "Serialization of
Parameters and Data Structures" subtree of the ``someip-rpc`` chapter of
the Open SOME/IP Specification. Each requirement below refines
:need:`feat_req__someip__rpc` and summarizes the normative scope of one
specification section. It does not copy the specification text word for
word.

.. feat_req:: SOME/IP-RPC Serialization Overview
   :id: feat_req__someip__rpc_ser_overview
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize parameter lists in the order the
   interface specification defines. When deserializing, it shall accept
   trailing parameters added to a previously known parameter list,
   without treating them as an error. It shall not add alignment padding
   on its own: any padding the interface specification's memory layout
   needs must be spelled out explicitly by that specification and
   calculated from the start of the SOME/IP message.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures; covers spec requirement IDs:
   feat_req_someip_168, feat_req_someip_169, feat_req_someip_711.

.. feat_req:: SOME/IP-RPC Basic Datatype Serialization
   :id: feat_req__someip__rpc_basic_types
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall support serializing and deserializing the
   full basic datatype set (booleans, signed and unsigned integers up to
   64 bits, and 32/64-bit IEEE 754 floating point numbers), using the
   byte order the interface specification defines for each parameter. It
   shall support uint64/sint64 at least on infotainment ECUs. It shall
   encode boolean values using only the lowest bit, treating the
   remaining bits as reserved (set to 0 when sending, ignored when
   receiving).

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Basic Datatypes; covers spec
   requirement IDs: feat_req_someip_172, feat_req_someip_224,
   feat_req_someip_623, feat_req_someip_817.

.. feat_req:: SOME/IP-RPC Structured Datatype (Struct) Serialization
   :id: feat_req__someip__rpc_structured_types
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize structs by writing their members
   into the buffer in order, exactly as the interface specification
   states, without adding automatic alignment or padding. When the
   interface specification adds an optional 8/16/32-bit length field
   before a struct (no field present means a length of zero), the
   Gateway shall read that field as the struct's byte count. It shall
   interpret only the bytes it knows from the interface specification
   and skip any remaining bytes the length field indicates.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Structured Datatypes (structs);
   covers spec requirement IDs: feat_req_someip_230,
   feat_req_someip_577, feat_req_someip_575, feat_req_someip_574,
   feat_req_someip_231, feat_req_someip_600, feat_req_someip_602,
   feat_req_someip_601.

.. feat_req:: SOME/IP-RPC Fixed-Length String Serialization
   :id: feat_req__someip__rpc_ser_str_fixed
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize fixed-length strings as
   Unicode-encoded buffers (UTF-8, UTF-16BE, or UTF-16LE, as the
   interface specification defines), using the byte length the interface
   specification gives. Each string shall start with a Byte Order Mark
   that counts toward that length, and end with null-character padding
   (a single 0x00 byte for UTF-8, or a pair of 0x00 bytes for UTF-16,
   ignoring the trailing byte of an odd-length UTF-16 buffer). The
   Gateway shall check a received string's Byte Order Mark against the
   interface specification.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Strings (fixed length); covers spec
   requirement IDs: feat_req_someip_233, feat_req_someip_234,
   feat_req_someip_687, feat_req_someip_639, feat_req_someip_640,
   feat_req_someip_641, feat_req_someip_662, feat_req_someip_800,
   feat_req_someip_666, feat_req_someip_235.

.. feat_req:: SOME/IP-RPC Dynamic-Length String Serialization
   :id: feat_req__someip__rpc_ser_str_dynamic
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize dynamic-length strings with a
   leading length field (8, 16, or 32 bits, as the interface
   specification defines, defaulting to 32 bits when unspecified). That
   field shall count only the string bytes that follow it, not itself.
   The Gateway shall apply the same encoding, Byte Order Mark, and
   termination rules used for fixed-length strings, and shall add extra
   null characters when the interface specification requires alignment
   of the data element that follows.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Strings (dynamic length); covers
   spec requirement IDs: feat_req_someip_237, feat_req_someip_642,
   feat_req_someip_582, feat_req_someip_581, feat_req_someip_562,
   feat_req_someip_238, feat_req_someip_239.

.. feat_req:: SOME/IP-RPC Fixed-Length Array Serialization
   :id: feat_req__someip__rpc_arrays
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize fixed-length arrays using the
   element count the interface specification fixes, carried implicitly
   (no length field on the wire). The number of elements shall match
   what the interface specification declares for that array.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Arrays (fixed length); covers spec
   requirement IDs: feat_req_someip_241.

.. feat_req:: SOME/IP-RPC One-Dimensional Fixed-Length Array Layout
   :id: feat_req__someip__rpc_one_dimensional
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize a one-dimensional fixed-length
   array of n elements as exactly n elements of the same type, one after
   another, with no length field or other framing between or around
   them.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Arrays (fixed length) >
   One-dimensional; covers spec requirement IDs: feat_req_someip_243,
   feat_req_someip_244.

.. feat_req:: SOME/IP-RPC Multidimensional Fixed-Length Array Layout
   :id: feat_req__someip__rpc_multidimensional
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize multidimensional fixed-length
   arrays using the same in-memory row-major ordering that C++ uses for
   multidimensional arrays, without any extra per-dimension framing.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Arrays (fixed length) >
   Multidimensional; covers spec requirement IDs: feat_req_someip_246,
   feat_req_someip_247.

.. feat_req:: SOME/IP-RPC Optional Parameter and Element Serialization
   :id: feat_req__someip__rpc_ser_optional
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize an optional element as a
   dynamic-length array holding zero or one elements, using the same
   framing rules it applies to dynamic-length arrays.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Optional Parameters / Optional
   Elements; covers spec requirement IDs: feat_req_someip_252.

.. feat_req:: SOME/IP-RPC Dynamic-Length Array Serialization
   :id: feat_req__someip__rpc_dyn_length_arrays
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize dynamic-length arrays with a
   length field sized as the interface specification defines (0, 8, 16,
   or 32 bits, defaulting to 32 bits). This field shall count only the
   array's payload bytes, not itself, and shall be zero for an empty
   array. For one-dimensional dynamic arrays, a single length field
   comes before the elements. For multidimensional dynamic arrays, each
   dimension needs its own length field. When static buffers are
   needed, the interface specification shall set a maximum length for
   each dimension.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Dynamic Length Arrays; covers spec
   requirement IDs: feat_req_someip_254, feat_req_someip_621,
   feat_req_someip_255, feat_req_someip_256, feat_req_someip_257,
   feat_req_someip_258, feat_req_someip_259, feat_req_someip_260.

.. feat_req:: SOME/IP-RPC Enumeration Serialization
   :id: feat_req__someip__rpc_enumeration
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize enumerations using the unsigned
   integer basic datatype (uint8, uint16, uint32, or uint64) that the
   interface specification declares for that enumeration. Unless
   configured otherwise, it shall be able to send and receive
   enumeration values that the enumeration does not define.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Enumeration; covers spec requirement
   IDs: feat_req_someip_651, feat_req_someip_799.

.. feat_req:: SOME/IP-RPC Bitfield Serialization
   :id: feat_req__someip__rpc_bitfield
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize bitfields as one of the unsigned
   integer basic datatypes uint8, uint16, or uint32, following a
   per-bit and per-value naming scheme the interface specification
   defines.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Bitfield; covers spec requirement
   IDs: feat_req_someip_689, feat_req_someip_690, feat_req_someip_691.

.. feat_req:: SOME/IP-RPC Union / Variant Serialization
   :id: feat_req__someip__rpc_union_variant
   :status: valid
   :version: 1
   :tags: someip_rpc
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway shall serialize a union (a parameter that carries
   exactly one of several declared alternative types) as a length field,
   followed by a type field, followed by the serialized element. By
   default it shall use Network Byte Order and 32-bit field widths,
   unless the interface specification configures otherwise. The length
   field (0, 8, 16, or 32 bits) shall give the byte size of the type
   and element together, not counting the length and type fields
   themselves. A zero-bit length field means all alternative types share
   the same length; if the interface specification breaks that rule, the
   Gateway shall raise a warning and use the longest alternative's
   length with zero padding. The type field (8, 16, or 32 bits) shall
   number the alternatives in ascending order starting at 1, with 0
   reserved to mean an empty (NULL) union where the interface
   specification allows it. Any bytes the length field covers beyond the
   serialized element's own length shall be treated as padding and
   skipped when deserializing.

   Refines :need:`feat_req__someip__rpc`.

   Cf. Open SOME/IP Specification, someip-rpc.rst, Serialization of
   Parameters and Data Structures > Union / Variant; covers spec
   requirement IDs: feat_req_someip_263, feat_req_someip_264,
   feat_req_someip_573, feat_req_someip_563, feat_req_someip_571,
   feat_req_someip_572, feat_req_someip_583, feat_req_someip_566,
   feat_req_someip_272, feat_req_someip_564, feat_req_someip_565,
   feat_req_someip_273, feat_req_someip_274.
