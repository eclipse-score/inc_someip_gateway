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

Component someip Requirements
#############################

Functional Requirements
-----------------------

.. comp_req:: SOME/IP protocol type and constant definitions
   :id: comp_req__someip__protocol_types
   :reqtype: Interface
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__someip

   The ``someip`` library shall provide the header field types
   (``types.h``), protocol constants (``constants.h``) and error codes
   (``someip_error.h``) required by the Open SOME/IP protocol
   specification, so that downstream components (``someipd``,
   ``gatewayd``) share a single canonical definition of the on-wire
   protocol elements.

