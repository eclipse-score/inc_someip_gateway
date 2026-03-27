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
==================================

Feature Requirement
-------------------

The following feature requirement establishes TC8 SOME/IP conformance testing
as a formal verification activity for the SOME/IP Gateway's protocol stack.

.. feat_req:: TC8 SOME/IP Protocol Conformance
   :id: feat_req__tc8_conformance__conformance
   :status: valid
   :tags: tc8, conformance, someip, verification
   :satisfies: stkh_req__docgen_enabled__example
   :safety: QM
   :security: NO
   :reqtype: Functional

   The SOME/IP Gateway project shall verify protocol conformance of its
   SOME/IP stack (``someipd``) against OPEN Alliance TC8 SOME/IP test
   specifications at the wire protocol level, without requiring application
   processes.

   Note: The ``:satisfies:`` link targets a placeholder stakeholder requirement.
   This shall be updated to reference the upstream S-CORE stakeholder requirement
   for SOME/IP interoperability once it is formally defined.

Component Requirements — Service Discovery
-------------------------------------------

The following component requirements define the high-priority TC8 conformance
tests for SOME/IP Service Discovery (SD), aligned with SOME/IP-SD Protocol
Specification (AUTOSAR PRS_SOMEIP_SD).

.. comp_req:: TC8 SD Offer Entry Format Validation
   :id: comp_req__tc8_conformance__sd_offer_format
   :status: valid
   :tags: tc8, conformance, service_discovery
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
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

.. comp_req:: TC8 SD Cyclic Offer Timing
   :id: comp_req__tc8_conformance__sd_cyclic_timing
   :status: valid
   :tags: tc8, conformance, service_discovery, timing
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` repeats
   OfferService entries at the configured ``cyclic_offer_delay`` interval
   (±20% tolerance) during the main phase of Service Discovery.

   Note: Traces to SOME/IP-SD specification section 4.1.1
   (SD Phases — Main Phase, cyclic offer behavior).
   Covers TC8-SD-003 from the test strategy.

.. comp_req:: TC8 SD FindService Response
   :id: comp_req__tc8_conformance__sd_find_response
   :status: valid
   :tags: tc8, conformance, service_discovery
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` responds to
   a SOME/IP-SD FindService entry with a unicast OfferService for a
   known service, and does not respond for an unknown service.

   Note: Traces to SOME/IP-SD specification section 4.1.2.2
   (FindService entry handling and response behavior).
   Covers TC8-SD-004 and TC8-SD-005 from the test strategy.

.. comp_req:: TC8 SD Subscribe Eventgroup Lifecycle
   :id: comp_req__tc8_conformance__sd_sub_lifecycle
   :status: valid
   :tags: tc8, conformance, service_discovery, eventgroup
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
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

.. comp_req:: TC8 SD Initial Delay and Repetitions Phase
   :id: comp_req__tc8_conformance__sd_phases_timing
   :status: valid
   :tags: tc8, conformance, service_discovery, timing
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` observes
   the three SD phases: initial wait delay within ``[initial_delay_min,
   initial_delay_max]``, repetition of offers ``repetitions_max`` times
   at ``repetitions_base_delay`` intervals, and transition to main phase.

   Note: Traces to SOME/IP-SD specification section 4.1.1
   (SD Phases — Initial Wait, Repetition, Main Phase).
   Covers TC8-SD-009 and TC8-SD-010 from the test strategy.

Component Requirements — SOME/IP Message Format
------------------------------------------------

.. comp_req:: TC8 SOME/IP Response Header Validation
   :id: comp_req__tc8_conformance__msg_resp_header
   :status: valid
   :tags: tc8, conformance, message_format
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
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

.. comp_req:: TC8 SOME/IP Error Return Codes
   :id: comp_req__tc8_conformance__msg_error_codes
   :status: valid
   :tags: tc8, conformance, message_format, error_handling
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
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

Component Requirements — Event Notification
--------------------------------------------

.. comp_req:: TC8 Event Notification Subscription Lifecycle
   :id: comp_req__tc8_conformance__evt_subscription
   :status: valid
   :tags: tc8, conformance, events, notification
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
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

Traceability Summary
---------------------

The following table links each component requirement to the SOME/IP
specification section it verifies.

.. list-table:: TC8 Requirement Traceability Matrix
   :widths: 30 20 30 20
   :header-rows: 1

   * - Requirement ID
     - TC8 Test IDs
     - SOME/IP Spec Reference
     - Safety
   * - ``comp_req__tc8_conformance__sd_offer_format``
     - TC8-SD-001, -002
     - SOME/IP-SD §4.1.2.1, §4.1.2.3
     - QM
   * - ``comp_req__tc8_conformance__sd_cyclic_timing``
     - TC8-SD-003
     - SOME/IP-SD §4.1.1 (Main Phase)
     - QM
   * - ``comp_req__tc8_conformance__sd_find_response``
     - TC8-SD-004, -005
     - SOME/IP-SD §4.1.2.2
     - QM
   * - ``comp_req__tc8_conformance__sd_sub_lifecycle``
     - TC8-SD-006, -007, -008
     - SOME/IP-SD §4.1.2.4–4.1.2.6
     - QM
   * - ``comp_req__tc8_conformance__sd_phases_timing``
     - TC8-SD-009, -010
     - SOME/IP-SD §4.1.1 (Phases)
     - QM
   * - ``comp_req__tc8_conformance__msg_resp_header``
     - TC8-MSG-001, -002, -005, -008
     - SOME/IP §4.1.3, §4.1.4, §4.1.6
     - QM
   * - ``comp_req__tc8_conformance__msg_error_codes``
     - TC8-MSG-003, -004, -006
     - SOME/IP §4.1.7 (Table 4.14)
     - QM
   * - ``comp_req__tc8_conformance__evt_subscription``
     - TC8-EVT-001–004, -006
     - SOME/IP §5.1, SOME/IP-SD §4.1.2.4
     - QM
