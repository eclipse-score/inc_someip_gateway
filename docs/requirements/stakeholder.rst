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
   # AI Disclosure: Assisted by OpenAI Codex (GPT-6.1 Sol).
   # *******************************************************************************

Stakeholder Requirements
========================

This file contains stakeholder-level requirements for the SOME/IP Gateway.
Stakeholder requirements capture high-level needs from users, integrators,
and safety standards. They are the top of the requirements hierarchy;
feature and component requirements derive from these.

.. stkh_req:: SOME/IP Wire-Level Protocol Interoperability
   :id: stkh_req__someip_gw__interoperability
   :status: valid
   :version: 1
   :valid_from: v0.0.0
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
   If one is defined in a future S-CORE release, the ``:derived_from:`` link
   on the feature requirement(s) below should be updated to reference it.

   ``feat_req__tc8_conformance__conformance`` (see
   :doc:`/tc8_conformance/requirements`) derives from this stakeholder
   requirement: TC8 wire-level conformance testing is the verification
   activity for this interoperability need.

SOCom version selection proposal
--------------------------------

These requirements are **proposals derived from issue #84**, pending committer
review. The pinned docs-as-code 8.2.0 metamodel permits only ``valid`` or
``invalid`` requirement status. Consequently, ``invalid`` with the
``proposal`` tag means not yet accepted here; it does not claim that the
implemented behavior has failed verification. QM and security NO follow the
existing SOCom component declaration and remain subject to classification review.
No approved release applicability or stakeholder acceptance is asserted.

.. stkh_req:: Unambiguous local service version selection
   :id: stkh_req__someip_gw__local_version_selection
   :status: invalid
   :version: 1
   :tags: proposal, issue_84
   :valid_from: v0.1
   :safety: QM
   :security: NO
   :reqtype: Functional
   :rationale: Issue 84 asks for separate service and full instance identities and optional instance/minor discovery filters; application users need predictable local version selection.

   Application developers shall be able to distinguish a service contract from
   an offered instance and request compatible instances in the local SOCom
   runtime using an exact service ID and major version.

   This is a proposed module-local stakeholder need, not an existing accepted
   S-CORE stakeholder requirement. ``valid_from`` is a proposed applicability
   window required by the metamodel, not release acceptance. Network discovery
   and wire-level TC8 conformance remain outside this requirement.
