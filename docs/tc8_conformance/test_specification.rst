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

TC8 Test Specifications
========================

This document provides the detailed test specification for each TC8
conformance test case.  Each entry describes the purpose, preconditions,
test stimuli, expected results, and requirement traceability.

For the full OA specification mapping see :doc:`traceability`.
For requirement definitions see :doc:`requirements`.

.. note::

   The "OA Spec Reference" field in each test case references the
   corresponding section from Chapter 6 of the OPEN Alliance
   TC8 Automotive Ethernet ECU Test Specification v3.0 (Final).
   See :doc:`traceability` for the full mapping.

.. note:: Execution model

   The host address is ``TC8_TESTER_IP``; the DUT address is ``TC8_DUT_IP``.
   Both default values are defined in ``tc8_itf_conftest.py``.  To run a
   test::

      bazel test --config=tc8-itf //tests/tc8_conformance:test_tc8_<name>

   For QNX x86_64 use ``--config=tc8-itf-qnx``.

.. note:: Terminology

   Throughout this specification, **"server"** refers to the SOME/IP Service Provider role
   (the DUT, which offers services and responds to requests), and **"client"** refers to the
   SOME/IP Service Consumer role (the external test harness, which discovers services and
   subscribes to events). This usage mirrors TC8 OA Section 6.1.5 ("SOME/IP Server Tests") and
   Section 6.1.6 ("ETS Client / Control") directly.

Service Discovery Tests
-----------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``

TC8-SD-001: Multicast Offer on Startup
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_08
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_001_multicast_offer_on_startup``
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that ``someipd`` sends at least one SD OfferService entry on the
configured multicast group (``224.244.224.245:30490``) after startup.

**Preconditions:**

- ``someipd`` started without ``gatewayd``
- Multicast route available (``224.0.0.0/4``)

**Stimuli:**
None, passive observation of DUT multicast traffic.

**Expected Result:**
At least one SOME/IP-SD message containing an OfferService entry is
received on the multicast group within 5 seconds of DUT startup.

TC8-SD-002: Offer Entry Format
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_14-18
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_002_offer_entry_format``
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that the OfferService entry carries the correct service ID,
instance ID, major/minor version, and TTL as configured.

**Preconditions:**

- Same as TC8-SD-001.

**Stimuli:**
None, passive observation.

**Expected Result:**
OfferService entry has ``service_id=0x1234``, ``instance_id=0x5678``,
``major_version=0x00``, ``minor_version=0x00000000``, and ``TTL > 0``.

TC8-SD-003: Cyclic Offer Timing
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_02
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_003_cyclic_offer_timing``
:Requirement: ``comp_req__tc8_conformance__sd_cyclic_timing``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that OfferService entries repeat at the configured
``cyclic_offer_delay`` (2000 ms ±20%) during the main phase.

**Preconditions:**

- DUT in SD main phase (wait for repetition phase to complete).

**Stimuli:**
None, passive observation with timestamps.

**Expected Result:**
Inter-offer gaps in main phase are within [1600 ms, 2400 ms].

TC8-SD-004: FindService Known Service
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.6, SOMEIP_ETS_171
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_004_find_known_service_unicast_offer``
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that ``someipd`` responds with a unicast OfferService when a
FindService is sent for a known service.

**Preconditions:**

- DUT offering service ``0x1234``.

**Stimuli:**
Send SD FindService entry for service ``0x1234`` / instance ``0x5678``.

**Expected Result:**
Unicast OfferService entry received for the requested service.

TC8-SD-005: FindService Unknown Service
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.4, implied by SD_BEHAVIOR_03/04
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_005_find_unknown_service_no_response``
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that ``someipd`` does not respond to a FindService for an
unknown service.

**Preconditions:**

- DUT running, not offering service ``0xBEEF``.

**Stimuli:**
Send SD FindService entry for service ``0xBEEF``.

**Expected Result:**
No OfferService entry received for service ``0xBEEF`` within 2 seconds.

TC8-SD-006: Subscribe Eventgroup Ack
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_13
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_006_subscribe_valid_eventgroup_ack``
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that ``someipd`` sends SubscribeEventgroupAck (TTL > 0) for a
valid eventgroup subscription.

**Preconditions:**

- DUT offering eventgroup ``0x4455``.

**Stimuli:**
Send SD SubscribeEventgroup for service ``0x1234``, eventgroup ``0x4455``.

**Expected Result:**
SubscribeEventgroupAck with TTL > 0 received.

