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

Component Config Requirements
#############################

Functional Requirements
-----------------------

.. comp_req:: FlatBuffers gateway configuration schema
   :id: comp_req__config__flatbuffer_schema
   :reqtype: Interface
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__config

   The gateway configuration shall be defined as a FlatBuffers schema
   (``mw_someip_config.fbs``) together with a JSON schema
   (``mw_someip_config.schema.json``) so both binary and JSON forms of
   the configuration can be validated against a single source of truth.

