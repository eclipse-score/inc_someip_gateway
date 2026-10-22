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

TC8 Conformance Traceability
=============================

This document maps OPEN Alliance TC8 test cases from the external
specification to the project's internal test IDs and component
requirements. The OA specification mapping below is hand maintained.
Requirement to test to result traceability is generated separately,
from the test decorators and the test run's ``test.xml``, and is shown
in the Test Results section at the end of this document.

Source Document
---------------

- **Title:** OA Automotive Ethernet ECU Test Specification, Layer 3-7
- **Version:** v3.0 (Final), May 2020
- **Chapter:** 6, Automotive Protocols (SOME/IP)
- **Publisher:** OPEN Alliance SIG

.. note::

   OA Spec References use test case identifiers from Chapter 6 of the
   OA Automotive Ethernet ECU Test Specification v3.0 (Final), May 2020.
   ``SOMEIPSRV_*`` IDs are from Section 6.1.5 (SOME/IP Server Tests) and
   ``SOMEIP_ETS_*`` IDs are from Section 6.1.6 (Enhanced Testability Service Tests).

Full Traceability Matrix
------------------------

The following tables link each OA TC8 specification test case to the
project's internal test ID and component requirement. All requirement
IDs use the ``comp_req__tc8_conformance__`` prefix (omitted for brevity).

Service Discovery
^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 45 20 35

   * - OA Spec Reference (Ch. 6)
     - Internal ID
     - Requirement
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_14-18
     - TC8-SD-002
     - ``sd_offer_format``
   * - Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_02
     - TC8-SD-003
     - ``sd_cyclic_timing``
   * - Section 6.1.6, SOMEIP_ETS_171
     - TC8-SD-004
     - ``sd_find_response``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_13
     - TC8-SD-006
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_19; Section 6.1.6, SOMEIP_ETS_140
     - TC8-SD-007
     - ``sd_sub_lifecycle``
   * - Section 6.1.6, SOMEIP_ETS_108
     - TC8-SD-008
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_01
     - TC8-SD-009
     - ``sd_phases_timing``
   * - Section 6.1.5.2, SOMEIPSRV_OPTIONS_01-07; Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_08
     - TC8-SD-011
     - ``sd_endpoint_option``
   * - Section 6.1.5.2, SOMEIPSRV_OPTIONS_08-14
     - TC8-SD-013
     - ``sd_mcast_eg``
   * - Section 6.1.6, SOMEIP_ETS_095
     - TC8-SD-014
     - ``sd_sub_lifecycle``

SD Entry Semantics
^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 45 20 35

   * - OA Spec Reference (Ch. 6)
     - Internal ID
     - Requirement
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_01
     - TC8-SDM-001
     - ``sd_find_response``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_02
     - TC8-SDM-002
     - ``sd_find_response``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_03
     - TC8-SDM-003
     - ``sd_find_response``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_04
     - TC8-SDM-004
     - ``sd_find_response``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_05
     - TC8-SDM-005
     - ``sd_find_response``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_06
     - TC8-SDM-006
     - ``sd_find_response``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_18; Section 6.1.6, SOMEIP_ETS_142
     - TC8-SDM-007
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_14-16; Section 6.1.6, SOMEIP_ETS_143
     - TC8-SDM-008
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_17; Section 6.1.6, SOMEIP_ETS_141
     - TC8-SDM-009
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_19
     - TC8-SDM-010
     - ``sd_sub_lifecycle``
   * - Section 6.1.6, SOMEIP_ETS_144
     - TC8-SDM-012
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_03
     - TC8-SDM-013
     - ``sd_find_mcast_rsp``
   * - Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_04
     - TC8-SDM-014
     - ``sd_find_mcast_rsp``

SD Lifecycle Advanced
^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 45 20 35

   * - OA Spec Reference (Ch. 6)
     - Internal ID
     - Requirement
   * - Section 6.1.6, SOMEIP_ETS_088
     - TC8-SDLC-001
     - ``sd_sub_lifecycle``
   * - Section 6.1.6, SOMEIP_ETS_092
     - TC8-SDLC-002
     - ``sd_sub_lifecycle``
   * - Section 6.1.6, SOMEIP_ETS_107
     - TC8-SDLC-004
     - ``sd_sub_lifecycle``
   * - Section 6.1.6, SOMEIP_ETS_120
     - TC8-SDLC-005
     - ``sd_sub_lifecycle``
   * - Section 6.1.6, SOMEIP_ETS_155
     - TC8-SDLC-007
     - ``sd_sub_lifecycle``
   * - Section 6.1.6, SOMEIP_ETS_091
     - TC8-SDLC-008
     - ``sd_offer_format``
   * - Section 6.1.6, SOMEIP_ETS_121
     - TC8-SDLC-009
     - ``sd_sub_lifecycle``
   * - Section 6.1.6, SOMEIP_ETS_101
     - TC8-SDLC-011
     - ``sd_sub_lifecycle``
   * - Section 6.1.6, SOMEIP_ETS_128
     - TC8-SDLC-012
     - ``sd_find_response``
   * - Section 6.1.6, SOMEIP_ETS_130
     - TC8-SDLC-013
     - ``sd_find_response``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_07; Section 6.1.5.1, SOMEIPSRV_FORMAT_02
     - TC8-SDLC-015
     - ``sd_reboot``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_07; Section 6.1.5.1, SOMEIPSRV_FORMAT_02
     - TC8-SDLC-016
     - ``sd_reboot``
   * - Section 6.1.6, SOMEIP_ETS_095
     - TC8-SDLC-019
     - ``sd_ttl_expiry``

