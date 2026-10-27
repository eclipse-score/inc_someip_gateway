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
