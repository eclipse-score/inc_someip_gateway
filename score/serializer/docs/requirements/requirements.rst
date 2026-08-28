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

Component Serializer Requirements
#################################

Functional Requirements
-----------------------

.. comp_req:: Null-passthrough serializer for pre-serialized payloads
   :id: comp_req__serializer__null_passthrough
   :reqtype: Functional
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__serializer

   The serializer shall provide a null-passthrough implementation
   (``NullSerializer``) for payloads that arrive already serialized
   (``PreSerializedData``), so the fast path can avoid a second
   serialization pass while still satisfying the generic
   ``Serializer`` interface.

.. comp_req:: C ABI-stable serializer plugin interface
   :id: comp_req__serializer__c_abi_plugin
   :reqtype: Interface
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__serializer

   The serializer shall expose an ``extern "C"`` interface
   (``score_com_serializer_serialize`` / ``_deserialize`` / ``_get`` /
   ``_init`` / ``_deinit``) so different serializer implementations
   (null, IDL-generated, third-party) can be loaded as ABI-compatible
   plugins without recompiling the gateway.

.. comp_req:: Per-element serializer lookup by service type and element kind
   :id: comp_req__serializer__element_lookup
   :reqtype: Interface
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__serializer

   ``score_com_serializer_get`` shall return the serializer matching a
   given ``service_type`` and element kind (``event``, ``field``,
   ``method_call``, ``method_response``) so events, fields and
   request/response method payloads can each be (de)serialized with
   the right layout without additional dispatch in the caller.

