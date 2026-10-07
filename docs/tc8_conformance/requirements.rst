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

This document defines the requirements for verifying ``someipd`` against
the OPEN Alliance TC8 SOME/IP test specification.

It belongs to a set of three documents that work together:

.. list-table:: TC8 Conformance Documentation Set
   :widths: 25 75
   :header-rows: 1

   * - Document
     - Purpose
   * - **requirements.rst** (this file)
     - Defines *what* must be verified: one feature requirement and
       multiple component requirements, each linked to the S-CORE
       requirement hierarchy.
   * - :doc:`test_specification`
     - Defines *how* each test runs: purpose, preconditions, stimuli,
       and expected results.
   * - :doc:`traceability`
     - Maps external OA spec test case IDs to internal test IDs and
       component requirements, and shows generated test results.

How the feature / component split works for TC8
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

For TC8 conformance, the split is simple:

* **One feature requirement**
  (``feat_req__tc8_conformance__conformance``) covers the overall goal:
  "verify ``someipd`` against OA TC8 SOME/IP at the wire level." This
  requirement does **not** change when new test areas are added.

* **Many component requirements**, one per testable protocol aspect
  (e.g., SD offer format, cyclic timing, response headers, TCP
  transport). Each component requirement:

  - Describes the specific behaviour under test.
  - References the relevant AUTOSAR PRS or TC8 specification section.
  - Is verified by one or more pytest functions.

Feature Requirement
-------------------

The following feature requirement establishes TC8 SOME/IP conformance testing
as a formal verification activity for the SOME/IP Gateway's protocol stack.

.. feat_req:: TC8 SOME/IP Protocol Conformance
   :id: feat_req__tc8_conformance__conformance
   :status: valid
   :version: 1
   :tags: tc8, conformance, someip, verification
   :derived_from: stkh_req__someip_gw__interoperability
   :satisfied_by: feat__someip_gateway
   :safety: QM
   :security: NO
   :reqtype: Functional
   :valid_from: v0.0.0

   The SOME/IP Gateway project shall verify protocol conformance of its
   SOME/IP stack (``someipd``) against OPEN Alliance TC8 SOME/IP test
   specifications at the wire protocol level, without requiring application
   processes.

   **Scope boundary:** This document currently defines only the top-level
   feature requirement and the initial set of TC8 component requirements
   established for the test infrastructure split. The remaining component
   requirements, covering method calls, field access, and the detailed
   traceability matrix against the OA TC8 v3.0 Chapter 6 test catalog, are
   added together with the test modules that implement and verify them.

Component Requirements: Service Discovery
------------------------------------------

The following component requirements define the high-priority TC8 conformance
tests for SOME/IP Service Discovery (SD), aligned with SOME/IP-SD Protocol
Specification (AUTOSAR PRS_SOMEIP_SD).

.. comp_req:: TC8 SD Offer Entry Format Validation
   :id: comp_req__tc8_conformance__sd_offer_format
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` transmits
   SOME/IP-SD OfferService entries on the configured multicast group
   with correct service ID, instance ID, major/minor version, and TTL
   fields upon startup.

   Note: Traces to AUTOSAR SOME/IP Service Discovery Protocol
   Specification R24-11, section 5.1.2.5.2 (Offer Service Entry).

.. comp_req:: TC8 SD Cyclic Offer Timing
   :id: comp_req__tc8_conformance__sd_cyclic_timing
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, timing
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` repeats
   OfferService entries at the configured ``cyclic_offer_delay`` interval
   (±20% tolerance) during the main phase of Service Discovery.

   Note: Traces to AUTOSAR SOME/IP Service Discovery Protocol
   Specification R24-11, section 5.1.4.1 (Startup Behavior, Main
   Phase cyclic offer behavior).

.. comp_req:: TC8 SD FindService Response
   :id: comp_req__tc8_conformance__sd_find_response
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` responds to
   a SOME/IP-SD FindService entry with a unicast OfferService for a
   known service, and does not respond for an unknown service.

   Note: Traces to AUTOSAR SOME/IP Service Discovery Protocol
   Specification R24-11, section 5.1.4.2 (Server Answer Behavior).

.. comp_req:: TC8 SD Subscribe Eventgroup Lifecycle
   :id: comp_req__tc8_conformance__sd_sub_lifecycle
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, eventgroup
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` correctly
   handles the SubscribeEventgroup lifecycle: acknowledge valid
   subscriptions (SubscribeEventgroupAck), reject unknown eventgroups
   (SubscribeEventgroupNack with TTL=0), honor StopSubscribeEventgroup
   by ceasing notifications, and clean up expired subscriptions after
   the subscription TTL elapses.

   Note: Traces to AUTOSAR SOME/IP Service Discovery Protocol
   Specification R24-11, sections 5.1.3.1.1 (Subscribe Eventgroup
   Entry, including TTL), 5.1.3.1.2 (Stop Subscribe Eventgroup
   Entry), 5.1.3.1.3 (Subscribe Eventgroup Acknowledgement), and
   5.1.3.1.4 (Subscribe Eventgroup Negative Acknowledgement).

