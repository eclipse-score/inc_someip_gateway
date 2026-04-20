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
     - Maps external OA spec test case IDs to internal test IDs,
       component requirements, and Python test functions.

Why this hierarchy?
^^^^^^^^^^^^^^^^^^^

S-CORE projects use a three-level requirement hierarchy (defined by the
docs-as-code guidelines). Each requirement must be traceable upward to a
business goal and downward to a test:

* **Stakeholder requirements** (``stkh_req``) — *why* something is needed
  (business or interoperability goal).
* **Feature requirements** (``feat_req``) — *what* capability is needed.
  Each ``feat_req`` links to a ``stkh_req`` via ``:satisfies:``.
* **Component requirements** (``comp_req``) — the *specific, testable
  behaviour*. Each ``comp_req`` links to a ``feat_req`` via
  ``:satisfies:``, and each test links back via
  ``record_property("FullyVerifies", ...)``.

This creates **bidirectional traceability**: from a stakeholder need down
to the test that proves it is met, and from any test back up to the
business goal. Sphinx-Needs tooling uses these links to generate coverage
matrices and detect gaps.

How the feature / component split works for TC8
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

For TC8 conformance, the split is simple:

* **One feature requirement**
  (``feat_req__tc8_conformance__conformance``) covers the overall goal:
  "verify ``someipd`` against OA TC8 SOME/IP at the wire level." This
  requirement does **not** change when new test areas are added.

* **Many component requirements** — one per testable protocol aspect
  (e.g., SD offer format, cyclic timing, response headers, TCP
  transport). Each component requirement:

  - Describes the specific behaviour under test.
  - References the relevant AUTOSAR PRS or TC8 specification section.
  - Is verified by one or more pytest functions.

.. note:: **When to extend this document**

   When new TC8 conformance tests are added (e.g., new OA specification
   chapters or protocol areas like SOME/IP-TP), update the documents as
   follows:

   1. **The feature requirement stays unchanged** — it already covers
      the overall TC8 protocol conformance goal.
   2. **Add new component requirements** to this file for each testable
      behaviour. Group them under a new heading (e.g.,
      "Component Requirements — SOME/IP-TP").
   3. **Add test case descriptions** in :doc:`test_specification`.
   4. **Add OA-to-internal mapping rows** in :doc:`traceability`.
   5. Each new pytest function must call
      ``record_property("FullyVerifies", "<comp_req_id>")`` to close
      the traceability chain.

   A **new feature requirement** is only needed if the scope expands
   beyond wire-level protocol conformance — for example, a separate
   "TC8 Enhanced Testability" campaign would need its own ``feat_req``.

Requirement Hierarchy Diagram
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The diagram below shows how the three requirement levels, the test code,
the external OA standard, and the documentation files relate to each other.

.. uml::

   @startuml
   !theme plain

   scale max 800 width
   skinparam packageStyle rectangle
   skinparam linetype ortho
   skinparam nodesep 50
   skinparam ranksep 50

   package "S-CORE Requirement Hierarchy" {
     rectangle "**Stakeholder Requirement**\n(stkh_req)" as STKH #b1ddf0 {
       rectangle "stkh_req~__docgen_enabled~__example\n//High-level interoperability need//" as SReq
     }
     rectangle "**Feature Requirement**\n(feat_req)" as FEAT #fff2cc {
       rectangle "feat_req~__tc8_conformance~__conformance\n//TC8 SOME/IP protocol conformance//" as FReq
     }
     rectangle "**Component Requirements**\n(comp_req)" as COMP #d5e8d4 {
       rectangle "comp_req~__tc8_conformance~__*\n//One per testable protocol behaviour//" as CReq
     }
   }

   package "Test Implementation" {
     collections "**tests/tc8_conformance/*.py**\nPython test functions\n(pytest + raw SOME/IP)" as Tests #f5f5f5
   }

   package "External Standard" {
     rectangle "**OA TC8 Spec**\nChapter 5 — SOME/IP\nSOMEIPSRV_*, SOMEIP_ETS_*" as OASpec #e0e0e0
   }

   ' --- Relationships ---

   ' Requirement satisfaction hierarchy (vertical within hierarchy)
   FReq -up-> SReq : <<satisfies>>
   CReq -up-> FReq : <<satisfies>>

   ' Test verification and external standard traceability
   Tests -up-> CReq : <<verifies>>
   Tests -right-> OASpec : <<traces to>>

   @enduml