TC8-SD-007: Subscribe Unknown Eventgroup Nack
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_14; Section 6.1.6, SOMEIP_ETS_140
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_007_subscribe_unknown_eventgroup_nack``
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that ``someipd`` sends SubscribeEventgroupNack (TTL = 0) for
an unknown eventgroup.

**Preconditions:**

- DUT running, eventgroup ``0xBEEF`` not configured.

**Stimuli:**
Send SD SubscribeEventgroup for eventgroup ``0xBEEF``.

**Expected Result:**
SubscribeEventgroupAck with TTL = 0 (Nack) received.

TC8-SD-008: StopSubscribe Ceases Notifications
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.6, SOMEIP_ETS_108, SOMEIP_ETS_092
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_008_stop_subscribe_ceases_notifications``
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that notifications cease after StopSubscribeEventgroup (TTL = 0).

**Preconditions:**

- Active subscription to eventgroup ``0x4455``.
- At least one notification received.

**Stimuli:**
Send SD SubscribeEventgroup with TTL = 0 (StopSubscribe).

**Expected Result:**
No further notifications received within 4 seconds.

TC8-SD-009: Repetition Phase Intervals
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_01
:Test Module: ``test_sd_phases_timing.py``
:Test Function: ``test_tc8_sd_009_repetition_phase_intervals``
:Requirement: ``comp_req__tc8_conformance__sd_phases_timing``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that the first inter-offer gap after startup is a Repetition
Phase gap (shorter than half the cyclic offer delay).

**Preconditions:**

- Multicast socket opened before DUT startup to capture first offer.

**Stimuli:**
None, passive observation from DUT start.

**Expected Result:**
First gap < 1000 ms (half of ``cyclic_offer_delay`` 2000 ms).

TC8-SD-010: Repetition Count
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_01
:Test Module: ``test_sd_phases_timing.py``
:Test Function: ``test_tc8_sd_010_repetition_count_before_main_phase``
:Requirement: ``comp_req__tc8_conformance__sd_phases_timing``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify at least ``repetitions_max - 1`` short-gap offers before
transition to main phase.

**Preconditions:**

- Same as TC8-SD-009.

**Stimuli:**
None, passive observation from DUT start.

**Expected Result:**
At least 2 short gaps (< 1000 ms) observed before a long gap (main phase).

TC8-SD-011: IPv4 Endpoint Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.2, SOMEIPSRV_OPTIONS_01-07
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_011_offer_ipv4_endpoint_option``
:Requirement: ``comp_req__tc8_conformance__sd_endpoint_option``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that OfferService SD entries include an IPv4EndpointOption
with the correct address, port, and L4 protocol.

**Preconditions:**

- DUT offering service on UDP port 30509.

**Stimuli:**
None, passive observation with option parsing.

**Expected Result:**
IPv4EndpointOption present with address matching ``host_ip``,
port = 30509, protocol = UDP.

TC8-SD-013: Multicast Eventgroup Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.2, SOMEIPSRV_OPTIONS_08-14
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_013_subscribe_ack_has_multicast_option``
:Requirement: ``comp_req__tc8_conformance__sd_mcast_eg``
:DUT Config: ``tc8_someipd_sd.json``
:Marker: ``@pytest.mark.network`` (requires non-loopback interface)

**Purpose:**
Verify that SubscribeEventgroupAck for a multicast eventgroup includes
a multicast IPv4EndpointOption.

**Preconditions:**

- Non-loopback network interface (``TC8_TESTER_IP`` set).
- Eventgroup ``0x4465`` configured with multicast address ``239.0.0.1``.

**Stimuli:**
Send SD SubscribeEventgroup for eventgroup ``0x4465``.

**Expected Result:**
SubscribeEventgroupAck contains a multicast IPv4EndpointOption.

TC8-SD-014: TTL Expiry Cleanup
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.6, SOMEIP_ETS_095
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_014_ttl_expiry_ceases_notifications``
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that notifications cease after the subscription TTL expires.

**Preconditions:**

- Active subscription with TTL = 3 seconds.
- At least one notification received before expiry.

**Stimuli:**
Wait for TTL to expire (3 s + 2 s margin).

**Expected Result:**
No notifications received in a 3-second window after TTL expiry.

SD Entry Semantics Tests
-------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_service_discovery``

TC8-SDM-001: FindService Wildcard Instance
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_01
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_01_instance_wildcard``

**Purpose:**
Verify that a FindService with instance_id = 0xFFFF (wildcard) elicits an OfferService
for the configured instance.

**Stimulus:**
Send FindService with instance_id = 0xFFFF.

