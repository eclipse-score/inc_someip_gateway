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

      bazel test --config=qemu-integration //tests/tc8_conformance:test_tc8_<name>

   For QNX x86_64 use ``--config=x86_64-qnx``.

.. note:: Terminology

   Throughout this specification, **"server"** refers to the SOME/IP Service Provider role
   (the DUT, which offers services and responds to requests), and **"client"** refers to the
   SOME/IP Service Consumer role (the external test harness, which discovers services and
   subscribes to events). This usage mirrors TC8 OA Section 6.1.5 ("Test Cases SOME/IP
   Server") and Section 6.1.6 ("Test Cases ETS") directly.

.. note:: DUT scope

   The OA ETS cases assume the Enhanced Testability Service. The DUT here is
   ``someipd`` offering service ``0x1234`` with the instances ``0x5678`` and
   ``0x5680``, eventgroups ``0x0002``, ``0x0005`` and ``0x0006``, the cyclic field
   ``0x8001`` (500 ms), the multicast event ``0x800B`` and the fields ``0x8005`` to
   ``0x8007``, which have no update cycle and are sent only as initial fields. It has
   no trigger methods, so OA steps that call one are marked "Not verified".
   The OA procedures take the instance ID from the received OfferService. The tests
   use the configured ID, which is the same.

Service Discovery Tests
-----------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``

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

- ``someipd`` started without ``gatewayd``
- Multicast route available (``224.0.0.0/4``)

**Stimuli:**
None, passive observation of the first OfferService of the capture.

**Expected Result:**
OfferService entry has ``service_id=0x1234``, ``instance_id=0x5678``,
``major_version=0x01``, ``minor_version=0x00000000``, and ``TTL = 30`` (configured ``ttl``).

TC8-SD-003: Cyclic Offer Timing
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_02
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_003_cyclic_offer_timing``
:Requirement: ``comp_req__tc8_conformance__sd_cyclic_timing``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that in the main phase OfferService messages are sent cyclically.

**Stimuli:**
Receive an OfferService for the service. Wait TOTAL-REP-INTV (1.4 s) for the DUT
to enter the main phase. Listen for two OfferService messages, up to
CYCLE-INTV * 2 + ParamToleranceTime.

**Expected Result:**
The time between the first and the second OfferService is greater than
CYCLE-INTV - ParamToleranceTime and less than CYCLE-INTV + ParamToleranceTime.
CYCLE-INTV is ``cyclic_offer_delay`` (2 s), ParamToleranceTime is 1 s by default.

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
Send SD FindService entry for service ``0x1234`` / instance ``0x5678`` unicast to
the DUT, repeated every 1.5 seconds for up to 5 seconds.

**Expected Result:**
At least one unicast OfferService entry received for the requested service.

TC8-SD-006: Subscribe Eventgroup Ack
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_13
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_006_subscribe_valid_eventgroup_ack``
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that ``someipd`` answers a valid eventgroup subscription with a
SubscribeEventgroupAck that echoes the subscription.

**Preconditions:**

- DUT offering eventgroup ``0x0002``.

**Stimuli:**
Send SD SubscribeEventgroup for service ``0x1234``, instance ``0x5678``, eventgroup
``0x0002``, major version ``0x01``, TTL ``0xFFFFFF``, Reserved 0.

**Expected Result:**
SubscribeEventgroupAck with the same service ID, instance ID, eventgroup ID and
major version, TTL ``0xFFFFFF`` and Reserved 0.

TC8-SD-007: Subscribe Unknown Eventgroup Nack
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_19; Section 6.1.6, SOMEIP_ETS_140
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
Send SD SubscribeEventgroup for eventgroup ``0xBEEF``, TTL ``0xFFFFFF``.

**Expected Result:**
SubscribeEventgroupAck with TTL = 0 (Nack) received.

TC8-SD-008: StopSubscribe Ceases Notifications
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.6, SOMEIP_ETS_108
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_008_stop_subscribe_ceases_notifications``
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that no events are sent after StopSubscribeEventgroup (TTL = 0)
(ETS_108, eventgroup ``0x0005``). The OA trigger method TestEventUINT8 is not
used; the cyclic event of the DUT is observed instead.

**Preconditions:**

- Active subscription to eventgroup ``0x0005``.
- At least one notification received.

**Stimuli:**
Send SD SubscribeEventgroup with TTL = 0 (StopSubscribe).

**Expected Result:**
No further notifications received within 4 seconds.

Not verified: the initial event and the TestEventUINT8 trigger (the DUT has no
trigger method).

TC8-SD-009: Repetition Phase Intervals
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_01
:Test Module: ``test_sd_phases_timing.py``
:Test Function: ``test_tc8_sd_009_repetition_phase_intervals``
:Requirement: ``comp_req__tc8_conformance__sd_phases_timing``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that the first two inter-offer gaps after startup follow the Repetition
Phase: about the base delay (200 ms), then about twice that (400 ms).

**Preconditions:**

- Multicast socket opened before DUT startup to capture first offer.

**Stimuli:**
None, passive observation from DUT start.

**Expected Result:**
First gap < 1000 ms (half of ``cyclic_offer_delay`` 2000 ms). Second gap
400 ms (2 x ``repetitions_base_delay`` 200 ms) within ``ParamToleranceTimeMillisec``
(default 100 ms), i.e. between 300 ms and 500 ms.

TC8-SD-011: IPv4 Endpoint Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: Section 6.1.5.2, SOMEIPSRV_OPTIONS_01-07; Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_08
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
Exactly one IPv4EndpointOption with Length ``0x0009``, both Reserved fields
``0x00``, address ``TC8_DUT_IP``, port = 30509, protocol = UDP (``0x11``).

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
- Eventgroup ``0x0006`` configured with multicast address ``239.0.0.1``.

**Stimuli:**
Send SD SubscribeEventgroup for eventgroup ``0x0006``.

**Expected Result:**
SubscribeEventgroupAck contains a multicast IPv4EndpointOption with option type
``0x14``, Length ``0x0009``, both Reserved fields ``0x00``, address ``239.0.0.1``,
protocol = UDP (``0x11``) and port = 40490.

The OA Synopsis of SOMEIPSRV_OPTIONS_14 names the port of the multicast events,
its Pass criteria name ``<SERVICE-ID-1-UDP-PORT>``. This test asserts the
multicast event port.

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

Not verified: the reaction to an explicit TriggerEventUint8 call (the DUT has no
trigger method).

SD Entry Semantics Tests
-------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_service_discovery``

TC8-SDM-001: FindService: Any Instance (0xFFFF)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_01
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_01_any_instance``

**Purpose:**
Verify that a FindService with instance_id = 0xFFFF (Any Instance) elicits an OfferService
entry for every offered instance of the service.

**Stimulus:**
Send FindService with instance_id = 0xFFFF.

**Expected Result:**
OfferService with two entries for service_id = 0x1234 and different instance IDs
(0x5678 and 0x5680).

TC8-SDM-002: FindService: Specific Instance
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_02
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_02_instance_specific``

**Purpose:**
Verify that a FindService with an exact instance_id elicits exactly one OfferService entry
for that instance.

**Stimulus:**
Send FindService with instance_id = 0x5678, then with instance_id = 0x5680.

**Expected Result:**
Each time one OfferService entry for service_id = 0x1234 and the requested instance_id.

TC8-SDM-003: FindService: Any Major Version (0xFF)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_03
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_03_any_major_version``

**Purpose:**
Verify that a FindService with major_version = 0xFF (Any Major Version) elicits an
OfferService.

**Stimulus:**
Send FindService with major_version = 0xFF, TTL = 0xFFFFFF.

**Expected Result:**
OfferService received for the configured service with major_version = 0x01.

TC8-SDM-004: FindService: Specific Major Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_04
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_04_major_version_specific``

**Purpose:**
Verify that a FindService with the exact major_version = 0x01 elicits an OfferService.

**Stimulus:**
Send FindService with major_version = 0x01, TTL = 0xFFFFFF.

**Expected Result:**
OfferService received for the configured service with major_version = 0x01.

TC8-SDM-005: FindService: Any Minor Version (0xFFFFFFFF)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_05
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_05_any_minor_version``

**Purpose:**
Verify that a FindService with minor_version = 0xFFFFFFFF (Any Minor Version) elicits an
OfferService.

**Stimulus:**
Send FindService with minor_version = 0xFFFFFFFF.

**Expected Result:**
OfferService received for the configured service with minor_version = 0x00000000.

TC8-SDM-006: FindService: Specific Minor Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_06
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_06_minor_version_specific``

**Purpose:**
Verify that a FindService with the exact minor_version = 0x00000000 elicits an
OfferService.

**Stimulus:**
Send FindService with minor_version = 0x00000000.

**Expected Result:**
OfferService received for the configured service with minor_version = 0x00000000.

TC8-SDM-007: SubscribeEventgroup: Wrong Major Version NAck
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_18; Section 6.1.6, SOMEIP_ETS_142
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_18_wrong_major_version``

**Purpose:**
Verify that a SubscribeEventgroup with a non-matching major version is answered with a
NAck (SubscribeEventgroupAck with TTL = 0) that carries the requested major version.

**Stimulus:**
Send SubscribeEventgroup for the configured service, instance and eventgroup with
major_version = 0x02 (configured major version + 1), TTL = 0xFFFFFF, Reserved = 0.

**Expected Result:**
SubscribeEventgroupAck with TTL = 0 and major_version = 0x02.

TC8-SDM-008: SubscribeEventgroup: Unknown Service ID NAck
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_14-16; Section 6.1.6, SOMEIP_ETS_143
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_14_15_16_unknown_service_id``

**Purpose:**
Verify that a SubscribeEventgroup for an unknown service is answered with a NAck that
echoes the request.

**Stimulus:**
Send SubscribeEventgroup with service_id = 0xBEEF and the configured instance, eventgroup
and major version, TTL = 0xFFFFFF, Reserved = 0.

**Expected Result:**
SubscribeEventgroupAck with TTL = 0, service_id = 0xBEEF and the instance ID, eventgroup
ID and major version of the request, Reserved = 0.

TC8-SDM-009: SubscribeEventgroup: Unknown Instance ID NAck
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_17; Section 6.1.6, SOMEIP_ETS_141
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_17_wrong_instance_id``

**Purpose:**
Verify that a SubscribeEventgroup for an unknown instance is answered with a NAck that
carries the requested instance ID.

**Stimulus:**
Send SubscribeEventgroup with instance_id = 0x5679 (configured instance + 1), TTL =
0xFFFFFF, Reserved = 0.

**Expected Result:**
SubscribeEventgroupAck with TTL = 0 and instance_id = 0x5679.

TC8-SDM-010: SubscribeEventgroup: Unknown Eventgroup NAck
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_19
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_19_unknown_eventgroup_id``

**Purpose:**
Verify that a SubscribeEventgroup for an unknown eventgroup ID is answered with a NAck.

**Stimulus:**
Send SubscribeEventgroup with eventgroup_id = 0xBEEF, TTL = 0xFFFFFF.

**Expected Result:**
SubscribeEventgroupAck with eventgroup_id = 0xBEEF and TTL = 0 (NAck).

TC8-SDM-012: SubscribeEventgroup: Reserved Fields of Endpoint Option Set
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_144
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_ets_144_reserved_field_endpoint_option_set``

**Purpose:**
Verify that the DUT ignores set reserved fields of the IPv4 Endpoint Option in a
SubscribeEventgroup and sends a SubscribeEventgroupAck.

**Stimulus:**
Send SubscribeEventgroup whose Endpoint Option has its reserved fields set.

**Expected Result:**
SubscribeEventgroupAck with TTL > 0.

TC8-SDM-013: FindService After Half Cyclic Offer Delay: Multicast Response
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_03
:Requirement: ``comp_req__tc8_conformance__sd_find_mcast_rsp``
:Test Function: ``TestSDFindServiceTiming::test_sd_behavior_03_unicast_findservice_timing``

**Purpose:**
Verify that a FindService with the Unicast Flag set, received at least half a cyclic
offer delay after the last OfferService, is answered with a multicast OfferService.

**Stimulus:**
Wait for the main phase and receive a cyclic OfferService. Wait
``cyclic_offer_delay`` / 2 (1 s), then send FindService with the Unicast Flag set.

**Expected Result:**
OfferService with IP destination address equal to the SD multicast group, received
within the listen time (``ParamListenTime``, default 10 s).

TC8-SDM-014: FindService With Unicast Flag Cleared: Multicast Response
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_04
:Requirement: ``comp_req__tc8_conformance__sd_find_mcast_rsp``
:Test Function: ``TestSDFindServiceTiming::test_sd_behavior_04_multicast_findservice_timing``

**Purpose:**
Verify that a FindService with the Unicast Flag cleared is answered with a multicast
OfferService.

**Stimulus:**
Wait for the main phase, then send FindService with the Unicast Flag = 0.

**Expected Result:**
OfferService with IP destination address equal to the SD multicast group, received
within ``cyclic_offer_delay`` (2 s).

SD Lifecycle Advanced Tests
----------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_service_discovery`` (TestSDSubscribeLifecycleAdvanced, TestSDFindServiceAdvanced) and ``test_sd_client``

TC8-SDLC-001: Multiple Subscribes in One Message
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_088
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_088_two_subscribes_same_session``

**Purpose:**
Verify (OA SD_Answer_multiple_subscribes_together) that three SubscribeEventgroup entries (eventgroups ``0x0002``,
``0x0005``, ``0x0006``) in one SD message each receive a positive ACK.

**Stimulus:**
Send an SD message containing three SubscribeEventgroup entries.

**Expected Result:**
Three ACK entries received (one per subscription).

TC8-SDLC-002: TTL = 0 as StopSubscribe (No NAck)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_092
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_092_ttl_zero_stop_subscribe_no_nack``

**Purpose:**
Verify that a unicast SubscribeEventgroup with TTL = 0 gets no reaction from the DUT (no
Ack, no NAck).

**Stimulus:**
Send a unicast SubscribeEventgroup with TTL = 0 (no prior subscription).

**Expected Result:**
No SubscribeEventgroupAck or SubscribeEventgroupNack received within 2 seconds.

The OA procedure numbers its steps from 3.

TC8-SDLC-004: Entries In One Message Processed In Order
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_107
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_107_consider_entries_order``

**Purpose:**
Verify that the DUT handles the entries of one SD message in order
(SOMEIP_ETS_107, SD_Consider_Entries_Order).

**Stimulus:**
Subscribe to eventgroup ``0x0005``, then send one SD message with a
StopSubscribeEventgroup entry (TTL = 0) followed by a SubscribeEventgroup entry for the
same eventgroup.

**Expected Result:**
The entries are processed in order: a new SubscribeAck is received and the initial
fields (``0x8005`` to ``0x8007``) arrive again after the re-subscribe.

TC8-SDLC-005: Events Sent to Endpoint Option Port
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_120
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_120_events_sent_to_endpoint_option_port``

**Purpose:**
Verify that events go to the address and port in the endpoint option, not to the
SD packet source port. OA ETS_120 also changes the IP address, but the test uses
the tester IP with a different port, so it only partially verifies the requirement.

**Stimulus:**
Send SubscribeEventgroup from the SD socket with an endpoint option naming a
different tester port.

**Expected Result:**
The ACK arrives on the SD socket and the initial fields (``0x8005`` to ``0x8007``)
arrive on the endpoint option socket. Not verified: an option IP address that differs
from the tester IP address.

TC8-SDLC-007: Re-subscribe After StopSubscribe
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_155
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_155_resubscribe_after_stop``

**Purpose:**
Verify that a subscription can be re-established after a StopSubscribeEventgroup
(TTL = 0), a new ACK is sent and the initial fields arrive. The OA procedure has no
tester step for the re-subscribe, the OA synopsis says the tester subscribes again.

**Stimulus:**
Subscribe, wait for the ACK, StopSubscribe, re-Subscribe.

**Expected Result:**
A new SubscribeAck and the initial fields (``0x8005`` to ``0x8007``) are received after
the re-subscription.

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

TC8-SDLC-009: Events Sent After Subscribe ACK
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_121
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_121_events_sent_after_subscribe_ack``

**Purpose:**
Verify that the DUT sends the initial fields to a new subscriber after a successful
SubscribeEventgroup ACK.

**Stimulus:**
Subscribe to eventgroup ``0x0005``; wait for the ACK and the initial fields.

**Expected Result:**
A SubscribeEventgroupAck and the initial fields (``0x8005`` to ``0x8007``) received
within 5 seconds after the ACK.

TC8-SDLC-011: StopOfferService Stops FindService
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_101
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_101_stop_offer_ceases_client_events``

**Purpose:**
Verify that the DUT stops sending FindService for a service after a StopOfferService.

**Stimulus:**
N/A (skipped).

**Expected Result:**
Not applicable: the DUT has no ETS client mode (``clientServiceActivate``).

TC8-SDLC-012: Multicast FindService: Any Major and Any Minor Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_128
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_128_multicast_findservice_version_any``

**Purpose:**
Verify that a multicast FindService with Any Major Version (0xFF) and with Any Minor
Version (0xFFFFFFFF) is answered with a unicast OfferService.

**Stimulus:**
Send 10 multicast FindService messages at 100 ms intervals with major_version = 0xFF
(other fields default), then 10 at 100 ms intervals with minor_version = 0xFFFFFFFF
(other fields default).

**Expected Result:**
At least one unicast OfferService received for the configured service in each phase.

TC8-SDLC-013: Multicast FindService: Unicast Flag Clear
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_130
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_130_multicast_findservice_unicast_flag_clear``

**Purpose:**
Verify that a multicast FindService with the unicast flag cleared (0x00) is answered
with a unicast OfferService (the flag is ignored).

**Stimulus:**
Send multicast FindService with unicast flag = 0.

**Expected Result:**
Unicast OfferService received on the tester socket.

TC8-SDLC-015: Restart: Reboot Flag and Session ID After First Restart
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_07; Section 6.1.5.1, SOMEIPSRV_FORMAT_02
:Requirement: ``comp_req__tc8_conformance__sd_reboot``
:Test Module: ``test_sd_client``
:Test Function: ``TestSDClientReboot::test_format_07_reboot_flag_set_after_first_restart``

**Purpose:**
Verify that after a DUT restart the first SD message has the reboot flag (bit 7 of SD
flags byte) set and the session_id restarts at 0x0001.

**Stimulus:**
Start the DUT, drain 3 SD messages, terminate it, restart it and capture the first
post-restart SD message.

**Expected Result:**
``sd_hdr.flag_reboot == True`` and ``outer.session_id == 1``.

Not verified: the reboot flag changing to 0 after the Session ID wraps (needs 65535
messages).

TC8-SDLC-016: Restart: Reboot Flag and Session ID After Second Restart
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_07; Section 6.1.5.1, SOMEIPSRV_FORMAT_02
:Requirement: ``comp_req__tc8_conformance__sd_reboot``
:Test Module: ``test_sd_client``
:Test Function: ``TestSDClientReboot::test_format_07_reboot_flag_set_after_second_restart``

**Purpose:**
Verify that the reboot flag and session ID restart hold across a second consecutive
restart (not just the first).

**Stimulus:**
Start, drain, stop, start, drain, stop, start, then capture the first message.

**Expected Result:**
``sd_hdr.flag_reboot == True`` and ``outer.session_id == 1``.

Not verified: the reboot flag changing to 0 after the Session ID wraps (needs 65535
messages).

TC8-SDLC-019: Subscribe TTL Expiry Stops Events
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_095
:Requirement: ``comp_req__tc8_conformance__sd_ttl_expiry``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_095_subscribe_ttl_expires_no_events``

**Purpose:**
Verify that when a subscription's TTL expires without renewal the DUT ceases sending
event notifications to that subscriber.

**Stimulus:**
Subscribe to eventgroup ``0x0002`` with TTL = 3 seconds, wait 5 seconds without
renewal, observe events.

**Expected Result:**
No NOTIFICATION messages received after TTL expiry window.

Not verified: the reaction to an explicit TriggerEventUint8 call (the DUT has no
trigger method).

SD Message Format Tests
-----------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_sd_format``

TC8-SDF-001: Client ID
^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_01
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDFormat::test_format_01_client_id``

**Purpose:**
Verify that the Client ID of SD messages sent by the DUT is 0x0000.

**Stimulus:**
None, passive observation of DUT SD messages.

**Expected Result:**
Client ID = ``0x0000``.

TC8-SDF-002: Protocol Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_03
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDFormat::test_format_03_protocol_version``

**Purpose:**
Verify that the Protocol Version of SD messages sent by the DUT is 0x01.

**Stimulus:**
None, passive observation of DUT SD messages.

**Expected Result:**
Protocol Version = ``0x01``.

TC8-SDF-003: Message Type
^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_05
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDFormat::test_format_05_message_type``

**Purpose:**
Verify that the Message Type of SD messages sent by the DUT is 0x02.

**Stimulus:**
None, passive observation of DUT SD messages.

**Expected Result:**
Message Type = ``0x02``.

TC8-SDF-004: Return Code
^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_06
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDFormat::test_format_06_return_code``

**Purpose:**
Verify that the Return Code of SD messages sent by the DUT is 0x00.

**Stimulus:**
None, passive observation of DUT SD messages.

**Expected Result:**
Return Code = ``0x00``.

TC8-SDF-005: Unicast Flag
^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_08
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDFormat::test_format_08_unicast_flag``

**Purpose:**
Verify that the Unicast Flag of SD messages sent by the DUT is set.

**Stimulus:**
None, passive observation of DUT SD messages.

**Expected Result:**
Unicast Flag bit = 1.

TC8-SDF-006: Undefined Flag Bits
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_09
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDFormat::test_format_09_undefined_flag_bits``

**Purpose:**
Verify that the undefined bits of the SD Flags field are 0.

**Stimulus:**
None, passive observation of DUT SD messages.

**Expected Result:**
The six Flag bits other than the Reboot and Unicast bits are 0.

TC8-SDF-007: Reserved Bits
^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_10
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDFormat::test_format_10_reserved_bits``

**Purpose:**
Verify that the Reserved field of SD messages sent by the DUT is 0.

**Stimulus:**
None, passive observation of DUT SD messages.

**Expected Result:**
Reserved = ``0x000000``.

TC8-SDF-008: Type 1 Entry Length
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_11
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDOfferEntryFormat::test_format_11_type_1_entry_length``

**Purpose:**
Verify the Entry Type and the Entries Array length of OfferService messages.

**Stimulus:**
None, passive observation of DUT SD messages.

**Expected Result:**
Entry Type = ``0x01`` and Entries Array length = number of entries x 16.

TC8-SDF-009: Type 1 Index First Option Run
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_12
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDOfferEntryFormat::test_format_12_type_1_index_first_option_run``

**Purpose:**
Verify the Index First Option Run of OfferService entries.

**Stimulus:**
None, passive observation of DUT SD messages.

**Expected Result:**
Index First Option Run = 0.

TC8-SDF-010: Type 1 Number of Options 1
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_13
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDOfferEntryFormat::test_format_13_type_1_number_of_options_1``

**Purpose:**
Verify the Number of Options 1 of OfferService entries.

**Stimulus:**
None, passive observation of DUT SD messages.

**Expected Result:**
Number of Options 1 >= 1.

TC8-SDF-011: Offer TTL
^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_07
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDOfferEntryFormat::test_sd_message_07_offer_ttl``

**Purpose:**
Verify that the TTL of an OfferService entry equals the lifetime of the service instance.

**Stimulus:**
Send FindService for service ``0x1234`` and take the OfferService.

**Expected Result:**
TTL = 30 (configured ``ttl``).

Not verified: OA steps 6 to 9 (wait for the TTL timeout, send a Request, expect no
Response). The DUT keeps offering the service cyclically.

TC8-SDF-012: Type 2 Entry Type
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_19
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeAckFormat::test_format_19_type_2_entry_type``

**Purpose:**
Verify the Entry Type of the SubscribeEventgroupAck.

**Stimulus:**
Send FindService for service ``0x1234``, take the OfferService, send
SubscribeEventgroup for eventgroup ``0x0002`` with TTL = 30 (configured ``ttl``) and
capture the SubscribeEventgroupAck.

**Expected Result:**
Entry Type = ``0x07``.

TC8-SDF-013: Type 2 Entry Length
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_20
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeAckFormat::test_format_20_type_2_entry_length``

**Purpose:**
Verify the Entries Array length of the SubscribeEventgroupAck.

**Stimulus:**
Send FindService for service ``0x1234``, take the OfferService, send
SubscribeEventgroup for eventgroup ``0x0002`` with TTL = 30 (configured ``ttl``) and
capture the SubscribeEventgroupAck.

**Expected Result:**
Entries Array length = number of entries x 16.

TC8-SDF-014: Type 2 Index First Option Run
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_21
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeAckFormat::test_format_21_type_2_index_first_option_run``

**Purpose:**
Verify the Index First Option Run of the SubscribeEventgroupAck entry.

**Stimulus:**
Send FindService for service ``0x1234``, take the OfferService, send
SubscribeEventgroup for eventgroup ``0x0002`` with TTL = 30 (configured ``ttl``) and
capture the SubscribeEventgroupAck.

**Expected Result:**
Index First Option Run = 0.

TC8-SDF-015: Type 2 Service ID
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_23
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeAckFormat::test_format_23_type_2_service_id``

**Purpose:**
Verify the Service ID of the SubscribeEventgroupAck entry.

**Stimulus:**
Send FindService for service ``0x1234``, take the OfferService, send
SubscribeEventgroup for eventgroup ``0x0002`` with TTL = 30 (configured ``ttl``) and
capture the SubscribeEventgroupAck.

**Expected Result:**
Service ID = ``0x1234``.

TC8-SDF-016: Type 2 Instance ID
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_24
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeAckFormat::test_format_24_type_2_instance_id``

**Purpose:**
Verify the Instance ID of the SubscribeEventgroupAck entry.

**Stimulus:**
Send FindService for service ``0x1234``, take the OfferService, send
SubscribeEventgroup for eventgroup ``0x0002`` with TTL = 30 (configured ``ttl``) and
capture the SubscribeEventgroupAck.

**Expected Result:**
Instance ID = ``0x5678``.

TC8-SDF-017: Type 2 Major Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_25
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeAckFormat::test_format_25_type_2_major_version``

**Purpose:**
Verify the Major Version of the SubscribeEventgroupAck entry.

**Stimulus:**
Send FindService for service ``0x1234``, take the OfferService, send
SubscribeEventgroup for eventgroup ``0x0002`` with TTL = 30 (configured ``ttl``) and
capture the SubscribeEventgroupAck.

**Expected Result:**
Major Version = ``0x01``.

TC8-SDF-018: Type 2 TTL
^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_26
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeAckFormat::test_format_26_type_2_ttl``

**Purpose:**
Verify the TTL of the SubscribeEventgroupAck entry.

**Stimulus:**
Send FindService for service ``0x1234``, take the OfferService, send
SubscribeEventgroup for eventgroup ``0x0002`` with TTL = 30 (configured ``ttl``) and
capture the SubscribeEventgroupAck.

**Expected Result:**
TTL = 30 (configured ``ttl``).

TC8-SDF-019: Type 2 Reserved
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_27
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeAckFormat::test_format_27_type_2_reserved``

**Purpose:**
Verify the Reserved field of the SubscribeEventgroupAck entry.

**Stimulus:**
Send FindService for service ``0x1234``, take the OfferService, send
SubscribeEventgroup for eventgroup ``0x0002`` with TTL = 30 (configured ``ttl``) and
capture the SubscribeEventgroupAck.

**Expected Result:**
Reserved = ``0x0000``.

TC8-SDF-020: Type 2 Eventgroup ID
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_28
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeAckFormat::test_format_28_type_2_eventgroup_id``

**Purpose:**
Verify the Eventgroup ID of the SubscribeEventgroupAck entry.

**Stimulus:**
Send FindService for service ``0x1234``, take the OfferService, send
SubscribeEventgroup for eventgroup ``0x0002`` with TTL = 30 (configured ``ttl``) and
capture the SubscribeEventgroupAck.

**Expected Result:**
Eventgroup ID = ``0x0002``.

TC8-SDF-021: Notification Interface Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.1, SOMEIPSRV_FORMAT_04
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestNotificationFormat::test_format_04_interface_version``

**Purpose:**
Verify that the Interface Version of a notification sent by the DUT is 0x01.

**Stimulus:**
Subscribe to eventgroup ``0x0002`` with TTL = 30 (configured ``ttl``) and receive the
notifications of service ``0x1234`` for 10 s.

**Expected Result:**
Interface Version = ``0x01`` in every notification received.

SD Find Response Tests
----------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_sd_find``

TC8-SDR-001: Discover Port and IP
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_060
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDFindResponse::test_ets_060_discover_port_and_ip``

**Purpose:**
Verify that a multicast FindService is answered with a unicast OfferService that states
the IP addresses and ports needed to reach the service.

**Stimulus:**
Send a multicast FindService for service ``0x1234`` and instance ``0x5678``.

**Expected Result:**
Unicast OfferService with an IPv4 Endpoint Option for UDP (``TC8_DUT_IP``, port 30509).

Not verified: a TCP Endpoint Option (the DUT configuration has no reliable port).

TC8-SDR-002: Ignore Options in FindService
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_118
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDFindResponse::test_ets_118_ignore_options_in_findservice``

**Purpose:**
Verify that the DUT ignores the options of a FindService.

**Stimulus:**
Send 10 FindService messages with an IPv4 Endpoint Option at 100 ms intervals.

**Expected Result:**
At least one unicast OfferService.

TC8-SDR-003: Multicast FindService
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_127
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDFindResponse::test_ets_127_multicast_findservice``

**Purpose:**
Verify that a multicast FindService for a valid service and instance is answered with a
unicast OfferService.

**Stimulus:**
Send 10 multicast FindService messages at 100 ms intervals for service ``0x1234`` and
instance ``0x5678``.

**Expected Result:**
At least one unicast OfferService.

SD TCP Tests
------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``
:Test Module: ``test_sd_tcp``

TC8-SDT-001: Layer 4 Protocol of TCP Endpoint Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.5.2, SOMEIPSRV_OPTIONS_15
:Requirement: ``comp_req__tc8_conformance__sd_endpoint_option``
:Test Function: ``TestSDTcpBinding::test_options_15_tcp_endpoint_l4_protocol``

**Purpose:**
Verify that an OfferService of a TCP-only service carries exactly one IPv4
Endpoint Option and that its Layer 4 protocol is TCP.

**Stimulus:**
Send FindService for service ``0x1234`` and take the OfferService.

**Expected Result:**
Exactly one IPv4 Endpoint Option, with Layer 4 protocol ``0x06`` (TCP).

The module uses a TCP-only service configuration (reliable port, no unreliable port), so
the OfferService carries a single Endpoint Option.

TC8-SDT-002: Subscribe With TCP Option Without Connection
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_096
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDTcpBinding::test_ets_096_subscribe_tcp_option_without_connection``

**Purpose:**
Verify that a SubscribeEventgroup for eventgroup 0x0002 with a TCP Endpoint Option is
rejected when no TCP connection exists.

**Stimulus:**
Send SubscribeEventgroup for eventgroup ``0x0002`` with a TCP Endpoint Option, without
establishing the TCP connection.

**Expected Result:**
SubscribeEventgroupNAck.

SD Subscribe Option Tests
-------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_sd_subscribe_options``

TC8-SDO-001: No Port in Endpoint Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_109
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeEndpointOptions::test_ets_109_no_port_in_endpoint_option``

**Purpose:**
Verify that a SubscribeEventgroup whose Endpoint Option has port 0 is rejected.

**Stimulus:**
Send SubscribeEventgroup with port 0 in the Endpoint Option.

**Expected Result:**
SubscribeEventgroupNAck.

TC8-SDO-002: No IPv4 Address in Endpoint Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_110
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeEndpointOptions::test_ets_110_no_ipv4_address_in_endpoint_option``

**Purpose:**
Verify that a SubscribeEventgroup without a valid IPv4 address in the Endpoint Option is
rejected.

**Stimulus:**
Send SubscribeEventgroup with IP address 32.0.0.0 in the Endpoint Option.

**Expected Result:**
SubscribeEventgroupNAck.

TC8-SDO-003: Wrong L4 Protocol in Endpoint Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_119
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeEndpointOptions::test_ets_119_wrong_l4proto``

**Purpose:**
Verify that a SubscribeEventgroup whose IPv4 Endpoint Option has an l4proto that is
neither UDP nor TCP is rejected.

**Stimulus:**
Send SubscribeEventgroup referencing an IPv4 Endpoint Option with l4proto ``0x00``.

**Expected Result:**
SubscribeEventgroupNAck.

The OA Pass criteria say "at least one unicast OfferService", which contradicts the OA
procedure and synopsis. The test follows the procedure and synopsis. The OA gives no
l4proto value, ``0x00`` is a neutral choice.

TC8-SDO-004: Unexisting IPv4 Address in Endpoint Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_154
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeEndpointOptions::test_ets_154_unexisting_ipv4_address``

**Purpose:**
Verify that a SubscribeEventgroup with an invalid IPv4 address in the Endpoint Option is
rejected.

**Stimulus:**
Send SubscribeEventgroup with IP address 169.254.200.200 in the Endpoint Option (the OA
gives no address, this value is a neutral choice).

**Expected Result:**
SubscribeEventgroupNAck.

Expected failure (strict xfail). OA expectation: SubscribeEventgroupNAck. Observed: no
NAck received.

TC8-SDO-005: Endpoint Option With DUT IP Address
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_162
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeEndpointOptions::test_ets_162_unallowed_option_ip``

**Purpose:**
Verify that a SubscribeEventgroup whose Endpoint Option carries the IP address of the DUT
is rejected.

**Stimulus:**
Send SubscribeEventgroup with the DUT IP address in the Endpoint Option.

**Expected Result:**
SubscribeEventgroupNAck.

TC8-SDO-006: Endpoint Option With 111.111.111.111
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_164
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeEndpointOptions::test_ets_164_unallowed_option_ip_2``

**Purpose:**
Verify that a SubscribeEventgroup whose Endpoint Option carries IP address
111.111.111.111 is rejected.

**Stimulus:**
Send SubscribeEventgroup with IP address 111.111.111.111 in the Endpoint Option.

**Expected Result:**
SubscribeEventgroupNAck.

TC8-SDO-007: Unicast SubscribeEventgroup With Option Indexes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_172 (alias SOMEIP_ETS_173)
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeEndpointOptions::test_ets_172_unicast_subscribe_option_indexes``

**Purpose:**
Verify that unicast subscriptions with different option index and run settings are
acknowledged.

**Stimulus:**
Send a unicast SubscribeEventgroup with index1stOptions = 0, index2ndOptions = 1,
numOfOptions1 = 1, numOfOptions2 = 1. Then send one with index1stOptions = 0,
index2ndOptions = 0, numOfOptions1 = 2, numOfOptions2 = 0. The OA gives no option
content: the test uses one UDP and one TCP IPv4 Endpoint Option, both with the tester IP
address and the sender port.

**Expected Result:**
A unicast SubscribeEventgroupAck for each.

The PDF heading prints SOMEIP_ETS_172 and SOMEIP_ETS_173 for this test.

Expected failure (strict xfail). OA expectation: a unicast SubscribeEventgroupAck for
each option layout. Observed: no Ack received for either layout.

TC8-SDO-008: Unreferenced Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_175
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeEndpointOptions::test_ets_175_unreferenced_option``

**Purpose:**
Verify that an unreferenced Configuration Option does not stop the DUT from acknowledging
a subscription.

**Stimulus:**
Send SubscribeEventgroup with all needed Endpoint Options and an additional unreferenced
Configuration Option. The OA gives no option content: the test appends the bytes
``00 02 01 00 00`` (empty string) after the Endpoint Option, and the entry does not
reference them.

**Expected Result:**
SubscribeEventgroupAck.

SD Malformed Message Tests
--------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_sd_malformed``

Base message of all entries: one SubscribeEventgroup entry that references one UDP IPv4
Endpoint Option (tester IP address, sender port). The OA gives no option content.

TC8-SDX-001: Empty Entries Array
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_111
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedArrays::test_ets_111_empty_entries_array``

**Purpose:**
Verify that a SubscribeEventgroup with an empty Entries Array is ignored.

**Stimulus:**
Send a SubscribeEventgroup entry with the Entries Array length field set to 0.

**Expected Result:**
No SD entry of any type within 5 s.

The OA synopsis says a total length of zero "should not be interpreted". The test sends a
real entry with the length field set to 0.

TC8-SDX-002: Empty Option
^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_112
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedArrays::test_ets_112_empty_option``

**Purpose:**
Verify that a SubscribeEventgroup with IPv4 Option length 0 is rejected.

**Stimulus:**
Send SubscribeEventgroup for eventgroup ``0x0002`` with the length field of the IPv4
Endpoint Option set to 0.

**Expected Result:**
SubscribeEventgroupNAck.

ETS_112 and ETS_113 share the same synopsis text in the OA.

TC8-SDX-003: Empty Options Array
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_113
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedArrays::test_ets_113_empty_options_array``

**Purpose:**
Verify that a SubscribeEventgroup with Options Array length 0 is rejected.

**Stimulus:**
Send SubscribeEventgroup for eventgroup ``0x0002`` with Options Array length 0 and an
entry that references no option.

**Expected Result:**
SubscribeEventgroupNAck.

The OA synopsis says "IPv4Option length = 0", the OA procedure says "options array length
= 0". The test follows the procedure. The OA does not say whether the entry references
options.

TC8-SDX-004: Entries Length Wrong, Two Entries
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_114
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedArrays::test_ets_114_entries_length_wrong_combined``

**Purpose:**
Verify that a SubscribeEventgroup with two correct entries but a shortened Entries Array
length is rejected.

**Stimulus:**
Send two identical SubscribeEventgroup entries with the Entries Array length set to 16
(one entry).

**Expected Result:**
SubscribeEventgroupNAck.

TC8-SDX-005: More Option References Than Options
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_115
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedArrays::test_ets_115_entry_references_more_options_than_exist``

**Purpose:**
Verify that a SubscribeEventgroup that references more options than exist is rejected.

**Stimulus:**
Send SubscribeEventgroup with Number of Options 1 = 3 and one existing option. The OA
gives no count.

**Expected Result:**
SubscribeEventgroupNAck.

TC8-SDX-006: Unknown Option Type 0x77
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_116
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedArrays::test_ets_116_entry_references_non_existing_option_type``

**Purpose:**
Verify that a SubscribeEventgroup that references an unknown option type (0x77) is
rejected.

**Stimulus:**
Send SubscribeEventgroup referencing an option of type ``0x77`` with length field 5, one
reserved byte and four zero bytes.

**Expected Result:**
SubscribeEventgroupNAck.

TC8-SDX-007: Two Options of the Same Kind
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_117
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedArrays::test_ets_117_entry_references_options_of_same_kind``

**Purpose:**
Verify that a SubscribeEventgroup with two options of the same type is rejected or
ignored.

**Stimulus:**
Send one SubscribeEventgroup entry with Number of Options 1 = 2 and two identical IPv4
Endpoint Options (tester IP address, sender port).

**Expected Result:**
SubscribeEventgroupNAck or no reaction (no Ack with TTL above 0).

TC8-SDX-008: Entries Array Longer Than Message
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_123
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedArrays::test_ets_123_length_of_entry_array_longer_than_message_allows``

**Purpose:**
Verify that a SubscribeEventgroup whose Entries Array length exceeds the total length is
rejected.

**Stimulus:**
Send SubscribeEventgroup with the Entries Array length set to the datagram length plus 1
(the OA gives no value).

**Expected Result:**
SubscribeEventgroupNAck.

The OA synopsis says the message is "unrecognizable" or "too damaged to be answered with
an Ack", the OA procedure and Pass criteria require a NAck. The test follows the
procedure.

Expected failure (strict xfail). OA expectation: SubscribeEventgroupNAck. Observed: no
NAck received within 5 s.

TC8-SDX-009: Entries Array Too Long
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_124
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedArrays::test_ets_124_length_of_entry_array_too_long``

**Purpose:**
Verify that a SubscribeEventgroup whose Entries Array length exceeds the total length by
at least 20 bytes is rejected.

**Stimulus:**
Send SubscribeEventgroup with the Entries Array length set to the datagram length plus
20.

**Expected Result:**
SubscribeEventgroupNAck.

The OA synopsis says the Entries Array length "does not exceed" the Message total length,
the OA procedure says it exceeds it by at least 20 bytes. The test follows the
procedure.

Expected failure (strict xfail). OA expectation: SubscribeEventgroupNAck. Observed: no
NAck received within 5 s.

TC8-SDX-010: Entries Array Too Short
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_125
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedArrays::test_ets_125_length_of_entry_array_too_short``

**Purpose:**
Verify that a SubscribeEventgroup whose Entries Array length is less than the sum of the
lengths of its entries is rejected.

**Stimulus:**
Send one SubscribeEventgroup entry (16 bytes) with the Entries Array length set to 15.
The OA gives no value.

**Expected Result:**
SubscribeEventgroupNAck.

Expected failure (strict xfail). OA expectation: SubscribeEventgroupNAck. Observed: no
NAck received within 5 s.

TC8-SDX-011: Option Ends Past Options Array, Variant A
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_134
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedOptionsAndHeader::test_ets_134_option_length_ends_past_options_array_var_a``

**Purpose:**
Verify that a SubscribeEventgroup whose option length surpasses the Options Array length
is rejected or ignored.

**Stimulus:**
Send SubscribeEventgroup with the SOME/IP length and the Options Array length both cut
by 4 bytes, option length fields unchanged, one option. The OA cuts both by 12 bytes
(60 to 48 and 24 to 12).

**Expected Result:**
SubscribeEventgroupNAck or no reaction (no Ack with TTL above 0).

The OA synopsis implies two options of 12 bytes, the OA procedure only says "option
length greater than the options array length". The test follows the synopsis shape with
one option.

TC8-SDX-012: Option Ends Past Options Array, Variant B
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_135
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedOptionsAndHeader::test_ets_135_option_length_ends_past_options_array_var_b``

**Purpose:**
Verify that a SubscribeEventgroup whose option length surpasses the Options Array length
is rejected or ignored.

**Stimulus:**
Send SubscribeEventgroup with Options Array length 8 and one option of 12 bytes. The OA
gives no value.

**Expected Result:**
SubscribeEventgroupNAck or no reaction (no Ack with TTL above 0).

The OA synopsis ends with a NAck only, the OA procedure and Pass criteria allow a NAck or
no reaction. The test follows the procedure.

TC8-SDX-013: UDP Option Length 4
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_136
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedOptionsAndHeader::test_ets_136_option_length_shorter_gt_0_as_specified_for_type``

**Purpose:**
Verify that a SubscribeEventgroup with a UDP option length shorter than specified for the
type is rejected or ignored.

**Stimulus:**
Send SubscribeEventgroup with the length field of the UDP IPv4 Endpoint Option set to 4
instead of 9.

**Expected Result:**
SubscribeEventgroupNAck or no reaction (no Ack with TTL above 0).

TC8-SDX-014: Shorter Option With Unaligned Next Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_137
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedOptionsAndHeader::test_ets_137_option_shorter_with_unaligned_next_option``

**Purpose:**
Verify that a SubscribeEventgroup with wrong sized options that keep the total Options
Array length is rejected.

**Stimulus:**
Send one SubscribeEventgroup entry with Number of Options 1 = 2 and these options, in
this order: a TCP IPv4 Endpoint Option with length field 14 followed by 5 dummy bytes,
then a UDP IPv4 Endpoint Option with length field 4 followed by 7 bytes. The Options
Array length is 24 and the SOME/IP length is consistent with the datagram.

**Expected Result:**
SubscribeEventgroupNAck.

Known limitation: A NAck is also returned for a valid Subscribe that references two options, so this
check cannot attribute the NAck to the injected fault alone.

TC8-SDX-015: Options Array Longer Than Message
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_138
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedOptionsAndHeader::test_ets_138_options_array_longer_than_message_allows``

**Purpose:**
Verify that a SubscribeEventgroup whose Options Array length is longer than the message
is acknowledged or ignored.

**Stimulus:**
Send SubscribeEventgroup with Options Array length 0x28 and one option of 12 bytes.

**Expected Result:**
SubscribeEventgroupAck or no reaction (no NAck).

The OA synopsis says "0x28 instead of 0x18", which assumes a 24 byte array with two
options. The test keeps 0x28 with one option.

TC8-SDX-016: Options Array Too Short
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_139
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedOptionsAndHeader::test_ets_139_options_array_too_short``

**Purpose:**
Verify that a SubscribeEventgroup whose Options Array length is shorter than required is
rejected.

**Stimulus:**
Send SubscribeEventgroup with Options Array length 2 and one option of 12 bytes.

**Expected Result:**
SubscribeEventgroupNAck.

TC8-SDX-017: SOME/IP Length Shorter Than Actual
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_153
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedOptionsAndHeader::test_ets_153_someip_length_shorter_as_expected``

**Purpose:**
Verify that a SubscribeEventgroup whose SOME/IP length is less than the actual length is
rejected or ignored.

**Stimulus:**
Send SubscribeEventgroup with the SOME/IP length set to the actual length minus 1.

**Expected Result:**
SubscribeEventgroupNAck or no reaction (no Ack with TTL above 0).

TC8-SDX-018: Unknown Option Type
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_174
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedOptionsAndHeader::test_ets_174_unknown_option_type``

**Purpose:**
Verify that a SubscribeEventgroup that references an unknown option type is rejected.

**Stimulus:**
Send one SubscribeEventgroup entry with Number of Options 1 = 2. Option 0 has the
unknown type ``0x77`` (length field 9, zero content), option 1 is a UDP IPv4 Endpoint
Option. The OA layout uses a TCP option instead of the unknown option.

**Expected Result:**
SubscribeEventgroupNAck.

Known limitation: A NAck is also returned for a valid Subscribe that references two options, so this
check cannot attribute the NAck to the injected fault alone.

TC8-SDX-019: Unused Data After Options Array
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_176
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedOptionsAndHeader::test_ets_176_unused_data_after_options_array``

**Purpose:**
Verify that unused payload data after the Options Array does not stop the DUT from
acknowledging a unicast subscription.

**Stimulus:**
Send a unicast SubscribeEventgroup with the unused data ``0x30303a3031`` after the
Options Array, counted in the SOME/IP length. Then send one with the same data, not
counted in the SOME/IP length.

**Expected Result:**
A SubscribeEventgroupAck for each.

TC8-SDX-020: Unused Data After Options Array, Wrong Length
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_177
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedOptionsAndHeader::test_ets_177_unused_data_after_options_array_wrong_length``

**Purpose:**
Verify that unused payload data that is not included in the SOME/IP length does not stop
the DUT from acknowledging a unicast subscription.

**Stimulus:**
Send a unicast SubscribeEventgroup with the unused data ``0x30303a3031`` at the end, not
included in the SOME/IP length.

**Expected Result:**
SubscribeEventgroupAck.

TC8-SDX-021: Wrong SOME/IP Message ID
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_178
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDMalformedOptionsAndHeader::test_ets_178_subscribe_using_wrong_someip_messageid``

**Purpose:**
Verify that a SubscribeEventgroup whose SOME/IP header has a wrong Message ID for Service
Discovery is rejected.

**Stimulus:**
Send SubscribeEventgroup with Method ID ``0x8101`` instead of ``0x8100`` in the SOME/IP
header (the OA gives no value).

**Expected Result:**
SubscribeEventgroupNAck.

Expected failure (strict xfail). OA expectation: SubscribeEventgroupNAck. Observed: no
NAck received within 5 s.

SD Initial Field and Reboot Detection Tests
-------------------------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_sd_initial_fields``

TC8-SDI-001: Initial Fields, Eventgroup 0x0002
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_086
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestEts086::test_ets_086_events_and_fields_all_2``

**Purpose:**
Verify that the DUT acknowledges a subscription to eventgroup 0x0002 and sends all initial
fields.

**Stimulus:**
Wait 6 s after the DUT is ready, so that the DUT application has published its field
values. Then subscribe to eventgroup ``0x0002`` and receive events for 4 s after the Ack.

**Expected Result:**
SubscribeEventgroupAck, then the initial fields ``0x8005`` (Interface Version), ``0x8006``
(TestfieldUINT8) and ``0x8007`` (TestfieldUINT8Array) to the tester endpoint.

Not verified: field ``0x8008`` (TestFieldUINT8Reliable), the DUT configuration has no
reliable port.

Expected failure (strict xfail). OA expectation: the initial fields ``0x8005`` to
``0x8007`` after the Ack. Observed: the Ack is received, the initial fields are not.

TC8-SDI-002: Initial Fields, Eventgroup 0x0005
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_087
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestEts087::test_ets_087_events_and_fields_unreliable_5``

**Purpose:**
Verify that the DUT acknowledges a subscription to eventgroup 0x0005 and sends all initial
fields.

**Stimulus:**
Wait 6 s after the DUT is ready, so that the DUT application has published its field
values. Then subscribe to eventgroup ``0x0005`` and receive events for 4 s after the Ack.

**Expected Result:**
SubscribeEventgroupAck, then the initial fields ``0x8005`` (Interface Version), ``0x8006``
(TestfieldUINT8) and ``0x8007`` (TestfieldUINT8Array) to the tester endpoint.

Expected failure (strict xfail). OA expectation: the initial fields ``0x8005`` to
``0x8007`` after the Ack. Observed: the Ack is received, the initial fields are not.

TC8-SDI-003: Reboot Detection, Multicast and Unicast
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_093
:Requirement: ``comp_req__tc8_conformance__sd_reboot``
:Test Function: ``TestEts093::test_ets_093_reboot_detection_multicast_and_unicast``

**Purpose:**
Verify that the DUT detects a client reboot (lower Session ID) separately for multicast
and unicast messages.

**Stimulus:**
All SD messages set the Reboot flag. Each step waits 2 s for the initial event.
A: multicast FindService (Session ID 4, 5, 1), each followed by a unicast
SubscribeEventgroup for eventgroup ``0x0005`` with the same Session ID.
B: after a 4 s wait, so that the subscription of part A expires, unicast
SubscribeEventgroup with Session ID 4, 5, 1.
C: multicast FindService with Session ID 100, 101, 102, each followed by a unicast
SubscribeEventgroup with Session ID 10, 11, 12. The OA gives no wait time before B and no
Session IDs for C, these values are neutral choices.

**Expected Result:**
A, B: SubscribeEventgroupAck each time. Initial event of field ``0x8005`` after the first
subscription, not after the second, again after the third (Session ID 1). C: no
initial event, no reboot detected.

Expected failure (strict xfail). OA expectation: the initial event ``0x8005`` again
after the subscription with Session ID 1 in A and B. Observed: ``0x8005`` is received only
after the first subscription of part A, not after Session ID 1 in A, nor after Session IDs
4 and 1 in B. Part C shows no initial event.

TC8-SDI-004: Reboot Detection, Server Side
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_094
:Requirement: ``comp_req__tc8_conformance__sd_reboot``
:Test Function: ``TestEts094::test_ets_094_reboot_detection_server_side``

**Purpose:**
Verify that the DUT detects a reboot of the tester and stops sending events.

**Stimulus:**
All SD messages set the Reboot flag. Subscribe to eventgroup ``0x0002`` with Session ID 1
and TTL ``0xFFFFFF`` (a short TTL would end the events by itself). Send FindService with
Session ID 5, then again with Session ID 5 (reboot simulation).

**Expected Result:**
SubscribeEventgroupAck, events continue after the first FindService and stop after the
second.

Not verified: the TriggerEventUint8 call (the DUT has no trigger method). The cyclic event
``0x8001`` is observed instead.

SD Session ID Wrap Test
-----------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_sd_session_wrap``

TC8-SDW-001: Session ID Is One After Wrapping
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: Section 6.1.6, SOMEIP_ETS_152
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDSessionWrap::test_ets_152_session_id_one_after_wrap``

**Purpose:**
Verify that the Session ID of the OfferService responses restarts at 0x0001 after the
wrap around.

**Stimulus:**
Send unicast FindService messages in bursts of 50 until the Session ID of the
OfferService responses for instance ``0x5678`` wraps. The OA gives no message count.

**Expected Result:**
The first Session ID after ``0xFFFF`` is ``0x0001``.

The OA Pass criteria also list a SubscribeEventgroupAck, which no step sends. It is not
checked.