The relationships work as follows:

1. **Stakeholder → Feature** (``:satisfies:``):
   The feature requirement satisfies the stakeholder need for SOME/IP
   interoperability.

2. **Feature → Component** (``:satisfies:``):
   Each component requirement defines a specific, testable protocol
   behaviour and links up to the single feature requirement.

3. **Component → Test** (``record_property("FullyVerifies", ...)``):
   Each pytest function creates a machine-readable link back to the
   component requirement it verifies (emitted in JUnit XML).

4. **Test → External Standard** (traceability matrix):
   The :doc:`traceability` maps each internal test ID to the
   corresponding OA TC8 specification test case, closing the chain
   from external standard to verified implementation.  The
   :doc:`test_specification` provides detailed test case descriptions
   (purpose, stimuli, expected results) for each component requirement.

Requirement Areas
^^^^^^^^^^^^^^^^^

The component requirements are grouped by TC8 test area:

.. list-table::
   :widths: 30 20 50
   :header-rows: 1

   * - Area
     - Req Count
     - Scope
   * - Service Discovery
     - 9
     - SD offer format, cyclic timing, find response, subscribe lifecycle,
       subscription TTL expiry, phases timing, endpoint options, reboot
       detection, multicast eventgroup
   * - SD Format and Options Compliance
     - 3
     - Byte-level field assertions for SD SOME/IP header, offer entry,
       SubscribeAck entry, StopSubscribeEventgroup entry, IPv4EndpointOption,
       and IPv4MulticastOption
   * - SD Robustness
     - 1
     - Malformed SD packet handling without crash or state corruption
   * - SOME/IP Message Format
     - 3
     - Response header fields, error return codes, malformed message handling
   * - Event Notification
     - 1
     - Notification delivery lifecycle (subscribe, event ID, multicast, stop)
   * - Field Conformance
     - 2
     - Initial value on subscribe, getter/setter methods
   * - TCP Transport Binding
     - 1
     - TCP reliable transport for RPC, field get/set, event notification
   * - Multi-service and Multi-instance
     - 1
     - Multi-service config loading, per-service port advertisement, SD isolation

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
   (SubscribeEventgroupNack with TTL=0), honor StopSubscribeEventgroup
   by ceasing notifications, and clean up expired subscriptions after
   the subscription TTL elapses.

   Note: Traces to SOME/IP-SD specification sections 4.1.2.4
   (SubscribeEventgroup), 4.1.2.5 (StopSubscribeEventgroup),
   4.1.2.6 (SubscribeEventgroupAck/Nack), and 4.1.2.7 (TTL handling).
   Covers TC8-SD-006, TC8-SD-007, TC8-SD-008, and TC8-SD-014 from the
   test strategy.

.. comp_req:: TC8 SD Subscription TTL Expiry
   :id: comp_req__tc8_conformance__sd_ttl_expiry
   :status: valid
   :tags: tc8, conformance, service_discovery, eventgroup, timing
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
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

.. comp_req:: TC8 SD IPv4 Endpoint Option Validation
   :id: comp_req__tc8_conformance__sd_endpoint_option
   :status: valid
   :tags: tc8, conformance, service_discovery
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` includes
   a valid IPv4EndpointOption in OfferService SD entries, carrying the
   correct unicast address, port, and L4 protocol (UDP) so that clients
   can reach the offered service.

   Note: Traces to SOME/IP-SD specification section 4.1.2.4
   (SD Options — IPv4 Endpoint Option format).
   Covers TC8-SD-011 from the test strategy.

.. comp_req:: TC8 SD Reboot Detection
   :id: comp_req__tc8_conformance__sd_reboot
   :status: valid
   :tags: tc8, conformance, service_discovery, reboot
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` resets its
   SD state upon restart: the reboot flag (SD flags byte bit 7) shall
   be set in the first SD message after restart, and the SD session ID
   shall reset to a low value (≤ 2).

   Note: Traces to SOME/IP-SD specification section 4.1.1
   (Reboot Detection — session ID and reboot flag handling).
   Covers TC8-SD-012 from the test strategy.

