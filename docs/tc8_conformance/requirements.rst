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

TC8 Conformance Test Requirements
=================================

Overview
--------

This document defines the project requirements for verifying ``someipd``
against the OPEN Alliance TC8 SOME/IP test specification. It works together
with the test specification and traceability documents:

.. list-table:: TC8 Conformance Documentation Set
   :widths: 25 75
   :header-rows: 1

   * - Document
     - Purpose
   * - **requirements.rst** (this document)
     - Defines what is to be verified through one feature requirement and
       component requirements allocated to ``someipd``.
   * - :doc:`test_specification`
     - Describes how each test is run, including purpose, preconditions,
       stimuli, and expected results.
   * - :doc:`traceability`
     - Maps OA TC8 test-case identifiers to internal test IDs and component
       requirements, and presents generated test results.

How the feature / component split works for TC8
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

* **One feature requirement** states the overall goal: verify ``someipd``
  against OA TC8 SOME/IP at the wire level. It describes the conformance
  objective rather than enumerating individual test scenarios.

* **Component requirements** describe specific protocol behaviours to verify,
  such as Service Discovery timing, message formatting, and event
  notifications. They cite relevant protocol sources or TC8 cases where
  applicable. Test decorators link these requirements to tests where coverage
  exists; the summary below identifies known gaps.

Feature Requirement
-------------------

The following feature requirement establishes TC8 SOME/IP conformance testing
as a formal verification activity for the SOME/IP Gateway's protocol stack.

.. feat_req:: TC8 SOME/IP Protocol Conformance
   :id: feat_req__tc8_conformance__conformance
   :status: valid
   :version: 1
   :tags: tc8, conformance, someip, verification
   :derived_from: stkh_req__someip_gw__interoperability, stkh_req__extspec__someip_83,
                  stkh_req__extspec__someip_90, stkh_req__extspec__someip_95
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway project shall verify protocol conformance of its
   SOME/IP stack (``someipd``) against OPEN Alliance TC8 SOME/IP test
   specifications at the wire protocol level, without requiring application
   processes.

Component Requirements — Service Discovery
------------------------------------------

The following component requirements define the high-priority TC8 conformance
tests for SOME/IP Service Discovery (SD), aligned with SOME/IP-SD Protocol
Specification (AUTOSAR PRS_SOMEIP_SD).

.. comp_req:: TC8 SD Offer Entry Format Validation
   :id: comp_req__tc8_conformance__sd_offer_format
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` transmits
   SOME/IP-SD OfferService entries on the configured multicast group
   with correct service ID, instance ID, major/minor version, and TTL
   fields upon startup.

   Note: Traces to SOME/IP-SD specification sections 4.1.2.1
   (OfferService entry format) and 4.1.2.3 (Service Entry fields).
   Covers TC8-SD-001 and TC8-SD-002 from the test strategy.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someipsd_47`, :need:`stkh_req__extspec__someipsd_253`.

