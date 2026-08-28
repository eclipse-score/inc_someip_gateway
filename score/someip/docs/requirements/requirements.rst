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

.. comp_req:: 16-bit SOME/IP identifier types
   :id: comp_req__someip__id_widths
   :reqtype: Interface
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__someip

   ``types.h`` shall define ``ServiceId``, ``InstanceId``, ``EventId``
   and ``EventGroupId`` as ``std::uint16_t`` so all in-process
   handling of SOME/IP identifiers matches the 16-bit widths mandated
   by the SOME/IP header layout in the Open SOME/IP specification.

.. comp_req:: Wildcard instance id and maximum message size
   :id: comp_req__someip__protocol_limits
   :reqtype: Interface
   :security: NO
   :safety: QM
   :status: valid
   :version: 1
   :satisfied_by: comp__someip

   ``constants.h`` shall expose the wildcard instance id
   (``kAnyInstance = 0xFFFF``), the SOME/IP header size
   (``kSomeipFullHeaderSize = 16``) and the per-message size cap
   (``kMaxMessageSize = 1500``) chosen to keep single SOME/IP messages
   within a standard Ethernet MTU and avoid IP fragmentation.