.. comp_req:: TC8 SD Multicast Eventgroup Option
   :id: comp_req__tc8_conformance__sd_mcast_eg
   :status: valid
   :tags: tc8, conformance, service_discovery, multicast
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` includes
   a multicast IPv4EndpointOption in the SubscribeEventgroupAck for
   eventgroups configured with a multicast address, so that clients
   know which multicast group to join for event delivery.

   Note: Traces to SOME/IP-SD specification section 4.1.2.6
   (SubscribeEventgroupAck options — multicast endpoint).
   Covers TC8-SD-013 from the test strategy.

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
   Client ID / Session ID). Covers TC8-MSG-001,
   TC8-MSG-002, TC8-MSG-005, and TC8-MSG-008
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
   ``E_UNKNOWN_SERVICE`` (0x02) or no response for requests to
   non-existent services (both are valid stack behaviors),
   ``E_UNKNOWN_METHOD`` (0x03) for invalid method IDs, and
   ``E_WRONG_INTERFACE_VERSION`` for interface version mismatches.

   Note: Traces to SOME/IP specification section 4.1.7 (Return Code)
   and the return code table (Table 4.14). Covers TC8-MSG-003,
   TC8-MSG-004, and TC8-MSG-006 from the test strategy.

.. comp_req:: TC8 SOME/IP Malformed Message Handling
   :id: comp_req__tc8_conformance__msg_malformed
   :status: valid
   :tags: tc8, conformance, message_format, robustness
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` does not
   crash when receiving malformed SOME/IP messages, including truncated
   messages (shorter than the 8-byte minimum header), messages with an
   invalid protocol version, and messages whose length field claims more
   data than the UDP payload contains.

   Note: Traces to SOME/IP specification section 4.1 (Header format
   validation and error handling). Covers TC8-MSG-007 from the
   test strategy.

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
   notification delivery). Covers TC8-EVT-001 through TC8-EVT-006
   from the test strategy.

Component Requirements — Field Conformance
-------------------------------------------

.. comp_req:: TC8 Field Initial Value on Subscribe
   :id: comp_req__tc8_conformance__fld_initial_value
   :status: valid
   :tags: tc8, conformance, fields, notification
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` delivers an initial
   NOTIFICATION message to a new subscriber of a field eventgroup (``is_field: true``)
   immediately upon subscription, carrying the last known field value.

   Note: Traces to SOME/IP specification section 5.3 (Fields — initial value
   notification on subscribe) and AUTOSAR SWS_CM_00719.
   Covers TC8-FLD-001 and TC8-FLD-002 from the test strategy.

.. comp_req:: TC8 Field Getter and Setter
   :id: comp_req__tc8_conformance__fld_get_set
   :status: valid
   :tags: tc8, conformance, fields, request_response
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` handles field
   getter (method 0x0001) and setter (method 0x0002) requests: the getter
   shall return the current field value in a RESPONSE; the setter shall update
   the stored field value, respond with E_OK, and immediately notify all
   active subscribers with the new value.

   Note: Traces to SOME/IP specification section 5.3 (Fields — getter/setter
   methods) and AUTOSAR SWS_CM_00720/SWS_CM_00721.
   Covers TC8-FLD-003 and TC8-FLD-004 from the test strategy.

Component Requirements — TCP Transport Binding
-----------------------------------------------