**Expected Result:**
OfferService received with ``instance_id == 0x5678``.

TC8-SDM-002: FindService Specific Instance
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_02
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_02_instance_specific``

**Purpose:**
Verify that a FindService with the exact instance_id = 0x5678 elicits an OfferService.

**Stimulus:**
Send FindService with instance_id = 0x5678.

**Expected Result:**
OfferService received for service_id = 0x1234, instance_id = 0x5678.

TC8-SDM-003: FindService Wildcard Major Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_03
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_03_major_version_wildcard``

**Purpose:**
Verify that a FindService with major_version = 0xFF (any) elicits an OfferService.

**Stimulus:**
Send FindService with major_version = 0xFF.

**Expected Result:**
OfferService received for the configured service.

TC8-SDM-004: FindService Specific Major Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_04
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_04_major_version_specific``

**Purpose:**
Verify that a FindService with the exact major_version = 0x00 elicits an OfferService.

**Stimulus:**
Send FindService with major_version = 0x00.

**Expected Result:**
OfferService received for the configured service.

TC8-SDM-005: FindService Wildcard Minor Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_05
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_05_minor_version_wildcard``

**Purpose:**
Verify that a FindService with minor_version = 0xFFFFFFFF (wildcard) elicits an
OfferService.

**Stimulus:**
Send FindService with minor_version = 0xFFFFFFFF.

**Expected Result:**
OfferService received for the configured service.

TC8-SDM-006: FindService Specific Minor Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_06
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_06_minor_version_specific``

**Purpose:**
Verify that a FindService with minor_version = 0x00000000 (exact) elicits an
OfferService.

**Stimulus:**
Send FindService with minor_version = 0x00000000.

**Expected Result:**
OfferService received for the configured service.

TC8-SDM-007: SubscribeEventgroup: Wrong Major Version Rejected
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_14
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_14_wrong_major_version``

**Purpose:**
Verify that a SubscribeEventgroup with a non-matching major version is rejected with
NAck (SubscribeEventgroupAck TTL = 0) or silently ignored.

**Stimulus:**
Send SubscribeEventgroup with major_version = 0x7F (not 0x00).

**Expected Result:**
No positive SubscribeAck (TTL > 0) received; DUT remains functional.

TC8-SDM-008: SubscribeEventgroup: Wrong Service ID Rejected
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_15
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_15_wrong_service_id``

**Purpose:**
Verify that a SubscribeEventgroup for an unknown service is rejected or ignored.

**Stimulus:**
Send SubscribeEventgroup with service_id = 0xBEEF.

**Expected Result:**
No positive SubscribeAck; DUT remains functional.

TC8-SDM-009: SubscribeEventgroup: Wrong Instance ID Rejected
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_16
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_16_wrong_instance_id``

**Purpose:**
Verify that a SubscribeEventgroup for an unknown instance is rejected or ignored.

**Stimulus:**
Send SubscribeEventgroup with instance_id = 0xBEEF.

**Expected Result:**
No positive SubscribeAck; DUT remains functional.

TC8-SDM-010: SubscribeEventgroup: Unknown Eventgroup Rejected
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_17
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_17_unknown_eventgroup_id``

**Purpose:**
Verify that a SubscribeEventgroup for an unknown eventgroup ID is rejected with NAck.

**Stimulus:**
Send SubscribeEventgroup with eventgroup_id = 0xBEEF.

**Expected Result:**
SubscribeAck with TTL = 0 (NAck) received.

TC8-SDM-011: SubscribeEventgroup: TTL = 0 is StopSubscribe
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_18
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_18_ttl_zero_stop_subscribe``

**Purpose:**
Verify that a SubscribeEventgroup with TTL = 0 is treated as StopSubscribeEventgroup
and does not elicit a positive ACK.

**Stimulus:**
Subscribe (TTL > 0) then immediately send another SubscribeEventgroup with TTL = 0.

**Expected Result:**
No positive ACK after the TTL = 0 message.

TC8-SDM-012: SubscribeEventgroup: Reserved Bits Set Rejected
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_19
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_19_reserved_field_set``

**Purpose:**
Verify that a SubscribeEventgroup with reserved flag bits set is rejected with NAck.

.. note::

   This test is expected to **FAIL** against vsomeip 3.6.1: the stack sends a
   positive ACK instead of NAck.

**Stimulus:**
Send SubscribeEventgroup with reserved SD flags bits set to 1.

**Expected Result:**
NAck (SubscribeAck TTL = 0) received; no positive ACK.

