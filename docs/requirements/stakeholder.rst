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

Stakeholder Requirements
========================

This file contains stakeholder-level requirements for the SOME/IP Gateway.
Stakeholder requirements capture high-level needs from users, integrators,
and safety standards. They are the top of the requirements hierarchy —
feature and component requirements derive from these.

.. stkh_req:: SOME/IP Wire-Level Protocol Interoperability
   :id: stkh_req__someip_gw__interoperability
   :status: valid
   :safety: QM
   :security: NO
   :reqtype: Functional
   :rationale: Automotive ECUs from different manufacturers communicate over SOME/IP. The SOME/IP Gateway network-facing component must conform to the SOME/IP wire-level protocol specification so that it interoperates with any compliant SOME/IP implementation.

   The SOME/IP Gateway shall ensure that its SOME/IP network interface
   component conforms to the SOME/IP wire-level protocol specification,
   enabling correct interoperation with SOME/IP-compliant devices.

.. note::

   This is a module-local stakeholder requirement. An upstream S-CORE
   stakeholder requirement for SOME/IP interoperability does not yet exist.
   If one is defined in a future S-CORE release, the ``:satisfies:`` link
   on the feature requirement should be updated to reference it.