.. comp_req:: TC8 TCP Transport Binding for RPC
   :id: comp_req__tc8_conformance__tcp_transport
   :status: valid
   :tags: tc8, conformance, tcp, transport, rpc
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` supports
   TCP (reliable) transport binding for SOME/IP RPC request/response
   communication, including correct TCP endpoint advertisement in
   Service Discovery and successful method invocation over a TCP
   connection.

   Note: Traces to OA TC8 specification references SOMEIPSRV_RPC_01,
   SOMEIPSRV_RPC_02, and SOMEIPSRV_OPTIONS_15. Also traces to
   PRS_SOMEIP_00142 (SOME/IP TCP message framing) and PRS_SOMEIP_00569
   (unaligned message handling over TCP), covered by TC8-TCP-009.
   Addresses Gap 1 (TCP transport binding) from the architecture
   conformance analysis.

.. seealso::

   For the full traceability chain (OA specification → internal TC8 ID →
   requirement → test function), see :doc:`traceability`.

   For detailed test case specifications (purpose, stimuli, expected results),
   see :doc:`test_specification`.

Component Requirements — Multi-service and Multi-instance
-----------------------------------------------------------

.. comp_req:: TC8 Multi-service and Multi-instance Routing
   :id: comp_req__tc8_conformance__multi_service
   :status: valid
   :tags: tc8, conformance, multi_service, routing
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` accepts a vsomeip
   configuration containing multiple service entries and, for each offered
   service, advertises the correct UDP port in the SD OfferService endpoint
   option.  Tests also verify that only the configured service IDs appear in
   SD traffic and that the multi-service config can be loaded without process
   failure.

   Note: Traces to OA TC8 specification references SOMEIPSRV_RPC_13
   (multi-service hosting) and SOMEIPSRV_RPC_14 (per-instance port isolation).
   Covered by ``test_multi_service.py`` in the ``tc8_multi_service`` Bazel target
   (TC8_SD_PORT=30499, TC8_SVC_PORT=30512, TC8_SVC_TCP_PORT=30513).

Component Requirements — SD Format and Options Compliance
-----------------------------------------------------------

The following component requirements cover byte-level field assertions for
SOME/IP-SD messages sent by ``someipd``, corresponding to OA TC8 v3.0 §5.1.5.1
(FORMAT_*) and §5.1.5.2 (OPTIONS_*).

.. comp_req:: TC8 SD SOME/IP Header and Entry Field Validation
   :id: comp_req__tc8_conformance__sd_format_fields
   :status: valid
   :tags: tc8, conformance, service_discovery, format
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` transmits SD
   OfferService and SubscribeEventgroupAck messages with correct byte-level
   field values, including: Client-ID = 0x0000 (FORMAT_01), Session-ID
   starting from 0x0001 (FORMAT_02), Interface Version = 0x01 (FORMAT_04),
   Message Type = 0x02 Notification (FORMAT_05), Return Code = 0x00 E_OK
   (FORMAT_06), undefined SD flag bits = 0 (FORMAT_09), reserved entry bytes
   = 0 (FORMAT_10), entry length = 16 bytes (FORMAT_11), correct option run
   indices and option counts (FORMAT_12/13), instance ID (FORMAT_15), major
   version (FORMAT_16), minor version (FORMAT_18) matching the configured
   values, SubscribeAck entry type = 0x06 (FORMAT_19), SubscribeAck entry
   length = 16 bytes (FORMAT_20), SubscribeAck option run index (FORMAT_21),
   SubscribeAck service ID (FORMAT_23), instance ID (FORMAT_24), major version
   (FORMAT_25), TTL > 0 (FORMAT_26), reserved field = 0 (FORMAT_27), and
   eventgroup ID (FORMAT_28) matching the subscribe request.

   Note: Traces to OA TC8 v3.0 §5.1.5.1 (SOME/IP-SD header and entry
   format assertions).

.. comp_req:: TC8 SD IPv4 Endpoint and Multicast Option Field Validation
   :id: comp_req__tc8_conformance__sd_options_fields
   :status: valid
   :tags: tc8, conformance, service_discovery, options
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` encodes SD
   IPv4EndpointOptions with correct sub-field values: option length = 0x0009
   (OPTIONS_01), option type = 0x04 (OPTIONS_02), reserved byte after type
   = 0x00 (OPTIONS_03), reserved byte before protocol = 0x00 (OPTIONS_05),
   and L4 protocol = 0x11 UDP (OPTIONS_06).  For multicast eventgroups the
   suite shall also verify that SubscribeEventgroupAck messages contain
   IPv4MulticastOptions with correct encoding: length = 0x0009 (OPTIONS_08),
   type = 0x14 (OPTIONS_09), reserved byte = 0x00 (OPTIONS_10), multicast
   address matching configuration (OPTIONS_11), reserved byte before port
   = 0x00 (OPTIONS_12), L4 protocol = 0x11 UDP (OPTIONS_13), and port number
   matching configuration (OPTIONS_14).

   Note: Traces to OA TC8 v3.0 §5.1.5.2 (SD Options format assertions).
   Multicast option tests (OPTIONS_08–14) require a non-loopback interface
   (``@pytest.mark.network``).