TC8-SDM-013: FindService Response Timing (Unicast)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_03
:Requirement: ``comp_req__tc8_conformance__sd_cyclic_timing``
:Test Function: ``TestSDFindServiceTiming::test_sd_behavior_03_unicast_findservice_timing``

**Purpose:**
Verify that a unicast OfferService response to a FindService arrives within the
configured request-response delay.

**Stimulus:**
Send unicast FindService; measure time to first OfferService response.

**Expected Result:**
OfferService received within the configured delay window.

TC8-SDM-014: FindService Response Timing (Multicast)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_04
:Requirement: ``comp_req__tc8_conformance__sd_cyclic_timing``
:Test Function: ``TestSDFindServiceTiming::test_sd_behavior_04_multicast_findservice_timing``

**Purpose:**
Verify that an OfferService response to a multicast FindService arrives within the
configured delay.

**Stimulus:**
Send multicast FindService; measure time to OfferService response.

**Expected Result:**
OfferService received within the allowed window.

SD Lifecycle Advanced Tests
----------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_service_discovery`` (TestSDSubscribeLifecycleAdvanced, TestSDFindServiceAdvanced) and ``test_sd_client``

TC8-SDLC-001: Two Simultaneous Subscribes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_088
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_088_two_subscribes_same_session``

**Purpose:**
Verify that two SubscribeEventgroup entries in one SD message each receive a
positive ACK.

**Stimulus:**
Send an SD message containing two SubscribeEventgroup entries.

**Expected Result:**
Two ACK entries received (one per subscription).

TC8-SDLC-002: TTL = 0 as StopSubscribe (No NAck)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_092
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_092_ttl_zero_stop_subscribe_no_nack``

**Purpose:**
Verify that a SubscribeEventgroup with TTL = 0 stops the subscription without
triggering a NAck from the DUT.

**Stimulus:**
Subscribe then send SubscribeEventgroup TTL = 0.

**Expected Result:**
No NAck (SubscribeAck TTL = 0) received after the StopSubscribe.

TC8-SDLC-003: Subscribe Without Prior RPC Call
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_098
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_098_subscribe_accepted_without_prior_rpc``

**Purpose:**
Verify that the DUT accepts a SubscribeEventgroup without any prior method call
(no prerequisite RPC interaction required).

**Stimulus:**
Send SubscribeEventgroup immediately after service discovery, without any RPC.

**Expected Result:**
Positive SubscribeAck received.

TC8-SDLC-004: Non-Standard SD Entry Order Handled
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_107
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_107_find_service_and_subscribe_processed_independently``

**Purpose:**
Verify that the DUT correctly handles SD messages where FindService and
SubscribeEventgroup entries appear in a non-standard order.

**Stimulus:**
Send SD message with FindService followed by SubscribeEventgroup.

**Expected Result:**
Both entries processed; OfferService and SubscribeAck received.

TC8-SDLC-005: Subscribe Endpoint IP Matches Tester
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_120
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_120_subscribe_endpoint_ip_matches_tester``

**Purpose:**
Verify that the DUT delivers events to the IP address specified in the subscribe
endpoint option, not the IP address the SD message was sourced from.

**Stimulus:**
Send SubscribeEventgroup with endpoint option IP = tester_ip.

**Expected Result:**
Events delivered to the tester_ip address.

TC8-SDLC-006: SD Interface Version = 0x01
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_122
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_122_sd_interface_version_is_one``

**Purpose:**
Verify that the SD interface_version field in DUT OfferService messages is 0x01.

**Stimulus:**
Passive capture of SD OfferService.

**Expected Result:**
``sd_hdr.interface_version == 0x01`` (same as SOMEIPSRV_FORMAT_04, verified in context
of the lifecycle sequence).

TC8-SDLC-007: Re-subscribe After StopSubscribe
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_155
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_155_resubscribe_after_stop``

**Purpose:**
Verify that a subscription can be re-established after a StopSubscribeEventgroup
(TTL = 0) and that events resume.

**Stimulus:**
Subscribe, verify events, StopSubscribe, re-Subscribe, verify events resume.

**Expected Result:**
Events received after re-subscription.

TC8-SDLC-008: Session ID Increments per OfferService
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_091
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_091_session_id_increments``

**Purpose:**
Verify that the SD SOME/IP header session_id increments by 1 between consecutive
OfferService messages.

**Stimulus:**
Capture two consecutive OfferService messages.

**Expected Result:**
``session_id[n+1] == session_id[n] + 1`` (with wrap from 0xFFFF to 0x0001).