.. comp_req:: TC8 SD Cyclic Offer Timing
   :id: comp_req__tc8_conformance__sd_cyclic_timing
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, timing
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` repeats
   OfferService entries at the configured ``cyclic_offer_delay`` interval
   during the main phase of Service Discovery, within the OA TC8
   ``ParamToleranceTime`` (1 s in the linked test).

   Note: Traces to SOME/IP-SD specification section 4.1.1
   (SD Phases — Main Phase, cyclic offer behavior).
   Covers TC8-SD-003 from the test strategy.

   Supporting Open SOMEIP requirements: :need:`stkh_req__extspec__someipsd_79`, :need:`stkh_req__extspec__someipsd_81`.

.. comp_req:: TC8 SD Multicast Response to FindService
   :id: comp_req__tc8_conformance__sd_find_mcast_rsp
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, timing
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` answers a
   FindService with a multicast OfferService for the specified timing and
   Unicast Flag conditions.

   Note: Traces to OA TC8 SOMEIPSRV_SD_BEHAVIOR_03 and
   SOMEIPSRV_SD_BEHAVIOR_04; see TC8-SDM-013 and TC8-SDM-014.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someipsd_83`, :need:`stkh_req__extspec__someipsd_90`, :need:`stkh_req__extspec__someipsd_91`, :need:`stkh_req__extspec__someipsd_824`.

.. comp_req:: TC8 SD FindService Response
   :id: comp_req__tc8_conformance__sd_find_response
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` responds to
   a SOME/IP-SD FindService entry for a known service with a unicast
   OfferService. The unknown-service no-response case is not currently
   covered by a linked test.

   Note: Traces to SOME/IP-SD specification section 4.1.2.2
   (FindService entry handling and response behavior).
   Covers known-service response behavior. TC8-SD-005 (unknown service)
   remains an unverified case.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someipsd_83`, :need:`stkh_req__extspec__someipsd_90`, :need:`stkh_req__extspec__someipsd_91`, :need:`stkh_req__extspec__someipsd_824`.

.. comp_req:: TC8 SD Subscribe Eventgroup Lifecycle
   :id: comp_req__tc8_conformance__sd_sub_lifecycle
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, eventgroup
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` correctly
   handles the SubscribeEventgroup lifecycle: acknowledge valid
   subscriptions (SubscribeEventgroupAck), reject unknown eventgroups
   (SubscribeEventgroupNack with TTL=0), and honor StopSubscribeEventgroup
   by ceasing notifications.

   Note: Traces to SOME/IP-SD specification sections 4.1.2.4
   (SubscribeEventgroup), 4.1.2.5 (StopSubscribeEventgroup),
   and 4.1.2.6 (SubscribeEventgroupAck/Nack).
   Covers TC8-SD-006, TC8-SD-007, and TC8-SD-008 from the test strategy.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someipsd_321`, :need:`stkh_req__extspec__someipsd_322`, :need:`stkh_req__extspec__someipsd_332`, :need:`stkh_req__extspec__someipsd_333`, :need:`stkh_req__extspec__someipsd_613`, :need:`stkh_req__extspec__someipsd_614`, :need:`stkh_req__extspec__someipsd_763`, :need:`stkh_req__extspec__someipsd_618`, :need:`stkh_req__extspec__someipsd_619`.

.. comp_req:: TC8 SD Subscription TTL Expiry
   :id: comp_req__tc8_conformance__sd_ttl_expiry
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, eventgroup, timing
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that event notifications cease
   after a subscription expires without renewal, conforming to
   OA TC8 SOMEIP_ETS_095.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someipsd_321`, :need:`stkh_req__extspec__someipsd_322`, :need:`stkh_req__extspec__someipsd_332`, :need:`stkh_req__extspec__someipsd_333`, :need:`stkh_req__extspec__someipsd_613`, :need:`stkh_req__extspec__someipsd_614`, :need:`stkh_req__extspec__someipsd_763`, :need:`stkh_req__extspec__someipsd_618`, :need:`stkh_req__extspec__someipsd_619`.