.. comp_req:: TC8 SD StopSubscribeEventgroup Entry Wire Format
   :id: comp_req__tc8_conformance__sd_stop_sub_fmt
   :status: valid
   :tags: tc8, conformance, service_discovery, format
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that a StopSubscribeEventgroup SD entry
   has entry type byte ``0x06`` and TTL field (bytes 9–11 of the entry) equal to
   ``0x000000`` at the wire level, conforming to OA TC8 SOMEIPSRV_SD_MESSAGE_12.

Component Requirements — SD Robustness
----------------------------------------

.. comp_req:: TC8 SD Robustness — Malformed Packet Survival
   :id: comp_req__tc8_conformance__sd_robustness
   :status: valid
   :tags: tc8, conformance, service_discovery, robustness
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` survives and
   remains functional (still responds to a valid FindService) after receiving
   malformed SOME/IP-SD packets, covering: empty entries arrays (ETS_111),
   zero-length or malformed options (ETS_112/113), mismatched entries-length
   fields (ETS_114), entries referencing more options than present (ETS_115),
   unknown option types (ETS_116/174), overlapping option indices (ETS_117),
   FindService with unexpected endpoint options (ETS_118), entries-length
   exceeding the payload (ETS_123/124/125), option lengths extending past the
   options array (ETS_134/135), option lengths shorter than the minimum
   (ETS_136), unaligned option lengths (ETS_137), options-array-length
   mismatches (ETS_138/139), SubscribeEventgroup without endpoint option
   (ETS_109), with zero IP endpoint (ETS_110), with wrong L4 protocol
   (ETS_119), for unknown service/instance/eventgroup (ETS_140/141/142/143),
   with reserved option type (ETS_144), SD with near-wrap or maximum session
   IDs (ETS_152), SOME/IP length field mismatches (ETS_153), and wrong
   SOME/IP service ID in the header (ETS_178).

   Note: Traces to OA TC8 v3.0 §5.1.6 (Enhanced Testability Service Tests —
   SD robustness cases).

Component Requirements — UDP Transport Binding
-----------------------------------------------

.. comp_req:: TC8 UDP Transport Binding — Multiple Messages per Datagram
   :id: comp_req__tc8_conformance__udp_transport
   :status: valid
   :tags: tc8, conformance, udp, transport
   :satisfies: feat_req__tc8_conformance__conformance
   :belongs_to: comp__someipd
   :safety: QM
   :security: NO
   :reqtype: Functional

   The conformance test suite shall verify that ``someipd`` correctly parses
   a UDP datagram that contains multiple SOME/IP messages packed consecutively,
   including the case where a message starts at a non-4-byte-aligned byte offset
   within the datagram.  The DUT shall respond to each contained SOME/IP request
   individually.

   Note: Traces to PRS_SOMEIP_00142 and PRS_SOMEIP_00569 (unaligned SOME/IP message
   parsing over UDP).
   Covered by TC8-UDP-001 in ``test_someip_message_format.py``
   (``test_tc8_ets_069_unaligned_someip_messages_over_udp``).
