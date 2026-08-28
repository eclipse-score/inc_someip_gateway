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