.. comp_req:: TC8 SD Subscription TTL Expiry
   :id: comp_req__tc8_conformance__sd_ttl_expiry
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, eventgroup, timing
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that event notifications cease to arrive
   after the subscription TTL expires: when a tester subscribes with TTL = 1 and no
   renewal is sent, no further SOME/IP notifications shall be received beyond 2 seconds
   after the TTL expiry, conforming to OA TC8 SOMEIP_ETS_095.

.. comp_req:: TC8 SD Initial Delay and Repetitions Phase
   :id: comp_req__tc8_conformance__sd_phases_timing
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, timing
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` observes
   the three SD phases: initial wait delay within ``[initial_delay_min,
   initial_delay_max]``, repetition of offers ``repetitions_max`` times
   at ``repetitions_base_delay`` intervals, and transition to main phase.

   Note: Traces to AUTOSAR SOME/IP Service Discovery Protocol
   Specification R24-11, section 5.1.4.1 (Startup Behavior, Initial
   Wait Phase, Repetition Phase, Main Phase).

.. comp_req:: TC8 SD IPv4 Endpoint Option Validation
   :id: comp_req__tc8_conformance__sd_endpoint_option
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` includes
   a valid IPv4EndpointOption in OfferService SD entries, carrying the
   correct unicast address, port, and L4 protocol (UDP) so that clients
   can reach the offered service.

   Note: Traces to AUTOSAR SOME/IP Service Discovery Protocol
   Specification R24-11, section 5.1.2.4.3 (IPv4 Endpoint Option).

.. comp_req:: TC8 SD Reboot Detection
   :id: comp_req__tc8_conformance__sd_reboot
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, reboot
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` resets its
   SD state upon restart: the reboot flag (SD flags byte bit 7) shall
   be set in the first SD message after restart, and the SD session ID
   shall reset to a low value (≤ 2).

   Note: Traces to AUTOSAR SOME/IP Service Discovery Protocol
   Specification R24-11, section 5.1.2.2 (SOME/IP-SD Header, Reboot
   Flag and Session ID handling).

.. comp_req:: TC8 SD Multicast Eventgroup Option
   :id: comp_req__tc8_conformance__sd_mcast_eg
   :status: valid
   :version: 1
   :tags: tc8, conformance, service_discovery, multicast
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` includes
   a multicast IPv4EndpointOption in the SubscribeEventgroupAck for
   eventgroups configured with a multicast address, so that clients
   know which multicast group to join for event delivery.

   Note: Traces to AUTOSAR SOME/IP Service Discovery Protocol
   Specification R24-11, section 5.1.3.1.3 (Subscribe Eventgroup
   Acknowledgement Entry, multicast endpoint option).

Component Requirements: SOME/IP Message Format
-----------------------------------------------

.. comp_req:: TC8 SOME/IP Response Header Validation
   :id: comp_req__tc8_conformance__msg_resp_header
   :status: valid
   :version: 1
   :tags: tc8, conformance, message_format
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` returns
   SOME/IP RESPONSE messages with correct protocol version (0x01),
   message type (0x80), matching session ID, and matching client ID
   for each received REQUEST.

   Note: Traces to AUTOSAR SOME/IP Protocol Specification R24-11, sections
   4.1.2.5 (Protocol Version), 4.1.2.7 (Message Type), and 4.1.2.4
   (Request ID, Client ID / Session ID).

.. comp_req:: TC8 SOME/IP Error Return Codes
   :id: comp_req__tc8_conformance__msg_error_codes
   :status: valid
   :version: 1
   :tags: tc8, conformance, message_format, error_handling
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` returns
   the correct SOME/IP return codes for error conditions:
   ``E_UNKNOWN_SERVICE`` (0x02) for requests to non-existent services,
   ``E_UNKNOWN_METHOD`` (0x03) for invalid method IDs, and
   ``E_WRONG_INTERFACE_VERSION`` for interface version mismatches.

   Note: Traces to AUTOSAR SOME/IP Protocol Specification R24-11, section
   4.1.2.8 (Return Code) and section 4.2.6.1 (Return Codes,
   requirement PRS_SOMEIP_00191).

Component Requirements: Event Notification
-------------------------------------------

.. comp_req:: TC8 Event Notification Subscription Lifecycle
   :id: comp_req__tc8_conformance__evt_subscription
   :status: valid
   :version: 1
   :tags: tc8, conformance, events, notification
   :derived_from: feat_req__tc8_conformance__conformance
   :satisfied_by: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` delivers
   NOTIFICATION messages (message type 0x02) with correct event ID
   only to endpoints with an active eventgroup subscription, and ceases
   delivery after StopSubscribeEventgroup.

   Note: Traces to AUTOSAR SOME/IP Protocol Specification R24-11, section
   4.2.4 (Notification Events), and AUTOSAR SOME/IP Service Discovery
   Protocol Specification R24-11, section 5.1.3.1.1 (Subscribe
   Eventgroup Entry).