TC8-SDLC-009: Initial Event Sent After Subscribe
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_099
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_099_initial_event_sent_after_subscribe``

**Purpose:**
Verify that the DUT sends an initial notification to a new subscriber immediately
after a successful SubscribeEventgroup ACK.

**Stimulus:**
Subscribe to eventgroup; wait for first notification.

**Expected Result:**
NOTIFICATION received promptly after ACK.

TC8-SDLC-010: Server Does Not Emit FindService
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_100
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_100_no_findservice_emitted_by_server``

**Purpose:**
Verify that the DUT (acting as service provider) does not emit FindService entries
in its SD messages during the main phase.

**Stimulus:**
Observe SD traffic for 6 seconds; collect all SD entries from the DUT.

**Expected Result:**
No FindService entries observed in DUT SD traffic.

TC8-SDLC-011: StopOfferService Ceases Client Events
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_101
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_101_stop_offer_ceases_client_events``

**Purpose:**
Verify that receipt of a StopOfferService (OfferService TTL = 0) from a server causes
the subscribed client to cease receiving events.

.. note::

   This test is currently implemented as ``pytest.skip`` because stopping the DUT's
   OfferService from an external tester requires a reverse-direction SD client target.

**Stimulus:**
N/A (skipped).

TC8-SDLC-012: Multicast FindService: Wildcard Versions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_128
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_128_multicast_findservice_version_wildcard``

**Purpose:**
Verify that a multicast FindService with wildcard major and minor versions elicits
an OfferService response.

**Stimulus:**
Send multicast FindService with major_version = 0xFF, minor_version = 0xFFFFFFFF.

**Expected Result:**
OfferService received for the configured service.

TC8-SDLC-013: Multicast FindService: Unicast Flag Clear
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_130
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_130_multicast_findservice_unicast_flag_clear``

**Purpose:**
Verify that a multicast FindService with the unicast flag cleared (0x00) elicits a
multicast OfferService response (not a unicast one).

**Stimulus:**
Send multicast FindService with unicast flag = 0.

**Expected Result:**
Multicast OfferService received.

TC8-SDLC-014: Client StopSubscribe Ceases Events
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_084
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Module: ``test_sd_client``
:Test Function: ``TestSDClientStopSubscribe::test_ets_084_stop_subscribe_ceases_events``

**Purpose:**
Verify that after a client sends StopSubscribeEventgroup (TTL = 0) the DUT stops
sending NOTIFICATION messages to that subscriber.

**Preconditions:**
Active subscription to eventgroup 0x4455; at least one notification received.

**Stimulus:**
Send SubscribeEventgroup with TTL = 0.

**Expected Result:**
No notifications received within 4 seconds after StopSubscribe.

TC8-SDLC-015: Client Reboot: Flag Set After First Restart
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_081
:Requirement: ``comp_req__tc8_conformance__sd_reboot``
:Test Module: ``test_sd_client``
:Test Function: ``TestSDClientReboot::test_ets_081_reboot_flag_set_after_first_restart``

**Purpose:**
Verify that after a DUT restart the first SD message has the reboot flag (bit 7
of SD flags byte) set and the session_id resets to a small value.

**Preconditions:**
DUT started, 3 SD messages drained, DUT terminated.

**Stimulus:**
Restart DUT; capture first post-reboot SD message.

**Expected Result:**
``sd_hdr.flag_reboot == True`` and ``outer.session_id <= 2``.

TC8-SDLC-016: Client Reboot: Flag Set After Second Restart
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_082
:Requirement: ``comp_req__tc8_conformance__sd_reboot``
:Test Module: ``test_sd_client``
:Test Function: ``TestSDClientReboot::test_ets_082_reboot_flag_set_after_second_restart``

**Purpose:**
Verify that the reboot flag and session ID reset hold across a second consecutive
restart (not just the first).

**Stimulus:**
Start then drain then stop then start then drain then stop then start then capture first message.

**Expected Result:**
``sd_hdr.flag_reboot == True`` and ``outer.session_id <= 2``.

TC8-SDLC-019: Subscribe TTL Expiry Stops Events
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_095
:Requirement: ``comp_req__tc8_conformance__sd_ttl_expiry``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_095_subscribe_ttl_expires_no_events``

**Purpose:**
Verify that when a subscription's TTL expires without renewal the DUT ceases sending
event notifications to that subscriber.

**Stimulus:**
Subscribe with a short TTL; do not renew; wait for TTL to expire; observe events.

**Expected Result:**
No NOTIFICATION messages received after TTL expiry window.