.. comp_req:: TC8 SD Initial Delay and Repetitions Phase
   :id: comp_req__tc8_conformance__sd_phases_timing
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, timing
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify the Repetition Phase OfferService
   timing: the first inter-offer gap is less than half the configured
   ``cyclic_offer_delay``, and the second gap is twice the configured
   ``repetitions_base_delay`` within the OA TC8 ``ParamToleranceTimeMillisec``
   (100 ms in the linked test). Initial-wait bounds, repetition count, and the
   transition to main phase are not currently covered by a linked test.

   Note: Traces to SOME/IP-SD specification section 4.1.1
   (SD Phases — Initial Wait, Repetition, Main Phase).
   Partially covers TC8-SD-009; TC8-SD-010 remains unverified.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someipsd_62`, :need:`stkh_req__extspec__someipsd_63`, :need:`stkh_req__extspec__someipsd_64`, :need:`stkh_req__extspec__someipsd_67`, :need:`stkh_req__extspec__someipsd_73`, :need:`stkh_req__extspec__someipsd_75`, :need:`stkh_req__extspec__someipsd_76`.

.. comp_req:: TC8 SD IPv4 Endpoint Option Validation
   :id: comp_req__tc8_conformance__sd_endpoint_option
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` includes a valid
   IPv4EndpointOption in OfferService SD entries, carrying the correct
   unicast address, port, and L4 protocol so clients can reach the offered
   service.

   Note: Traces to AUTOSAR SOME/IP-SD Protocol Specification R24-11,
   section 5.1.2.4.3 (IPv4 Endpoint Option); see TC8-SD-011.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someipsd_127`, :need:`stkh_req__extspec__someipsd_128`, :need:`stkh_req__extspec__someipsd_129`, :need:`stkh_req__extspec__someipsd_681`, :need:`stkh_req__extspec__someipsd_849`, :need:`stkh_req__extspec__someipsd_850`.

.. comp_req:: TC8 SD Reboot Detection
   :id: comp_req__tc8_conformance__sd_reboot
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, reboot
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` sets the reboot
   flag and resets its SD session ID after restart, and detects a peer reboot
   through the reboot flag and a lower session ID.

   Note: Traces to SOME/IP-SD Header reboot flag and Session ID handling;
   see TC8-SDLC-015 and TC8-SDLC-016.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someipsd_41`, :need:`stkh_req__extspec__someipsd_765`, :need:`stkh_req__extspec__someipsd_863`, :need:`stkh_req__extspec__someipsd_871`.

.. comp_req:: TC8 SD Multicast Eventgroup Option
   :id: comp_req__tc8_conformance__sd_mcast_eg
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, multicast
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` includes an
   IPv4 Multicast Option in the SubscribeEventgroupAck for eventgroups
   configured with a multicast address.

   Note: Traces to AUTOSAR SOME/IP-SD Protocol Specification R24-11,
   section 5.1.3.1.3 (Subscribe Eventgroup Acknowledgement); see TC8-SD-013.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someipsd_723`, :need:`stkh_req__extspec__someipsd_724`, :need:`stkh_req__extspec__someipsd_763`, :need:`stkh_req__extspec__someipsd_854`.

Component Requirements — SOME/IP Message Format
-----------------------------------------------

.. comp_req:: TC8 SOME/IP Response Header Validation
   :id: comp_req__tc8_conformance__msg_resp_header
   :status: valid
   :version: 1
   :tags: tc8, conformance, message_format
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` returns
   SOME/IP RESPONSE messages with correct protocol version (0x01),
   message type (0x80), matching session ID, and matching client ID
   for each received REQUEST.

   Note: Traces to SOME/IP specification sections 4.1.4
   (Protocol Version), 4.1.6 (Message Type), and 4.1.3 (Request ID —
   Client ID / Session ID). Covers TC8-SOMEIP-MSG-001,
   TC8-SOMEIP-MSG-002, TC8-SOMEIP-MSG-005, and TC8-SOMEIP-MSG-008
   from the test strategy.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someip_79`, :need:`stkh_req__extspec__someip_83`, :need:`stkh_req__extspec__someip_90`, :need:`stkh_req__extspec__someip_95`, :need:`stkh_req__extspec__someip_141`.

.. comp_req:: TC8 SOME/IP Error Return Codes
   :id: comp_req__tc8_conformance__msg_error_codes
   :status: valid
   :version: 1
   :tags: tc8, conformance, message_format, error_handling
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` returns
   the correct SOME/IP return codes for error conditions:
   ``E_UNKNOWN_SERVICE`` (0x02) for requests to non-existent services,
   ``E_UNKNOWN_METHOD`` (0x03) for invalid method IDs, and
   ``E_WRONG_INTERFACE_VERSION`` for interface version mismatches.

   Note: Traces to SOME/IP specification section 4.1.7 (Return Code)
   and the return code table (Table 4.14). Covers TC8-SOMEIP-MSG-003,
   TC8-SOMEIP-MSG-004, and TC8-SOMEIP-MSG-006 from the test strategy.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someip_144`, :need:`stkh_req__extspec__someip_371`, :need:`stkh_req__extspec__someip_816`.

Component Requirements — Event Notification
-------------------------------------------

.. comp_req:: TC8 Event Notification Subscription Lifecycle
   :id: comp_req__tc8_conformance__evt_subscription
   :status: valid
   :version: 1
   :tags: tc8, conformance, events, notification
   :satisfied_by: comp__someipd
   :derived_from: feat_req__tc8_conformance__conformance
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` delivers
   NOTIFICATION messages (message type 0x02) with correct event ID
   only to endpoints with an active eventgroup subscription, and ceases
   delivery after StopSubscribeEventgroup.

   Note: Traces to SOME/IP specification section 5.1 (Events) and
   SOME/IP-SD section 4.1.2.4 (SubscribeEventgroup triggering
   notification delivery). Covers TC8-EVT-001 through TC8-EVT-004
   and TC8-EVT-006 from the test strategy.

   Supporting Open SOME/IP requirements: :need:`stkh_req__extspec__someip_361`, :need:`stkh_req__extspec__someip_807`, :need:`stkh_req__extspec__someipsd_1297`.

Traceability Summary
--------------------

The table records whether testcase decorators currently link to each
component requirement. Supporting Open SOME/IP requirement entries are linked
from the applicable component requirements above; these links do not replace
the OA TC8 case references. Test execution results are shown in
``traceability.rst`` when a test run is available.

* ``comp_req__tc8_conformance__sd_offer_format`` — linked.
* ``comp_req__tc8_conformance__sd_cyclic_timing`` — linked.
* ``comp_req__tc8_conformance__sd_find_mcast_rsp`` — partial; one linked case is expected-failing.
* ``comp_req__tc8_conformance__sd_find_response`` — linked; unknown-service case not tested.
* ``comp_req__tc8_conformance__sd_sub_lifecycle`` — linked.
* ``comp_req__tc8_conformance__sd_ttl_expiry`` — partially linked.
* ``comp_req__tc8_conformance__sd_phases_timing`` — partial; initial-wait and main-phase cases missing.
* ``comp_req__tc8_conformance__sd_endpoint_option`` — linked.
* ``comp_req__tc8_conformance__sd_mcast_eg`` — linked; skipped on loopback environments.
* ``comp_req__tc8_conformance__sd_reboot`` — partially linked.
* ``comp_req__tc8_conformance__msg_resp_header`` — no testcase decorator link.
* ``comp_req__tc8_conformance__msg_error_codes`` — no testcase decorator link.
* ``comp_req__tc8_conformance__evt_subscription`` — no testcase decorator link.