.. note::

   TC8-SDLC-011 (SOMEIP_ETS_101) is implemented as ``pytest.skip`` because the
   DUT has no ETS client mode (``clientServiceActivate``).

SD Message Format
^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 45 20 35

   * - OA Spec Reference (Ch. 6)
     - Internal ID
     - Requirement
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_01
     - TC8-SDF-001
     - ``sd_offer_format``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_03
     - TC8-SDF-002
     - ``sd_offer_format``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_05
     - TC8-SDF-003
     - ``sd_offer_format``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_06
     - TC8-SDF-004
     - ``sd_offer_format``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_08
     - TC8-SDF-005
     - ``sd_offer_format``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_09
     - TC8-SDF-006
     - ``sd_offer_format``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_10
     - TC8-SDF-007
     - ``sd_offer_format``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_11
     - TC8-SDF-008
     - ``sd_offer_format``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_12
     - TC8-SDF-009
     - ``sd_offer_format``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_13
     - TC8-SDF-010
     - ``sd_offer_format``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_07
     - TC8-SDF-011
     - ``sd_offer_format``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_19
     - TC8-SDF-012
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_20
     - TC8-SDF-013
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_21
     - TC8-SDF-014
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_23
     - TC8-SDF-015
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_24
     - TC8-SDF-016
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_25
     - TC8-SDF-017
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_26
     - TC8-SDF-018
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_27
     - TC8-SDF-019
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_28
     - TC8-SDF-020
     - ``sd_sub_lifecycle``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_04
     - TC8-SDF-021
     - ``sd_offer_format``

SD Find Response
^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 45 20 35

   * - OA Spec Reference (Ch. 6)
     - Internal ID
     - Requirement
   * - Section 6.1.6, SOMEIP_ETS_060
     - TC8-SDR-001
     - ``sd_find_response``
   * - Section 6.1.6, SOMEIP_ETS_118
     - TC8-SDR-002
     - ``sd_find_response``
   * - Section 6.1.6, SOMEIP_ETS_127
     - TC8-SDR-003
     - ``sd_find_response``

SD TCP
^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 45 20 35

   * - OA Spec Reference (Ch. 6)
     - Internal ID
     - Requirement
   * - Section 6.1.5.2, SOMEIPSRV_OPTIONS_15
     - TC8-SDT-001
     - ``sd_endpoint_option``
   * - Section 6.1.6, SOMEIP_ETS_096
     - TC8-SDT-002
     - ``sd_sub_lifecycle``

SD Initial Field and Reboot Detection
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 45 20 35

   * - OA Spec Reference (Ch. 6)
     - Internal ID
     - Requirement
   * - Section 6.1.6, SOMEIP_ETS_086
     - TC8-SDI-001
     - ``sd_sub_lifecycle``
   * - Section 6.1.6, SOMEIP_ETS_087
     - TC8-SDI-002
     - ``sd_sub_lifecycle``
   * - Section 6.1.6, SOMEIP_ETS_093
     - TC8-SDI-003
     - ``sd_reboot``
   * - Section 6.1.6, SOMEIP_ETS_094
     - TC8-SDI-004
     - ``sd_reboot``

Test Results
-------------

The table below is generated from the project's native requirement
to test traceability: it lists every testcase need that links, via its
``fully_verifies`` or ``partially_verifies`` field, to a
``comp_req__tc8_conformance__`` requirement, together with its last
recorded result. It is empty on a local build until a test run's
``test.xml`` is made available to the docs build.

.. needtable::
   :types: testcase
   :filter: any(i.startswith("comp_req__tc8_conformance__") for i in ((fully_verifies or []) + (partially_verifies or [])))
   :columns: id;result;fully_verifies;partially_verifies
   :style: table

Tests that skip or error during setup do not get a results link today, because
of an upstream ``score_tooling`` 2.1.1 limitation in its pytest results
plugin, so a requirement verified only by a skip marked test shows no linked
test results.
