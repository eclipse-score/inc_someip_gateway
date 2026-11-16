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

serializer Component Requirements
=================================

These requirements cover the serializer plugin interface and the null
serializer that passes pre-serialized payloads through unchanged.

.. comp_req:: Serializer reports failure instead of invalid data
   :id: comp_req__serializer__reports_failure
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Interface
   :satisfied_by: comp__serializer

   The serializer shall return a result code other than
   ``score_com_serializer_result_ok`` when a serialization or
   deserialization cannot be completed, and shall not report a written
   size for a failed serialization.

.. comp_req:: Null serializer preserves payload bytes
   :id: comp_req__serializer__null_preserves_payload
   :status: valid
   :version: 1
   :safety: ASIL_B
   :security: NO
   :reqtype: Functional
   :satisfied_by: comp__null_serializer

   The null serializer shall copy the payload bytes of a message
   unchanged between the application data type and the SOME/IP payload
   buffer, and shall fail with a serialization or deserialization
   failure result when the payload exceeds the target buffer size.
