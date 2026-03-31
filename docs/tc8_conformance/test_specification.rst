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
   corresponding section from Chapter 5 of the OPEN Alliance
   TC8 Automotive Ethernet ECU Test Specification v3.0.
   See :doc:`traceability` for the full mapping.

.. note:: Terminology

   Throughout this specification, **"server"** refers to the SOME/IP Service Provider role
   (the DUT, which offers services and responds to requests), and **"client"** refers to the
   SOME/IP Service Consumer role (the external test harness, which discovers services and
   subscribes to events). This usage mirrors TC8 OA §5.1.5 ("SOME/IP Server Tests") and
   §5.1.6 ("ETS Client / Control") directly.

Service Discovery Tests
-----------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``

TC8-SD-001 — Multicast Offer on Startup
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_08
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_001_multicast_offer_on_startup``
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that ``someipd`` sends at least one SD OfferService entry on the
configured multicast group (``224.244.224.245:30490``) after startup.

**Preconditions:**

- ``someipd`` started with ``--tc8-standalone`` flag
- Multicast route available (``224.0.0.0/4``)

**Stimuli:**
None — passive observation of DUT multicast traffic.

**Expected Result:**
At least one SOME/IP-SD message containing an OfferService entry is
received on the multicast group within 5 seconds of DUT startup.

TC8-SD-002 — Offer Entry Format
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_14–18
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
None — passive observation.

**Expected Result:**
OfferService entry has ``service_id=0x1234``, ``instance_id=0x5678``,
``major_version=0x00``, ``minor_version=0x00000000``, and ``TTL > 0``.

TC8-SD-003 — Cyclic Offer Timing
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.4 — SOMEIPSRV_SD_BEHAVIOR_02
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
None — passive observation with timestamps.

**Expected Result:**
Inter-offer gaps in main phase are within [1600 ms, 2400 ms].

TC8-SD-004 — FindService Known Service
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.6 — SOMEIP_ETS_171
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

TC8-SD-005 — FindService Unknown Service
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.4 — implied by SD_BEHAVIOR_03/04
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

TC8-SD-006 — Subscribe Eventgroup Ack
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_13
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

TC8-SD-007 — Subscribe Unknown Eventgroup Nack
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_14; §5.1.6 — SOMEIP_ETS_140
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

TC8-SD-008 — StopSubscribe Ceases Notifications
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.6 — SOMEIP_ETS_108, SOMEIP_ETS_092
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

TC8-SD-009 — Repetition Phase Intervals
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.4 — SOMEIPSRV_SD_BEHAVIOR_01
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
None — passive observation from DUT start.

**Expected Result:**
First gap < 1000 ms (half of ``cyclic_offer_delay`` 2000 ms).

TC8-SD-010 — Repetition Count
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.4 — SOMEIPSRV_SD_BEHAVIOR_01
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
None — passive observation from DUT start.

**Expected Result:**
At least 2 short gaps (< 1000 ms) observed before a long gap (main phase).

TC8-SD-011 — IPv4 Endpoint Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_01–07
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
None — passive observation with option parsing.

**Expected Result:**
IPv4EndpointOption present with address matching ``host_ip``,
port = 30509, protocol = UDP.

TC8-SD-012 — Reboot Detection
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_02, FORMAT_07
:Test Module: ``test_sd_reboot.py``
:Test Functions:
  - ``test_tc8_sd_012_reboot_flag_set_after_restart``
  - ``test_tc8_sd_012_session_id_resets_after_restart``
:Requirement: ``comp_req__tc8_conformance__sd_reboot``
:DUT Config: ``tc8_someipd_sd.json``

**Purpose:**
Verify that ``someipd`` resets SD state on restart: reboot flag set
(bit 7 = 1) and session ID reset to ≤ 2.

**Preconditions:**

- DUT started, SD messages captured, then DUT terminated.

**Stimuli:**
Restart ``someipd`` and capture the first post-reboot SD message.

**Expected Result:**
First post-restart SD message has reboot flag set and session ID ≤ 2.

TC8-SD-013 — Multicast Eventgroup Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_08–14
:Test Module: ``test_service_discovery.py``
:Test Function: ``test_tc8_sd_013_subscribe_ack_has_multicast_option``
:Requirement: ``comp_req__tc8_conformance__sd_mcast_eg``
:DUT Config: ``tc8_someipd_sd.json``
:Marker: ``@pytest.mark.network`` (requires non-loopback NIC)

**Purpose:**
Verify that SubscribeEventgroupAck for a multicast eventgroup includes
a multicast IPv4EndpointOption.

**Preconditions:**

- Non-loopback network interface (``TC8_HOST_IP`` set).
- Eventgroup ``0x4465`` configured with multicast address ``239.0.0.1``.

**Stimuli:**
Send SD SubscribeEventgroup for eventgroup ``0x4465``.

**Expected Result:**
SubscribeEventgroupAck contains a multicast IPv4EndpointOption.

TC8-SD-014 — TTL Expiry Cleanup
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.6 — SOMEIP_ETS_095
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

SOME/IP Message Format Tests
-----------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

TC8-MSG-001 — Protocol Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.6 — SOMEIPSRV_ONWIRE_05
:Test Module: ``test_someip_message_format.py``
:Test Function: ``test_tc8_msg_001_protocol_version``
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that RESPONSE messages have ``protocol_version = 0x01``.

**Stimuli:**
Send REQUEST to service ``0x1234``, method ``0x0421``.

**Expected Result:**
RESPONSE with ``protocol_version == 1``.

TC8-MSG-002 — Message Type
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.6 — SOMEIPSRV_ONWIRE_07
:Test Module: ``test_someip_message_format.py``
:Test Functions:
  - ``test_tc8_msg_002_message_type_response``
  - ``test_tc8_msg_002_no_response_for_request_no_return``
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify RESPONSE (0x80) for REQUEST; no response for REQUEST_NO_RETURN.

**Stimuli:**

- Send REQUEST → expect RESPONSE.
- Send REQUEST_NO_RETURN → expect silence.

**Expected Result:**

- REQUEST produces RESPONSE with ``message_type = 0x80``.
- REQUEST_NO_RETURN produces no response within 2 seconds.

TC8-MSG-003 — Unknown Service
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.6 — SOMEIPSRV_ONWIRE_10; §5.1.6 — SOMEIP_ETS_077
:Test Module: ``test_someip_message_format.py``
:Test Function: ``test_tc8_msg_003_unknown_service``
:Requirement: ``comp_req__tc8_conformance__msg_error_codes``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify error handling for requests to non-existent services.

**Stimuli:**
Send REQUEST to service ``0xBEEF``.

**Expected Result:**
``E_UNKNOWN_SERVICE`` (0x02) or no response (both valid per TC8).

TC8-MSG-004 — Unknown Method
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.6 — SOMEIPSRV_ONWIRE_12; §5.1.6 — SOMEIP_ETS_076
:Test Module: ``test_someip_message_format.py``
:Test Function: ``test_tc8_msg_004_unknown_method``
:Requirement: ``comp_req__tc8_conformance__msg_error_codes``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify error handling for requests with invalid method IDs.

**Stimuli:**
Send REQUEST to service ``0x1234``, method ``0xBEEF``.

**Expected Result:**
``E_UNKNOWN_METHOD`` (0x03).

TC8-MSG-005 — Session ID Echo
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.6 — SOMEIPSRV_ONWIRE_03
:Test Module: ``test_someip_message_format.py``
:Test Function: ``test_tc8_msg_005_session_id_echo``
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify RESPONSE echoes the REQUEST session ID.

**Stimuli:**
Send REQUEST with ``session_id = 0x1234``.

**Expected Result:**
RESPONSE has ``session_id == 0x1234``.

TC8-MSG-006 — Wrong Interface Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.6 — SOMEIP_ETS_074
:Test Module: ``test_someip_message_format.py``
:Test Function: ``test_tc8_msg_006_wrong_interface_version``
:Requirement: ``comp_req__tc8_conformance__msg_error_codes``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify error handling for requests with wrong interface version.

**Stimuli:**
Send REQUEST with ``interface_version = 0xFF``.

**Expected Result:**
``E_WRONG_INTERFACE_VERSION``, ``E_UNKNOWN_METHOD``, or ``E_OK``
(vsomeip behavior varies — all accepted).

TC8-MSG-007 — Malformed Message Handling
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.6 — SOMEIP_ETS_054, 055, 058, 078
:Test Module: ``test_someip_message_format.py``
:Test Functions:
  - ``test_tc8_msg_007_truncated_message_no_crash``
  - ``test_tc8_msg_007_wrong_protocol_version_no_crash``
  - ``test_tc8_msg_007_oversized_length_field_no_crash``
:Requirement: ``comp_req__tc8_conformance__msg_malformed``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that DUT does not crash when receiving malformed messages.

**Stimuli:**

- Truncated message (7 bytes, below 8-byte minimum).
- Message with ``protocol_version = 0xFF``.
- Message with length field claiming 0x7FF3 bytes (actual payload = 16 bytes).

**Expected Result:**
DUT process remains alive (``poll() is None``) after each malformed message.

TC8-MSG-008 — Client ID Echo
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.6 — SOMEIPSRV_ONWIRE_03
:Test Module: ``test_someip_message_format.py``
:Test Function: ``test_tc8_msg_008_client_id_echo``
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify RESPONSE echoes the REQUEST client ID.

**Stimuli:**
Send REQUEST with ``client_id = 0x0011``.

**Expected Result:**
RESPONSE has ``client_id == 0x0011``.

Event Notification Tests
------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

TC8-EVT-001 — Notification Message Type
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.5 — SOMEIPSRV_BASIC_03; §5.1.6 — SOMEIP_ETS_147
:Test Module: ``test_event_notification.py``
:Test Function: ``test_tc8_evt_001_notification_message_type``
:Requirement: ``comp_req__tc8_conformance__evt_subscription``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that event notifications have ``message_type = NOTIFICATION (0x02)``.

**Preconditions:**

- DUT offering service, subscription acknowledged.

**Stimuli:**
Subscribe to eventgroup ``0x4455`` and wait for notification.

**Expected Result:**
Notification received with ``message_type == 0x02``.

TC8-EVT-002 — Correct Event ID
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.5 — SOMEIPSRV_BASIC_03
:Test Module: ``test_event_notification.py``
:Test Function: ``test_tc8_evt_002_correct_event_id``
:Requirement: ``comp_req__tc8_conformance__evt_subscription``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that notification ``method_id`` field carries the correct event ID.

**Stimuli:**
Subscribe and capture notification.

**Expected Result:**
``method_id == 0x0777`` (configured event ID).

TC8-EVT-003 — Notification Only to Subscriber
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.6 — SOMEIP_ETS_147
:Test Module: ``test_event_notification.py``
:Test Function: ``test_tc8_evt_003_notification_only_to_subscriber``
:Requirement: ``comp_req__tc8_conformance__evt_subscription``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that only the subscribed endpoint receives notifications.

**Stimuli:**
Open two sockets: subscribe on one, leave the other unsubscribed.

**Expected Result:**
Subscribed socket receives notification; unsubscribed socket receives nothing.

TC8-EVT-004 — No Notification Before Subscribe
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.6 — SOMEIP_ETS_147 (pre-subscribe)
:Test Module: ``test_event_notification.py``
:Test Function: ``test_tc8_evt_004_no_notification_before_subscribe``
:Requirement: ``comp_req__tc8_conformance__evt_subscription``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that no notifications arrive before subscribing.

**Stimuli:**
Open a socket without subscribing, listen for 3 seconds.

**Expected Result:**
No notifications received.

TC8-EVT-005 — Multicast Notification Delivery
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.6 — SOMEIP_ETS_150
:Test Module: ``test_event_notification.py``
:Test Function: ``test_tc8_evt_005_multicast_notification_delivery``
:Requirement: ``comp_req__tc8_conformance__evt_subscription``
:DUT Config: ``tc8_someipd_service.json``
:Marker: ``@pytest.mark.network`` (requires non-loopback NIC)

**Purpose:**
Verify that notifications for a multicast eventgroup arrive on the
multicast address.

**Preconditions:**

- Multicast group ``239.0.0.1:40490`` joinable.
- Eventgroup ``0x4465`` configured with multicast.

**Stimuli:**
Subscribe to eventgroup ``0x4465`` and listen on multicast socket.

**Expected Result:**
NOTIFICATION received on ``239.0.0.1:40490``.

TC8-EVT-006 — StopSubscribe Ceases Notifications
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.6 — SOMEIP_ETS_108
:Test Module: ``test_event_notification.py``
:Test Function: ``test_tc8_evt_006_stop_subscribe_ceases_notifications``
:Requirement: ``comp_req__tc8_conformance__evt_subscription``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that notifications stop after StopSubscribeEventgroup.

**Preconditions:**

- Active subscription with at least one notification received.

**Stimuli:**
Send SubscribeEventgroup with TTL = 0.

**Expected Result:**
No notifications received within 4 seconds after StopSubscribe.

TC8-EVT-007 — Field Notifies Only on Value Change
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.7 — SOMEIPSRV_RPC_16
:Test Module: ``test_event_notification.py``
:Test Function: ``TestEventNotification::test_rpc_16_field_notifies_only_on_change``
:Requirement: ``comp_req__tc8_conformance__fld_getter_setter``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that a field event notification is sent only when the field value changes,
not on every cyclic trigger.

**Preconditions:**

- DUT offering service with a field configured for on-change notification.
- Active subscription acknowledged.

**Stimuli:**
SET field to value A; observe notifications; SET field to the same value A again;
observe again.

**Expected Result:**
Notification sent after first SET (value changed from initial); no duplicate
notification sent when SET issues the same value a second time.

Field Conformance Tests
-----------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

TC8-FLD-001 — Initial Notification on Subscribe
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.6 — SOMEIP_ETS_121
:Test Module: ``test_field_conformance.py``
:Test Function: ``test_tc8_fld_001_initial_notification_on_subscribe``
:Requirement: ``comp_req__tc8_conformance__fld_initial_value``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that subscribing to a field eventgroup (``is_field: true``)
triggers an immediate NOTIFICATION with the cached value.

**Preconditions:**

- DUT has sent at least one ``notify()`` so vsomeip has a cached value.

**Stimuli:**
Subscribe to eventgroup ``0x4455``.

**Expected Result:**
NOTIFICATION received promptly after subscription acknowledgment.

TC8-FLD-002 — Initial Value Timing
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.6 — SOMEIP_ETS_121
:Test Module: ``test_field_conformance.py``
:Test Function: ``test_tc8_fld_002_is_field_sends_initial_value_within_one_second``
:Requirement: ``comp_req__tc8_conformance__fld_initial_value``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that the initial field notification arrives within 1 second of
subscribe ACK (contrasting with non-field events that only notify on
the next cycle).

**Stimuli:**
Subscribe and measure time to first notification.

**Expected Result:**
Notification received within 1 second.

TC8-FLD-003 — Field Getter
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.7 — SOMEIPSRV_RPC_03; §5.1.6 — SOMEIP_ETS_166
:Test Module: ``test_field_conformance.py``
:Test Function: ``test_tc8_fld_003_getter_returns_current_value``
:Requirement: ``comp_req__tc8_conformance__fld_get_set``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that a GET request (method ``0x0001``) returns the current
field value.

**Stimuli:**
Send REQUEST to method ``0x0001``.

**Expected Result:**
RESPONSE with ``return_code = E_OK`` and non-empty payload.

TC8-FLD-004 — Field Setter and Notify
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.7 — SOMEIPSRV_RPC_11; §5.1.6 — SOMEIP_ETS_166
:Test Module: ``test_field_conformance.py``
:Test Function: ``test_tc8_fld_004_setter_updates_value_and_notifies``
:Requirement: ``comp_req__tc8_conformance__fld_get_set``
:DUT Config: ``tc8_someipd_service.json``

**Purpose:**
Verify that a SET request (method ``0x0002``) updates the field value
and notifies all active subscribers with the new value.

**Preconditions:**

- Active subscription to eventgroup ``0x4455``.
- Initial field notification drained.

**Stimuli:**
Send REQUEST to method ``0x0002`` with payload ``0xCAFE``.

**Expected Result:**

- RESPONSE with ``return_code = E_OK``.
- NOTIFICATION received with payload matching ``0xCAFE``.

TCP Transport Binding Tests
----------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

TC8-TCP-001 — TCP Request/Response
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: SOMEIPSRV_RPC_01 §5.1.5.7
:Test Module: ``test_someip_message_format.py``
:Test Function: ``test_tc8_rpc_01_tcp_request_response``
:Requirement ID: ``comp_req__tc8_conformance__tcp_transport``
:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

**Purpose:**
Verify that the DUT accepts a TCP REQUEST and returns a RESPONSE on
the same TCP connection.

**Preconditions:**

- DUT started with ``--tc8-standalone`` flag.
- Service ``0x1234`` offered and SD OfferService received.

**Stimuli:**
Open TCP connection to service port 30510; send a SOME/IP REQUEST
(method ``0x0421``, ``message_type=0x00``).

**Expected Result:**
DUT sends SOME/IP RESPONSE (``message_type=0x80``,
``return_code=E_OK=0x00``) on the same TCP connection.

TC8-TCP-002 — TCP Session ID Echo
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: SOMEIPSRV_RPC_01 §5.1.5.7
:Test Module: ``test_someip_message_format.py``
:Test Function: ``test_tc8_rpc_01_tcp_session_id_echo``
:Requirement ID: ``comp_req__tc8_conformance__tcp_transport``
:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

**Purpose:**
Verify that the DUT echoes the ``session_id`` from a TCP REQUEST in
the TCP RESPONSE.

**Preconditions:**

- DUT started, service offered.

**Stimuli:**
Send TCP REQUEST with a known ``session_id`` value.

**Expected Result:**
TCP RESPONSE carries the same ``session_id`` value.

TC8-TCP-003 — TCP Client ID Echo
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: SOMEIPSRV_RPC_01 §5.1.5.7
:Test Module: ``test_someip_message_format.py``
:Test Function: ``test_tc8_rpc_01_tcp_client_id_echo``
:Requirement ID: ``comp_req__tc8_conformance__tcp_transport``
:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

**Purpose:**
Verify that the DUT echoes the ``client_id`` from a TCP REQUEST in
the TCP RESPONSE.

**Preconditions:**

- DUT started, service offered.

**Stimuli:**
Send TCP REQUEST with a known ``client_id`` value.

**Expected Result:**
TCP RESPONSE carries the same ``client_id`` value.

TC8-TCP-004 — Multiple Methods over Single TCP Connection
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: SOMEIPSRV_RPC_02 §5.1.5.7
:Test Module: ``test_someip_message_format.py``
:Test Function: ``test_tc8_rpc_02_tcp_multiple_methods_single_connection``
:Requirement ID: ``comp_req__tc8_conformance__tcp_transport``
:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

**Purpose:**
Verify that the DUT handles multiple SOME/IP method calls over a
single persistent TCP connection.

**Preconditions:**

- DUT started, service offered.

**Stimuli:**
Open one TCP connection; send two consecutive REQUESTs for different
methods.

**Expected Result:**
Both REQUESTs receive valid RESPONSEs on the same TCP connection
without reconnection.

TC8-TCP-005 — TCP Endpoint Advertised in SD Offer
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: SOMEIPSRV_OPTIONS_15 §5.1.5.2
:Test Module: ``test_someip_message_format.py``
:Test Function: ``test_tc8_sd_options_15_tcp_endpoint_advertised``
:Requirement ID: ``comp_req__tc8_conformance__tcp_transport``
:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

**Purpose:**
Verify that the DUT includes a TCP IPv4EndpointOption
(``L4-Proto=0x06``) in SD OfferService messages.

**Preconditions:**

- DUT starting up; multicast socket opened before DUT launch to
  capture initial SD traffic.

**Stimuli:**
Capture SD multicast traffic on DUT startup.

**Expected Result:**
SD OfferService contains an IPv4EndpointOption with
``L4-Proto=0x06`` (TCP) in addition to the UDP endpoint option.

TC8-TCP-006 — TCP Field Getter (partial SOMEIPSRV_RPC_17)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: SOMEIPSRV_RPC_17 §5.1.5.7 (partial coverage — see note)
:Test Module: ``test_field_conformance.py``
:Test Function: ``test_tc8_rpc_17_tcp_field_getter``
:Requirement ID: ``comp_req__tc8_conformance__tcp_transport``
:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

**Purpose:**
Verify that the DUT responds to a field GET request (method ``0x0001``)
over TCP with ``E_OK`` and the current field value.

**Preconditions:**

- DUT started, service offered.

**Stimuli:**
Open TCP connection to port 30510; send SOME/IP REQUEST for method
``0x0001``.

**Expected Result:**
DUT returns RESPONSE with ``return_code=E_OK`` and payload containing
the current field value.

TC8-TCP-007 — TCP Field Setter (partial SOMEIPSRV_RPC_17)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: SOMEIPSRV_RPC_17 §5.1.5.7 (partial coverage — see note)
:Test Module: ``test_field_conformance.py``
:Test Function: ``test_tc8_rpc_17_tcp_field_setter``
:Requirement ID: ``comp_req__tc8_conformance__tcp_transport``
:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

**Purpose:**
Verify that the DUT processes a field SET request (method ``0x0002``)
over TCP and updates the field value.

**Preconditions:**

- DUT started, service offered.

**Stimuli:**
Open TCP connection; send SET REQUEST with new field value; then send
GET REQUEST to confirm.

**Expected Result:**
SET RESPONSE with ``E_OK``; subsequent GET confirms the updated value.

TC8-TCP-008 — TCP Event Notification Delivery (partial SOMEIPSRV_RPC_17)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: SOMEIPSRV_RPC_17 §5.1.5.7 (partial coverage — see note)
:Test Module: ``test_event_notification.py``
:Test Function: ``test_tc8_rpc_17_tcp_event_notification_delivery``
:Requirement ID: ``comp_req__tc8_conformance__tcp_transport``
:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

**Purpose:**
Verify that the DUT delivers SOME/IP NOTIFICATION messages over TCP
to a subscribed client.

**Preconditions:**

- DUT started, service offered.

**Stimuli:**
Subscribe to eventgroup ``0x4475`` (TCP); open TCP connection to port
30510.

**Expected Result:**
DUT sends NOTIFICATION messages (``message_type=0x02``,
``event_id=0x0778``) over the TCP connection.

TC8-TCP-009: Unaligned SOME/IP Messages over TCP (SOMEIP_ETS_068)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: SOMEIP_ETS_068 (PRS_SOMEIP_00142, PRS_SOMEIP_00569)
:Test Module: ``test_someip_message_format``
:Test Function: ``test_tc8_ets_068_unaligned_someip_messages_over_tcp``
:Requirement ID: ``comp_req__tc8_conformance__tcp_transport``
:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

**Purpose**
  Verify that someipd parses a TCP byte stream containing multiple SOME/IP messages
  packed into a single TCP segment, including the case where one message starts at
  a non-4-byte-aligned byte offset.

**Preconditions**
  - someipd running in ``--tc8-standalone`` mode with ``tc8_someipd_service.json``.
  - TCP port ``DUT_RELIABLE_PORT`` (30510) accepting connections.

**Stimuli**
  Connect via TCP; send three concatenated SOME/IP REQUEST messages (session IDs
  0x0071, 0x0072, 0x0073) as a **single** ``sendall()`` call.  Message sizes are
  16 / 18 / 16 bytes so the third starts at offset 34 (not 4-byte aligned).

**Expected Result**
  Receive three SOME/IP RESPONSE messages with ``message_type = RESPONSE (0x80)``,
  ``service_id = 0x1234``, and session IDs ``{0x0071, 0x0072, 0x0073}`` (any order).

.. note::

   **SOMEIPSRV_RPC_17 partial coverage:** TC8-TCP-006, TC8-TCP-007,
   and TC8-TCP-008 verify TCP transport for field GET/SET and event
   notification operations using a single service instance.  The full
   SOMEIPSRV_RPC_17 requirement (each service instance on a separate
   TCP connection) is not covered by the current implementation.  The
   multi-instance TCP scenario is a known gap that will be addressed
   when multi-instance vsomeip configurations are available.

TC8-UDP-001: Unaligned SOME/IP Messages over UDP (SOMEIP_ETS_069)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: SOMEIP_ETS_069 (PRS_SOMEIP_00142, PRS_SOMEIP_00569)
:Test Module: ``test_someip_message_format``
:Test Function: ``test_tc8_ets_069_unaligned_someip_messages_over_udp``
:Requirement ID: ``comp_req__tc8_conformance__udp_transport``
:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``

**Purpose**
  Verify that someipd parses a UDP datagram containing multiple SOME/IP messages
  where one message starts at a non-4-byte-aligned byte offset within the datagram.

**Preconditions**
  - someipd running in ``--tc8-standalone`` mode with ``tc8_someipd_service.json``.
  - UDP port ``DUT_UNRELIABLE_PORT`` (30502 in ``tc8_message_format`` target) accepting requests.

**Stimuli**
  Create a UDP socket bound to an ephemeral port; send three concatenated SOME/IP
  REQUEST messages (session IDs 0x0081, 0x0082, 0x0083) as a **single** ``sendto()``
  call.  Message sizes are 16 / 18 / 16 bytes so the third starts at offset 34
  (not 4-byte aligned).

**Expected Result**
  Receive three SOME/IP RESPONSE messages with ``message_type = RESPONSE (0x80)``,
  ``service_id = 0x1234``, and session IDs ``{0x0081, 0x0082, 0x0083}`` (any order).
  Each response arrives as a separate UDP datagram.

Multi-service and Multi-instance Tests
---------------------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_multi.json``

TC8-MULTI-001 — Multi-service Config Loads and Primary Service Offered
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.7 — SOMEIPSRV_RPC_13
:Test Module: ``test_multi_service``
:Test Function: ``TestMultiServiceInstanceRouting::test_rpc_13_multi_service_config_loads_and_primary_service_offered``
:Requirement: ``comp_req__tc8_conformance__multi_service``
:DUT Config: ``tc8_someipd_multi.json``

**Purpose**
  Verify that ``someipd`` accepts a vsomeip configuration that declares two
  service entries and that the primary service (0x1234/0x5678) is correctly
  offered via SD OfferService after startup.

**Preconditions**

  - ``someipd`` started with ``--tc8-standalone`` and ``tc8_someipd_multi.json``
  - ``tc8_someipd_multi.json`` declares two service entries (0x1234/0x5678 and
    0x5678/0x0001)
  - Multicast route available (224.0.0.0/4)

**Stimuli**
  Passive observation of SD OfferService multicast messages.

**Expected Result**
  - The ``someipd`` process remains alive after startup (multi-service config
    parsed without crash)
  - An SD OfferService entry for service_id=0x1234, instance_id=0x5678 is
    received within 10 seconds
  - The OfferService entry has TTL > 0

TC8-MULTI-002 — Service Instance UDP Port Advertisement and Isolation
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Spec Reference: §5.1.5.7 — SOMEIPSRV_RPC_14
:Test Module: ``test_multi_service``
:Test Functions: ``TestMultiServiceInstanceRouting::test_rpc_14_service_a_advertises_configured_udp_port``,
  ``TestMultiServiceInstanceRouting::test_rpc_14_no_unexpected_service_ids_in_offers``
:Requirement: ``comp_req__tc8_conformance__multi_service``
:DUT Config: ``tc8_someipd_multi.json``

**Purpose**
  Verify that the offered service advertises the UDP port from its ``unreliable``
  config field (TC8_SVC_PORT) in the SD IPv4EndpointOption, and that no
  unexpected service IDs appear in SD traffic.

**Preconditions**

  - Same as TC8-MULTI-001.

**Stimuli**
  Passive observation of SD OfferService for a 6-second window; extraction of
  IPv4EndpointOption from each OfferService entry.

**Expected Result**
  - Service 0x1234 OfferService carries an IPv4 UDP endpoint option with
    ``port == DUT_UNRELIABLE_PORT`` (TC8_SVC_PORT = 30512)
  - All observed service IDs are in the set {0x1234, 0x5678} (configured services)
  - No phantom service IDs are present

SD Format and Options Compliance Tests
---------------------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_sd_format_compliance``

TC8-SDF-001 — SD SOME/IP Header: Client ID = 0x0000
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_01
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsOfferService::test_format_01_client_id_is_zero``

**Purpose:**
Verify the Client-ID field in SOME/IP SD OfferService messages is always 0x0000.

**Preconditions:**
``someipd`` started in standalone mode; multicast capture socket open before launch.

**Stimulus:**
Passive capture of the initial multicast OfferService.

**Expected Result:**
``sd_hdr.client_id == 0x0000``.

TC8-SDF-002 — SD SOME/IP Header: Session ID non-zero
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_02
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsOfferService::test_format_02_session_id_is_nonzero_and_in_range``

**Purpose:**
Verify the SD Session-ID field is non-zero (never 0x0000) and fits in 16 bits.

**Stimulus:**
Passive capture of the multicast OfferService.

**Expected Result:**
``sd_hdr.session_id != 0x0000`` and ``sd_hdr.session_id <= 0xFFFF``.

TC8-SDF-003 — SD SOME/IP Header: Interface Version = 0x01
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_04
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsOfferService::test_format_04_interface_version_is_one``

**Purpose:**
Verify the SOME/IP interface_version field in SD messages equals 0x01.

**Stimulus:**
Passive capture of the multicast OfferService.

**Expected Result:**
``sd_hdr.interface_version == 0x01``.

TC8-SDF-004 — SD SOME/IP Header: Message Type = NOTIFICATION
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_05
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsOfferService::test_format_05_message_type_is_notification``

**Purpose:**
Verify the SOME/IP message_type field in SD messages equals 0x02 (NOTIFICATION).

**Stimulus:**
Passive capture of the multicast OfferService.

**Expected Result:**
``sd_hdr.message_type == SOMEIPMessageType.NOTIFICATION``.

TC8-SDF-005 — SD SOME/IP Header: Return Code = E_OK
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_06
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsOfferService::test_format_06_return_code_is_e_ok``

**Purpose:**
Verify the SOME/IP return_code field in SD messages equals 0x00 (E_OK).

**Stimulus:**
Passive capture of the multicast OfferService.

**Expected Result:**
``sd_hdr.return_code == SOMEIPReturnCode.E_OK``.

TC8-SDF-006 — SD Flags: Reserved Bits are Zero
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_09
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsOfferService::test_format_09_sd_flags_reserved_bits_are_zero``

**Purpose:**
Verify that bits 5-0 of the SD flags byte (reserved/undefined) are all zero.

**Stimulus:**
Passive capture of the multicast OfferService.

**Expected Result:**
``sd_hdr.flags_unknown == 0``.

TC8-SDF-007 — SD Entry: Reserved Byte is Zero
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_10
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsOfferService::test_format_10_sd_entry_reserved_bytes_are_zero``

**Purpose:**
Verify that byte [2] (index_second_option_run, reserved for Type-1 entries) in the
16-byte OfferService SD entry is zero.

**Stimulus:**
Passive capture and raw byte inspection of the OfferService entry.

**Expected Result:**
``entry_bytes[2] == 0``.

TC8-SDF-008 — SD Entry: Length is 16 Bytes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_11
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdOfferEntryFields::test_format_11_entry_is_16_bytes``

**Purpose:**
Verify that each SD entry serialises to exactly 16 bytes.

**Stimulus:**
Passive capture; entry built and measured.

**Expected Result:**
``len(entry.build()) == 16``.

TC8-SDF-009 — SD Entry: First Option Run Index is Zero
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_12
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdOfferEntryFields::test_format_12_first_option_run_index_is_zero``

**Purpose:**
Verify that byte [1] (option_index_1) in the OfferService entry is 0x00.

**Stimulus:**
Passive capture; raw byte inspection.

**Expected Result:**
``entry_bytes[1] == 0``.

TC8-SDF-010 — SD Entry: num_options_1 Matches Resolved Options
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_13
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdOfferEntryFields::test_format_13_num_options_matches_options_list``

**Purpose:**
Verify that the num_options_1 counter field equals the actual number of options
associated with the entry.

**Stimulus:**
Passive capture; compare raw entry counter with resolved options list length.

**Expected Result:**
``raw_entry.num_options_1 == len(entry.options_1)``.

TC8-SDF-011 — SD Entry: Instance ID Matches Config
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_15
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdOfferEntryFields::test_format_15_instance_id_matches_config``

**Purpose:**
Verify that the OfferService entry instance_id matches the configured value (0x5678).

**Stimulus:**
Passive capture.

**Expected Result:**
``entry.instance_id == 0x5678``.

TC8-SDF-012 — SD Entry: Major Version Matches Config
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_16
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdOfferEntryFields::test_format_16_major_version_matches_config``

**Purpose:**
Verify that the OfferService entry major_version matches the configured value (0x00).

**Stimulus:**
Passive capture.

**Expected Result:**
``entry.major_version == 0x00``.

TC8-SDF-013 — SD Entry: Minor Version Matches Config
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_18
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdOfferEntryFields::test_format_18_minor_version_matches_config``

**Purpose:**
Verify that the OfferService entry minor_version matches the configured value (0x00000000).

**Stimulus:**
Passive capture.

**Expected Result:**
``entry.service_minor_version == 0x00000000``.

TC8-SDF-014 — SubscribeAck Entry Type = 0x06
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_19
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsSubscribeAck::test_format_19_ack_entry_type_is_subscribe_ack``

**Purpose:**
Verify that the SubscribeEventgroupAck SD entry type equals 0x07 (SubscribeAck).

**Preconditions:**
``someipd`` offering eventgroup 0x4455.

**Stimulus:**
Send SubscribeEventgroup and capture the Ack.

**Expected Result:**
``ack.sd_type == SOMEIPSDEntryType.SubscribeAck``.

TC8-SDF-015 — SubscribeAck Entry: 16 Bytes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_20
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsSubscribeAck::test_format_20_ack_entry_is_16_bytes``

**Purpose:**
Verify that the SubscribeEventgroupAck entry serialises to exactly 16 bytes.

**Stimulus:**
Send SubscribeEventgroup; capture Ack; measure serialised length.

**Expected Result:**
``len(ack.build()) == 16``.

TC8-SDF-016 — SubscribeAck: Option Run Index Correct
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_21
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsSubscribeAck::test_format_21_ack_option_run_index_is_zero_when_no_options``

**Purpose:**
Verify that the SubscribeAck option run index is 0x00 when no options are attached,
or a valid index (< 16) when options are present.

**Stimulus:**
Send SubscribeEventgroup for UDP unicast eventgroup (no multicast option expected).

**Expected Result:**
``option_index_1 == 0`` when ``num_options_1 == 0``.

TC8-SDF-017 — SubscribeAck: Service ID Matches
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_23
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsSubscribeAck::test_format_23_ack_service_id_matches_config``

**Purpose:**
Verify that the SubscribeAck entry carries the same service_id as the subscribe request.

**Stimulus:**
Send SubscribeEventgroup for service 0x1234.

**Expected Result:**
``ack.service_id == 0x1234``.

TC8-SDF-018 — SubscribeAck: Instance ID Matches
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_24
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsSubscribeAck::test_format_24_ack_instance_id_matches_config``

**Purpose:**
Verify that the SubscribeAck entry carries the same instance_id as the subscribe request.

**Stimulus:**
Send SubscribeEventgroup for instance 0x5678.

**Expected Result:**
``ack.instance_id == 0x5678``.

TC8-SDF-019 — SubscribeAck: Major Version Matches
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_25
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsSubscribeAck::test_format_25_ack_major_version_matches_config``

**Purpose:**
Verify that the SubscribeAck entry major_version matches the service definition.

**Stimulus:**
Send SubscribeEventgroup with major_version = 0x00.

**Expected Result:**
``ack.major_version == 0x00``.

TC8-SDF-020 — SubscribeAck: TTL > 0
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_26
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsSubscribeAck::test_format_26_ack_ttl_is_nonzero``

**Purpose:**
Verify that a positive SubscribeEventgroupAck has TTL > 0 (TTL = 0 means Nack).

**Stimulus:**
Send valid SubscribeEventgroup.

**Expected Result:**
``ack.ttl > 0``.

TC8-SDF-021 — SubscribeAck: Reserved Field = 0
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_27
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsSubscribeAck::test_format_27_ack_reserved_field_is_zero``

**Purpose:**
Verify that the high 16 bits of the SubscribeAck minver_or_counter field (the reserved
counter portion) are all zero.

**Stimulus:**
Send SubscribeEventgroup; inspect raw ack entry fields.

**Expected Result:**
``(ack.minver_or_counter >> 16) & 0xFFFF == 0``.

TC8-SDF-022 — SubscribeAck: Eventgroup ID Matches
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.1 — SOMEIPSRV_FORMAT_28
:Requirement: ``comp_req__tc8_conformance__sd_format_fields``
:Test Function: ``TestSdHeaderFieldsSubscribeAck::test_format_28_ack_eventgroup_id_matches_subscribe``

**Purpose:**
Verify that the SubscribeAck carries the same eventgroup_id as the subscribe request.

**Stimulus:**
Send SubscribeEventgroup for eventgroup 0x4455.

**Expected Result:**
``(ack.minver_or_counter & 0xFFFF) == 0x4455``.

TC8-SDF-023 — Endpoint Option: Length = 0x0009
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_01
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Test Function: ``TestSdOptionsEndpoint::test_options_01_endpoint_option_length_is_nine``

**Purpose:**
Verify that the IPv4EndpointOption length field in SD OfferService messages equals 0x0009.

**Stimulus:**
Passive capture; extract first IPv4EndpointOption from the OfferService entry; read raw bytes.

**Expected Result:**
``length_field == 0x0009``.

TC8-SDF-024 — Endpoint Option: Type = 0x04
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_02
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Test Function: ``TestSdOptionsEndpoint::test_options_02_endpoint_option_type_is_0x04``

**Purpose:**
Verify that the IPv4EndpointOption type byte equals 0x04.

**Stimulus:**
Passive capture; inspect raw option byte [2].

**Expected Result:**
``type_byte == 0x04``.

TC8-SDF-025 — Endpoint Option: Reserved Byte After Type = 0x00
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_03
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Test Function: ``TestSdOptionsEndpoint::test_options_03_endpoint_option_reserved_after_type_is_zero``

**Purpose:**
Verify that the reserved byte at option offset [3] (after the type byte) equals 0x00.

**Stimulus:**
Passive capture; inspect raw option byte [3].

**Expected Result:**
``reserved_byte == 0x00``.

TC8-SDF-026 — Endpoint Option: Reserved Before Protocol = 0x00
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_05
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Test Function: ``TestSdOptionsEndpoint::test_options_05_endpoint_option_reserved_before_protocol_is_zero``

**Purpose:**
Verify that the reserved byte at option offset [8] (before the L4 protocol byte) equals 0x00.

**Stimulus:**
Passive capture; inspect raw option byte [8].

**Expected Result:**
``reserved_byte == 0x00``.

TC8-SDF-027 — Endpoint Option: L4 Protocol = 0x11 (UDP)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_06
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Test Function: ``TestSdOptionsEndpoint::test_options_06_endpoint_option_protocol_is_udp``

**Purpose:**
Verify that the IPv4EndpointOption L4 protocol field equals 0x11 (UDP).

**Stimulus:**
Passive capture; read ``opt.l4proto``.

**Expected Result:**
``opt.l4proto == L4Protocols.UDP``.

TC8-SDF-028 — Multicast Option: Length = 0x0009
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_08
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Marker: ``@pytest.mark.network`` (requires non-loopback NIC)
:Test Function: ``TestSdOptionsMulticast::test_options_08_multicast_option_length_is_nine``

**Purpose:**
Verify that the IPv4MulticastOption in SubscribeEventgroupAck has length field 0x0009.

**Preconditions:**
Non-loopback NIC (TC8_HOST_IP set); eventgroup 0x4465 configured with multicast address.

**Stimulus:**
Send SubscribeEventgroup for eventgroup 0x4465; capture Ack and extract multicast option.

**Expected Result:**
``length_field == 0x0009``.

TC8-SDF-029 — Multicast Option: Type = 0x14
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_09
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Marker: ``@pytest.mark.network`` (requires non-loopback NIC)
:Test Function: ``TestSdOptionsMulticast::test_options_09_multicast_option_type_is_0x14``

**Purpose:**
Verify that the IPv4MulticastOption type byte equals 0x14 (decimal 20).

**Stimulus:**
Same as TC8-SDF-028.

**Expected Result:**
``type_byte == 0x14``.

TC8-SDF-030 — Multicast Option: Reserved = 0x00
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_10
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Marker: ``@pytest.mark.network`` (requires non-loopback NIC)
:Test Function: ``TestSdOptionsMulticast::test_options_10_multicast_option_reserved_is_zero``

**Purpose:**
Verify that the reserved byte at offset [3] of the IPv4MulticastOption equals 0x00.

**Stimulus:**
Same as TC8-SDF-028.

**Expected Result:**
``reserved_byte == 0x00``.

TC8-SDF-031 — Multicast Option: Address Matches Config
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_11
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Marker: ``@pytest.mark.network`` (requires non-loopback NIC)
:Test Function: ``TestSdOptionsMulticast::test_options_11_multicast_address_matches_config``

**Purpose:**
Verify that the IPv4MulticastOption address field equals the configured multicast
group address (``239.0.0.1``).

**Stimulus:**
Same as TC8-SDF-028.

**Expected Result:**
``opt.address == IPv4Address("239.0.0.1")``.

TC8-SDF-032 — Multicast Option: Reserved Before Port = 0x00
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_12
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Marker: ``@pytest.mark.network`` (requires non-loopback NIC)
:Test Function: ``TestSdOptionsMulticast::test_options_12_multicast_option_reserved_before_port_is_zero``

**Purpose:**
Verify that the reserved byte at option offset [8] (before the port field) equals 0x00.

**Stimulus:**
Same as TC8-SDF-028.

**Expected Result:**
``reserved_byte == 0x00``.

TC8-SDF-033 — Multicast Option: L4 Protocol = 0x11 (UDP)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_13
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Marker: ``@pytest.mark.network`` (requires non-loopback NIC)
:Test Function: ``TestSdOptionsMulticast::test_options_13_multicast_option_protocol_is_udp``

**Purpose:**
Verify that the IPv4MulticastOption L4 protocol field equals 0x11 (UDP).

**Stimulus:**
Same as TC8-SDF-028.

**Expected Result:**
``opt.l4proto == L4Protocols.UDP``.

TC8-SDF-034 — Multicast Option: Port Matches Config
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.2 — SOMEIPSRV_OPTIONS_14
:Requirement: ``comp_req__tc8_conformance__sd_options_fields``
:Marker: ``@pytest.mark.network`` (requires non-loopback NIC)
:Test Function: ``TestSdOptionsMulticast::test_options_14_multicast_port_matches_config``

**Purpose:**
Verify that the IPv4MulticastOption port field equals the configured multicast port
(40490).

**Stimulus:**
Same as TC8-SDF-028.

**Expected Result:**
``opt.port == 40490``.

TC8-SDF-035 — StopSubscribeEventgroup Entry Wire Format
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_12
:Requirement: ``comp_req__tc8_conformance__sd_stop_sub_fmt``
:Test Function: ``TestSdStopSubscribeFormat::test_sd_message_12_stop_subscribe_entry_format``

**Purpose:**
Verify that a StopSubscribeEventgroup SD entry has entry type byte ``0x06``
and TTL field (bytes 9–11) equal to ``0x000000`` at the wire level.

**Preconditions:**
None — pure byte-level format check, no DUT interaction.

**Stimulus:**
Inline construction of a 16-byte SD entry with TTL=0.

**Expected Result:**
``entry[0] == 0x06`` and ``entry[9:12] == b'\x00\x00\x00'``.

SD Entry Semantics Tests
-------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_service_discovery``

TC8-SDM-001 — FindService Wildcard Instance
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_01
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_01_instance_wildcard``

**Purpose:**
Verify that a FindService with instance_id = 0xFFFF (wildcard) elicits an OfferService
for the configured instance.

**Stimulus:**
Send FindService with instance_id = 0xFFFF.

**Expected Result:**
OfferService received with ``instance_id == 0x5678``.

TC8-SDM-002 — FindService Specific Instance
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_02
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_02_instance_specific``

**Purpose:**
Verify that a FindService with the exact instance_id = 0x5678 elicits an OfferService.

**Stimulus:**
Send FindService with instance_id = 0x5678.

**Expected Result:**
OfferService received for service_id = 0x1234, instance_id = 0x5678.

TC8-SDM-003 — FindService Wildcard Major Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_03
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_03_major_version_wildcard``

**Purpose:**
Verify that a FindService with major_version = 0xFF (any) elicits an OfferService.

**Stimulus:**
Send FindService with major_version = 0xFF.

**Expected Result:**
OfferService received for the configured service.

TC8-SDM-004 — FindService Specific Major Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_04
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_04_major_version_specific``

**Purpose:**
Verify that a FindService with the exact major_version = 0x00 elicits an OfferService.

**Stimulus:**
Send FindService with major_version = 0x00.

**Expected Result:**
OfferService received for the configured service.

TC8-SDM-005 — FindService Wildcard Minor Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_05
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_05_minor_version_wildcard``

**Purpose:**
Verify that a FindService with minor_version = 0xFFFFFFFF (wildcard) elicits an
OfferService.

**Stimulus:**
Send FindService with minor_version = 0xFFFFFFFF.

**Expected Result:**
OfferService received for the configured service.

TC8-SDM-006 — FindService Specific Minor Version
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_06
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDVersionMatching::test_sd_message_06_minor_version_specific``

**Purpose:**
Verify that a FindService with minor_version = 0x00000000 (exact) elicits an
OfferService.

**Stimulus:**
Send FindService with minor_version = 0x00000000.

**Expected Result:**
OfferService received for the configured service.

TC8-SDM-007 — SubscribeEventgroup: Wrong Major Version Rejected
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_14
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_14_wrong_major_version``

**Purpose:**
Verify that a SubscribeEventgroup with a non-matching major version is rejected with
NAck (SubscribeEventgroupAck TTL = 0) or silently ignored.

**Stimulus:**
Send SubscribeEventgroup with major_version = 0x7F (not 0x00).

**Expected Result:**
No positive SubscribeAck (TTL > 0) received; DUT remains functional.

TC8-SDM-008 — SubscribeEventgroup: Wrong Service ID Rejected
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_15
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_15_wrong_service_id``

**Purpose:**
Verify that a SubscribeEventgroup for an unknown service is rejected or ignored.

**Stimulus:**
Send SubscribeEventgroup with service_id = 0xBEEF.

**Expected Result:**
No positive SubscribeAck; DUT remains functional.

TC8-SDM-009 — SubscribeEventgroup: Wrong Instance ID Rejected
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_16
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_16_wrong_instance_id``

**Purpose:**
Verify that a SubscribeEventgroup for an unknown instance is rejected or ignored.

**Stimulus:**
Send SubscribeEventgroup with instance_id = 0xBEEF.

**Expected Result:**
No positive SubscribeAck; DUT remains functional.

TC8-SDM-010 — SubscribeEventgroup: Unknown Eventgroup Rejected
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_17
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_17_unknown_eventgroup_id``

**Purpose:**
Verify that a SubscribeEventgroup for an unknown eventgroup ID is rejected with NAck.

**Stimulus:**
Send SubscribeEventgroup with eventgroup_id = 0xBEEF.

**Expected Result:**
SubscribeAck with TTL = 0 (NAck) received.

TC8-SDM-011 — SubscribeEventgroup: TTL = 0 is StopSubscribe
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_18
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeNAck::test_sd_message_18_ttl_zero_stop_subscribe``

**Purpose:**
Verify that a SubscribeEventgroup with TTL = 0 is treated as StopSubscribeEventgroup
and does not elicit a positive ACK.

**Stimulus:**
Subscribe (TTL > 0) then immediately send another SubscribeEventgroup with TTL = 0.

**Expected Result:**
No positive ACK after the TTL = 0 message.

TC8-SDM-012 — SubscribeEventgroup: Reserved Bits Set Rejected
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_19
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

TC8-SDM-013 — FindService Response Timing (Unicast)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.4 — SOMEIPSRV_SD_BEHAVIOR_03
:Requirement: ``comp_req__tc8_conformance__sd_cyclic_timing``
:Test Function: ``TestSDFindServiceTiming::test_sd_behavior_03_unicast_findservice_timing``

**Purpose:**
Verify that a unicast OfferService response to a FindService arrives within the
configured request-response delay.

**Stimulus:**
Send unicast FindService; measure time to first OfferService response.

**Expected Result:**
OfferService received within the configured delay window.

TC8-SDM-014 — FindService Response Timing (Multicast)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.4 — SOMEIPSRV_SD_BEHAVIOR_04
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

TC8-SDLC-001 — Two Simultaneous Subscribes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_088
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_088_two_subscribes_same_session``

**Purpose:**
Verify that two SubscribeEventgroup entries in one SD message each receive a
positive ACK.

**Stimulus:**
Send an SD message containing two SubscribeEventgroup entries.

**Expected Result:**
Two ACK entries received (one per subscription).

TC8-SDLC-002 — TTL = 0 as StopSubscribe (No NAck)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_092
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_092_ttl_zero_stop_subscribe_no_nack``

**Purpose:**
Verify that a SubscribeEventgroup with TTL = 0 stops the subscription without
triggering a NAck from the DUT.

**Stimulus:**
Subscribe then send SubscribeEventgroup TTL = 0.

**Expected Result:**
No NAck (SubscribeAck TTL = 0) received after the StopSubscribe.

TC8-SDLC-003 — Subscribe Without Prior RPC Call
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_098
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_098_subscribe_accepted_without_prior_rpc``

**Purpose:**
Verify that the DUT accepts a SubscribeEventgroup without any prior method call
(no prerequisite RPC interaction required).

**Stimulus:**
Send SubscribeEventgroup immediately after service discovery, without any RPC.

**Expected Result:**
Positive SubscribeAck received.

TC8-SDLC-004 — Non-Standard SD Entry Order Handled
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_107
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_107_find_service_and_subscribe_processed_independently``

**Purpose:**
Verify that the DUT correctly handles SD messages where FindService and
SubscribeEventgroup entries appear in a non-standard order.

**Stimulus:**
Send SD message with FindService followed by SubscribeEventgroup.

**Expected Result:**
Both entries processed; OfferService and SubscribeAck received.

TC8-SDLC-005 — Subscribe Endpoint IP Matches Tester
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_120
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_120_subscribe_endpoint_ip_matches_tester``

**Purpose:**
Verify that the DUT delivers events to the IP address specified in the subscribe
endpoint option, not the IP address the SD message was sourced from.

**Stimulus:**
Send SubscribeEventgroup with endpoint option IP = tester_ip.

**Expected Result:**
Events delivered to the tester_ip address.

TC8-SDLC-006 — SD Interface Version = 0x01
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_122
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_122_sd_interface_version_is_one``

**Purpose:**
Verify that the SD interface_version field in DUT OfferService messages is 0x01.

**Stimulus:**
Passive capture of SD OfferService.

**Expected Result:**
``sd_hdr.interface_version == 0x01`` (same as SOMEIPSRV_FORMAT_04, verified in context
of the lifecycle sequence).

TC8-SDLC-007 — Re-subscribe After StopSubscribe
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_155
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_155_resubscribe_after_stop``

**Purpose:**
Verify that a subscription can be re-established after a StopSubscribeEventgroup
(TTL = 0) and that events resume.

**Stimulus:**
Subscribe, verify events, StopSubscribe, re-Subscribe, verify events resume.

**Expected Result:**
Events received after re-subscription.

TC8-SDLC-008 — Session ID Increments per OfferService
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_091
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_091_session_id_increments``

**Purpose:**
Verify that the SD SOME/IP header session_id increments by 1 between consecutive
OfferService messages.

**Stimulus:**
Capture two consecutive OfferService messages.

**Expected Result:**
``session_id[n+1] == session_id[n] + 1`` (with wrap from 0xFFFF to 0x0001).

TC8-SDLC-009 — Initial Event Sent After Subscribe
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_099
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_099_initial_event_sent_after_subscribe``

**Purpose:**
Verify that the DUT sends an initial notification to a new subscriber immediately
after a successful SubscribeEventgroup ACK.

**Stimulus:**
Subscribe to eventgroup; wait for first notification.

**Expected Result:**
NOTIFICATION received promptly after ACK.

TC8-SDLC-010 — Server Does Not Emit FindService
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_100
:Requirement: ``comp_req__tc8_conformance__sd_offer_format``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_100_no_findservice_emitted_by_server``

**Purpose:**
Verify that the DUT (acting as service provider) does not emit FindService entries
in its SD messages during the main phase.

**Stimulus:**
Observe SD traffic for 6 seconds; collect all SD entries from the DUT.

**Expected Result:**
No FindService entries observed in DUT SD traffic.

TC8-SDLC-011 — StopOfferService Ceases Client Events
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_101
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

TC8-SDLC-012 — Multicast FindService: Wildcard Versions
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_128
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_128_multicast_findservice_version_wildcard``

**Purpose:**
Verify that a multicast FindService with wildcard major and minor versions elicits
an OfferService response.

**Stimulus:**
Send multicast FindService with major_version = 0xFF, minor_version = 0xFFFFFFFF.

**Expected Result:**
OfferService received for the configured service.

TC8-SDLC-013 — Multicast FindService: Unicast Flag Clear
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_130
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_130_multicast_findservice_unicast_flag_clear``

**Purpose:**
Verify that a multicast FindService with the unicast flag cleared (0x00) elicits a
multicast OfferService response (not a unicast one).

**Stimulus:**
Send multicast FindService with unicast flag = 0.

**Expected Result:**
Multicast OfferService received.

TC8-SDLC-014 — Client StopSubscribe Ceases Events
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_084
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

TC8-SDLC-015 — Client Reboot: Flag Set After First Restart
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_081
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

TC8-SDLC-016 — Client Reboot: Flag Set After Second Restart
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_082
:Requirement: ``comp_req__tc8_conformance__sd_reboot``
:Test Module: ``test_sd_client``
:Test Function: ``TestSDClientReboot::test_ets_082_reboot_flag_set_after_second_restart``

**Purpose:**
Verify that the reboot flag and session ID reset hold across a second consecutive
restart (not just the first).

**Stimulus:**
Start → drain → stop → start → drain → stop → start → capture first message.

**Expected Result:**
``sd_hdr.flag_reboot == True`` and ``outer.session_id <= 2``.

TC8-SDLC-017 — Reboot Detection on Unicast Channel
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_093
:Requirement: ``comp_req__tc8_conformance__sd_reboot``
:Test Module: ``test_sd_reboot``
:Test Function: ``TestSDReboot::test_ets_093_reboot_on_unicast_channel``

**Purpose:**
Verify that a reboot is detected when the session ID resets on the unicast SD channel
(not only on multicast).

**Stimulus:**
Restart DUT; observe first SD unicast message sent by DUT after restart.

**Expected Result:**
``sd_hdr.flag_reboot == True`` and ``outer.session_id <= 2`` on the unicast channel.

TC8-SDLC-018 — Server Reboot: Session ID Resets
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_094
:Requirement: ``comp_req__tc8_conformance__sd_reboot``
:Test Module: ``test_sd_reboot``
:Test Function: ``TestSDReboot::test_ets_094_server_reboot_session_id_resets``

**Purpose:**
Verify that after a DUT restart the SD session ID starts from 1 (or 2), not from the
pre-restart value.

**Stimulus:**
Capture pre-restart session IDs; restart DUT; capture post-restart session ID.

**Expected Result:**
Post-restart session ID is ≤ 2, regardless of the pre-restart value.

TC8-SDLC-019 — Subscribe TTL Expiry Stops Events
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_095
:Requirement: ``comp_req__tc8_conformance__sd_ttl_expiry``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_095_subscribe_ttl_expires_no_events``

**Purpose:**
Verify that when a subscription's TTL expires without renewal the DUT ceases sending
event notifications to that subscriber.

**Stimulus:**
Subscribe with a short TTL; do not renew; wait for TTL to expire; observe events.

**Expected Result:**
No NOTIFICATION messages received after TTL expiry window.

TC8-SDLC-020 — Initial Event Sent via UDP Unicast
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_105
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_105_initial_event_udp_unicast``

**Purpose:**
Verify that the first event notification after a unicast subscription is sent via
UDP unicast to the subscriber's endpoint.

**Stimulus:**
Subscribe to a unicast eventgroup; capture the first NOTIFICATION after ACK.

**Expected Result:**
First NOTIFICATION is unicast UDP addressed to the tester's endpoint.

TC8-SDLC-021 — SubscribeEventgroup ACK Received
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_106
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_106_subscribe_eventgroup_ack_received``

**Purpose:**
Verify that the DUT sends a SubscribeEventgroupAck (entry type 0x06, TTL > 0) in
response to a valid SubscribeEventgroup.

**Stimulus:**
Send a valid SubscribeEventgroup for the configured service/eventgroup.

**Expected Result:**
SubscribeEventgroupAck received with ``entry_type == 0x06`` and ``TTL > 0``.

TC8-SDLC-022 — Initial Field Event After Subscribe
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_121
:Requirement: ``comp_req__tc8_conformance__fld_initial_value``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_121_initial_field_event_after_subscribe``

**Purpose:**
Verify that after subscribing to a field eventgroup the DUT sends an initial value
notification immediately (within one TTL cycle) without requiring a GET request.

**Stimulus:**
Subscribe to field eventgroup ``0x4455``; observe notifications within 1 second.

**Expected Result:**
At least one NOTIFICATION received within 1 second of subscription ACK.

TC8-SDLC-023 — Unicast Subscribe Receives ACK
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_173
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_173_unicast_subscribe_receives_ack``

**Purpose:**
Verify that the DUT sends a SubscribeEventgroupAck via unicast directly to the
subscriber when a unicast SubscribeEventgroup is received.

**Stimulus:**
Send SubscribeEventgroup via unicast to the DUT SD port.

**Expected Result:**
SubscribeEventgroupAck received unicast at tester's endpoint; ``entry_type == 0x06``.

TC8-SDLC-024 — Last Value Delivered via UDP Multicast
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_104
:Requirement: ``comp_req__tc8_conformance__sd_sub_lifecycle``
:Test Function: ``TestSDSubscribeLifecycleAdvanced::test_ets_104_last_value_udp_multicast``

**Preconditions:**
Requires physical multicast NIC; skipped automatically on loopback.

**Stimulus:**
Subscribe to multicast eventgroup; capture NOTIFICATION on configured multicast group.

**Expected Result:**
NOTIFICATION is received on the eventgroup's multicast address, not unicast.

TC8-SDLC-025 — Multicast FindService Elicits Offer Response
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_127
:Requirement: ``comp_req__tc8_conformance__sd_find_response``
:Test Function: ``TestSDFindServiceAdvanced::test_ets_127_multicast_findservice_response``

**Preconditions:**
Requires physical multicast NIC; skipped automatically on loopback.

**Stimulus:**
Send SD FindService for the configured service via multicast.

**Expected Result:**
DUT responds with OfferService (unicast or multicast) for the known service.

SD Robustness Tests
--------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_sd.json``
:Test Module: ``test_sd_robustness``

All robustness tests follow the pattern: inject one malformed SD packet, then send a
valid FindService and verify the DUT still replies with OfferService (DUT alive
assertion).

TC8-SDROBUST-001 — Empty Entries Array
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_111
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedEntries::test_ets_111_empty_entries_array``

**Stimulus:**
Send SD packet with entries_array_length = 0.

**Expected Result:**
DUT alive (replies to FindService after injection).

TC8-SDROBUST-002 — Zero-Length Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_112/113
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedEntries::test_ets_112_empty_option_zero_length``

**Stimulus:**
Send SubscribeEventgroup with option length = 1 (too short).

**Expected Result:**
DUT alive.

TC8-SDROBUST-003 — Entries Length Field Wrong
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_114
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Functions:
  - ``TestSDMalformedEntries::test_ets_114_entries_length_zero``
  - ``TestSDMalformedEntries::test_ets_114_entries_length_mismatched``

**Stimulus:**
Send SD with entries_array_length = 0 (one entry present) and with
entries_array_length = 8 (not a multiple of 16).

**Expected Result:**
DUT alive after each injection.

TC8-SDROBUST-004 — Entry References More Options Than Exist
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_115
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedEntries::test_ets_115_entry_refs_more_options_than_exist``

**Stimulus:**
Send SubscribeEventgroup with num_options_1 = 3 but only 1 option present.

**Expected Result:**
DUT alive.

TC8-SDROBUST-005 — Entry Unknown Option Type
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_116
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedEntries::test_ets_116_entry_unknown_option_type``

**Stimulus:**
Send SubscribeEventgroup with unknown option type 0x77.

**Expected Result:**
DUT alive.

TC8-SDROBUST-006 — Two Entries Share Same Option Index
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_117
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedEntries::test_ets_117_two_entries_same_option``

**Stimulus:**
Send SD with two entries both referencing option index 0.

**Expected Result:**
DUT alive.

TC8-SDROBUST-007 — FindService With Endpoint Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_118
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedEntries::test_ets_118_find_service_with_endpoint_option``

**Stimulus:**
Send FindService with an unexpected endpoint option attached.

**Expected Result:**
DUT responds to the FindService; DUT alive.

TC8-SDROBUST-008 — Entries Length Wildly Too Large
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_123/124
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedEntries::test_ets_123_entries_length_wildly_too_large``

**Stimulus:**
Send SD with entries_array_length = 0xFFFF (far exceeds actual payload size).

**Expected Result:**
DUT alive.

TC8-SDROBUST-009 — Truncated Entry
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_125
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedEntries::test_ets_125_truncated_entry``

**Stimulus:**
Send SD with entries_array_length = 16 but only 8 bytes of entry data present.

**Expected Result:**
DUT alive.

TC8-SDROBUST-010 — Option Length Much Too Large
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_134
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedOptions::test_ets_134_option_length_much_too_large``

**Stimulus:**
Send SubscribeEventgroup with IPv4EndpointOption length = 0x00FF.

**Expected Result:**
DUT alive.

TC8-SDROBUST-011 — Option Length One Too Large
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_135
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedOptions::test_ets_135_option_length_one_too_large``

**Stimulus:**
Send SubscribeEventgroup with IPv4EndpointOption length = 0x000A (one too large).

**Expected Result:**
DUT alive.

TC8-SDROBUST-012 — Option Length Too Short
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_136
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedOptions::test_ets_136_option_length_too_short``

**Stimulus:**
Send SubscribeEventgroup with IPv4EndpointOption length = 0x0001 (shorter than minimum).

**Expected Result:**
DUT alive.

TC8-SDROBUST-013 — Option Length Unaligned
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_137
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedOptions::test_ets_137_option_length_unaligned``

**Stimulus:**
Send SubscribeEventgroup with IPv4EndpointOption length = 0x000A (unaligned/odd).

**Expected Result:**
DUT alive.

TC8-SDROBUST-014 — Options Array Length Too Large
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_138
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedOptions::test_ets_138_options_array_length_too_large``

**Stimulus:**
Send SubscribeEventgroup with options_array_length = 100 but only 12 bytes of options.

**Expected Result:**
DUT alive.

TC8-SDROBUST-015 — Options Array Length Too Short
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_139
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedOptions::test_ets_139_options_array_length_too_short``

**Stimulus:**
Send SubscribeEventgroup with options_array_length = 2 but 12 bytes of options present.

**Expected Result:**
DUT alive.

TC8-SDROBUST-016 — Unknown Option Type 0x77
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_174
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedOptions::test_ets_174_unknown_option_type_0x77``

**Stimulus:**
Send SD with option type 0x77 (reserved/unknown).

**Expected Result:**
DUT alive.

TC8-SDROBUST-017 — Subscribe With No Endpoint Option
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_109
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDSubscribeEdgeCases::test_ets_109_subscribe_no_endpoint_option``

**Stimulus:**
Send SubscribeEventgroup with num_options_1 = 0 (no endpoint option).

**Expected Result:**
NAck or silent discard; DUT alive.

TC8-SDROBUST-018 — Subscribe With Zero IP Endpoint
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_110
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDSubscribeEdgeCases::test_ets_110_subscribe_endpoint_ip_zero``

**Stimulus:**
Send SubscribeEventgroup with endpoint IP = 0.0.0.0.

**Expected Result:**
NAck or silent discard; DUT alive.

TC8-SDROBUST-019 — Subscribe With Unknown L4 Protocol
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_119
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDSubscribeEdgeCases::test_ets_119_subscribe_unknown_l4proto``

**Stimulus:**
Send SubscribeEventgroup with L4 protocol byte = 0x00 (unknown).

**Expected Result:**
NAck or silent discard; DUT alive.

TC8-SDROBUST-020 — Subscribe Unknown Service ID
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_140
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDSubscribeEdgeCases::test_ets_140_subscribe_unknown_service_id``

**Stimulus:**
Send SubscribeEventgroup for service_id = 0xDEAD (not offered).

**Expected Result:**
No positive SubscribeAck; DUT alive.

TC8-SDROBUST-021 — Subscribe Unknown Instance ID
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_141
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDSubscribeEdgeCases::test_ets_141_subscribe_unknown_instance_id``

**Stimulus:**
Send SubscribeEventgroup for correct service_id but unknown instance_id = 0xBEEF.

**Expected Result:**
No positive SubscribeAck; DUT alive.

TC8-SDROBUST-022 — Subscribe Unknown Eventgroup ID
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_142
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDSubscribeEdgeCases::test_ets_142_subscribe_unknown_eventgroup_id``

**Stimulus:**
Send SubscribeEventgroup for correct service/instance but unknown eventgroup 0xDEAD.

**Expected Result:**
NAck or silent discard; DUT alive.

TC8-SDROBUST-023 — Subscribe All IDs Unknown
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_143
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDSubscribeEdgeCases::test_ets_143_subscribe_all_ids_unknown``

**Stimulus:**
Send SubscribeEventgroup with service_id, instance_id, and eventgroup_id all unknown.

**Expected Result:**
No positive SubscribeAck; DUT alive.

TC8-SDROBUST-024 — Subscribe With Reserved Option Type
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_144
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDSubscribeEdgeCases::test_ets_144_subscribe_reserved_option_type``

**Stimulus:**
Send SubscribeEventgroup with reserved option type 0x20.

**Expected Result:**
NAck or silent discard; DUT alive.

TC8-SDROBUST-025 — Near-Wrap and Maximum Session IDs
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_152
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Functions:
  - ``TestSDMessageFramingErrors::test_ets_152_high_session_id_0xfffe``
  - ``TestSDMessageFramingErrors::test_ets_152_session_id_0xffff``
  - ``TestSDMessageFramingErrors::test_ets_152_session_id_one``

**Stimulus:**
Send FindService with session_id = 0xFFFE, then 0xFFFF, then 0x0001.

**Expected Result:**
DUT alive after each injection.

TC8-SDROBUST-026 — SOME/IP Length Field Mismatch
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_153
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Functions:
  - ``TestSDMessageFramingErrors::test_ets_153_someip_length_too_small``
  - ``TestSDMessageFramingErrors::test_ets_153_someip_length_too_large``

**Stimulus:**
Send SD with SOME/IP length = 8 (smaller than payload) then length = 0x1000 (larger).

**Expected Result:**
DUT alive after each injection.

TC8-SDROBUST-027 — Wrong SOME/IP Service ID in SD Header
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_178
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMessageFramingErrors::test_ets_178_wrong_someip_service_id``

**Stimulus:**
Send SD packet with SOME/IP service_id = 0x1234 (not 0xFFFF).

**Expected Result:**
DUT silently discards (not recognized as SD); DUT alive.

TC8-SDROBUST-028 — SOME/IP Length Field Way Too Long
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_058
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMessageFramingErrors::test_ets_058_someip_length_way_too_long``

**Stimulus:**
Send SD packet where the SOME/IP length field is set to 0xFFFFFFFF (far beyond actual
packet size).

**Expected Result:**
DUT silently discards the oversized-length SD message; DUT alive.

TC8-SDROBUST-029 — Empty Options Array With Subscribe
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_113
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedEntries::test_ets_113_empty_options_array_with_subscribe``

**Stimulus:**
Send SubscribeEventgroup entry where options_array_length = 0 but the entry
references option index 0.

**Expected Result:**
DUT discards or NAcks the subscribe; DUT alive.

TC8-SDROBUST-030 — Entries Length Too Long By Small Margin
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_124
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedEntries::test_ets_124_entries_length_too_long_by_small_margin``

**Stimulus:**
Send SD packet where entries_array_length is exactly 4 bytes larger than the
actual entries data present.

**Expected Result:**
DUT discards the malformed SD message; DUT alive.

TC8-SDROBUST-031 — Subscribe With Non-Routable Endpoint IP
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_154
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDSubscribeEdgeCases::test_ets_154_subscribe_nonroutable_endpoint_ip``

**Stimulus:**
Send SubscribeEventgroup with an IPv4EndpointOption whose IP address is a
non-routable address (e.g., 0.0.0.1).

**Expected Result:**
DUT rejects or ignores the subscribe; DUT alive.

TC8-SDROBUST-032 — Subscribe Endpoint IP Not Matching Subscriber
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_162
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDSubscribeEdgeCases::test_ets_162_subscribe_endpoint_ip_not_subscriber``

**Stimulus:**
Send SubscribeEventgroup where the IPv4EndpointOption IP address differs from the
actual sender IP.

**Expected Result:**
DUT rejects or ignores the subscribe; DUT alive.

TC8-SDROBUST-033 — Subscribe Endpoint IP Is the DUT Address
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_163
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDSubscribeEdgeCases::test_ets_163_subscribe_endpoint_dut_address``

**Stimulus:**
Send SubscribeEventgroup with an IPv4EndpointOption whose IP address is the DUT's
own loopback or service address.

**Expected Result:**
DUT rejects or ignores the subscribe (loopback endpoint invalid); DUT alive.

TC8-SDROBUST-034 — Unreferenced Option Ignored
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_175
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedOptions::test_ets_175_unreferenced_option_ignored``

**Stimulus:**
Send an SD OfferService where the options array contains one option but the
entry's option run has num_options_1 = 0 (entry references no options).

**Expected Result:**
DUT processes the entry normally (unreferenced option ignored); DUT alive.

TC8-SDROBUST-035 — Trailing Data After Options Array
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_176
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedOptions::test_ets_176_trailing_data_after_options_array``

**Stimulus:**
Send SD packet where extra trailing bytes appear after the options array
(options_array_length is correct but UDP payload is longer).

**Expected Result:**
DUT discards or tolerates the trailing data; DUT alive.

TC8-SDROBUST-036 — Trailing Data With Wrong Options Length
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_177
:Requirement: ``comp_req__tc8_conformance__sd_robustness``
:Test Function: ``TestSDMalformedOptions::test_ets_177_trailing_data_wrong_options_length``

**Stimulus:**
Send SD packet where options_array_length is inflated to include trailing garbage
bytes beyond the real options data.

**Expected Result:**
DUT discards the malformed SD message; DUT alive.

SOME/IP Message Protocol Compliance Tests
------------------------------------------

:DUT Config: ``tests/tc8_conformance/config/tc8_someipd_service.json``
:Test Module: ``test_someip_message_format``

TC8-MSG-009 — Valid Service ID Gets Response
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.5 — SOMEIPSRV_BASIC_01
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipBasicIdentifiers::test_basic_01_correct_service_id_gets_response``

**Purpose:**
Verify that a REQUEST to a valid, offered service ID elicits a RESPONSE.

**Stimulus:**
Send REQUEST to service 0x1234, method 0x0421.

**Expected Result:**
RESPONSE received with message_type = 0x80.

TC8-MSG-010 — Unknown Service ID: E_UNKNOWN_SERVICE
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.5 — SOMEIPSRV_BASIC_02
:Requirement: ``comp_req__tc8_conformance__msg_error_codes``
:Test Function: ``TestSomeipBasicIdentifiers::test_basic_02_unknown_service_id_no_response_or_error``

**Purpose:**
Verify that a REQUEST to an unknown service ID is rejected with E_UNKNOWN_SERVICE.

.. note::

   This test is expected to **FAIL** against vsomeip 3.6.1: the stack silently
   drops requests for unknown services rather than responding with E_UNKNOWN_SERVICE.

**Stimulus:**
Send REQUEST to service 0xDEAD (not offered).

**Expected Result:**
ERROR response with return_code = E_UNKNOWN_SERVICE (0x02).

TC8-MSG-011 — Event Method ID: No RESPONSE
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.5 — SOMEIPSRV_BASIC_03
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipBasicIdentifiers::test_basic_03_event_method_id_no_response``

**Purpose:**
Verify that a REQUEST using an event method ID (bit 15 = 1) does not elicit a
RESPONSE (0x80). An ERROR (0x81) response is permitted.

**Stimulus:**
Send REQUEST with method_id = 0x8001 (event ID range).

**Expected Result:**
No RESPONSE (0x80) received within the timeout.

TC8-MSG-012 — Response Source Address Correct
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.6 — SOMEIPSRV_ONWIRE_01
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipResponseFields::test_onwire_01_response_source_address``

**Purpose:**
Verify that RESPONSE messages originate from the DUT's service endpoint address
and port (as advertised in the SD OfferService).

**Stimulus:**
Send REQUEST; observe UDP source address of the RESPONSE.

**Expected Result:**
RESPONSE source IP matches the DUT's service endpoint; source port equals the
advertised unreliable port.

TC8-MSG-013 — Method ID MSB = 0 in RESPONSE
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.6 — SOMEIPSRV_ONWIRE_02
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipResponseFields::test_onwire_02_method_id_msb_zero_in_response``

**Purpose:**
Verify that RESPONSE messages have bit 15 of the method_id field equal to 0.

**Stimulus:**
Send REQUEST; inspect method_id in the RESPONSE.

**Expected Result:**
``(response.method_id & 0x8000) == 0``.

TC8-MSG-014 — Request ID Reuse Across Sequential Requests
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.6 — SOMEIPSRV_ONWIRE_04
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipResponseFields::test_onwire_04_request_id_reuse``

**Purpose:**
Verify that the DUT correctly handles sequential requests that reuse the same
session_id value.

**Stimulus:**
Send two consecutive REQUESTs with the same session_id.

**Expected Result:**
Both receive valid RESPONSEs with the echoed session_id.

TC8-MSG-015 — Interface Version Echoed in RESPONSE
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.6 — SOMEIPSRV_ONWIRE_06
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipResponseFields::test_onwire_06_interface_version_echoed``

**Purpose:**
Verify that the RESPONSE interface_version matches the REQUEST interface_version.

**Stimulus:**
Send REQUEST with interface_version = 0x01.

**Expected Result:**
RESPONSE has interface_version = 0x01.

TC8-MSG-016 — Normal RESPONSE Return Code = E_OK
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.6 — SOMEIPSRV_ONWIRE_11
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipResponseFields::test_onwire_11_normal_response_return_code_ok``

**Purpose:**
Verify that a successful RESPONSE carries return_code = 0x00 (E_OK).

**Stimulus:**
Send valid REQUEST to a known method.

**Expected Result:**
``response.return_code == 0x00``.

TC8-MSG-017 — Message ID Echoed in RESPONSE (RPC_18)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.7 — SOMEIPSRV_RPC_18
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipResponseFields::test_rpc_18_message_id_echoed``

**Purpose:**
Verify that the RESPONSE message_id (service_id + method_id) matches the REQUEST.

**Stimulus:**
Send REQUEST; compare message_id in RESPONSE.

**Expected Result:**
RESPONSE service_id and method_id match the REQUEST.

TC8-MSG-018 — Interface Version Copied From Request (RPC_20)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.7 — SOMEIPSRV_RPC_20
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipResponseFields::test_rpc_20_interface_version_copied_from_request``

**Purpose:**
Verify that the DUT copies the interface_version from the REQUEST into the RESPONSE.

**Stimulus:**
Send REQUEST with a specific interface_version.

**Expected Result:**
RESPONSE interface_version matches the REQUEST value.

TC8-MSG-019 — Fire-and-Forget: No Error Response (RPC_05)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.7 — SOMEIPSRV_RPC_05
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_rpc_05_fire_and_forget_no_error``

**Purpose:**
Verify that REQUEST_NO_RETURN (fire-and-forget) messages do not elicit an error
response.

**Stimulus:**
Send REQUEST_NO_RETURN to a valid method.

**Expected Result:**
No response (ERROR or RESPONSE) received within the timeout.

TC8-MSG-020 — Return Code Upper Bits Zero (RPC_06)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.7 — SOMEIPSRV_RPC_06
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_rpc_06_return_code_upper_bits_zero``

**Purpose:**
Verify that bits 7-5 of the return_code field in RESPONSE messages are zero.

**Stimulus:**
Send valid REQUEST; inspect RESPONSE return_code.

**Expected Result:**
``(response.return_code & 0xE0) == 0``.

TC8-MSG-021 — Inbound Return Code Upper Bits Ignored (RPC_07)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.7 — SOMEIPSRV_RPC_07
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_rpc_07_request_with_return_code_bits_set``

**Purpose:**
Verify that the DUT ignores the two MSBs of the return_code field in inbound
REQUEST messages (still responds normally).

**Stimulus:**
Send REQUEST with return_code = 0xC0 (two MSBs set).

**Expected Result:**
RESPONSE received with E_OK.

TC8-MSG-022 — No Reply for REQUEST With Non-Zero Return Code (RPC_08)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.7 — SOMEIPSRV_RPC_08
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_rpc_08_request_with_error_return_code_no_reply``

**Purpose:**
Verify that the DUT does not reply to a REQUEST that has a non-zero return_code
(per spec, such requests should be silently discarded).

.. note::

   This test is expected to **FAIL** against vsomeip 3.6.1: the stack replies
   to such messages.

**Stimulus:**
Send REQUEST with return_code = 0x01 (non-zero).

**Expected Result:**
No RESPONSE received within the timeout.

TC8-MSG-023 — ERROR Response Has No Payload (RPC_09)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.7 — SOMEIPSRV_RPC_09
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_rpc_09_error_response_no_payload``

**Purpose:**
Verify that ERROR responses carry no payload beyond the 8-byte SOME/IP header.

**Stimulus:**
Trigger an error response (e.g., unknown method).

**Expected Result:**
ERROR response has ``payload == b""`` (zero payload bytes).

TC8-MSG-024 — Fire-and-Forget Reserved Type: No Error (RPC_10)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.5.7 — SOMEIPSRV_RPC_10
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_rpc_10_fire_and_forget_reserved_type_no_error``

**Purpose:**
Verify that a fire-and-forget message sent to a wrong/reserved service ID does not
elicit an error response.

**Stimulus:**
Send REQUEST_NO_RETURN to an unknown service.

**Expected Result:**
No error response received.

TC8-MSG-025 — Burst 10 Sequential Requests (ETS_004)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_004
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_ets_004_burst_10_sequential_requests``

**Purpose:**
Verify that the DUT responds correctly to a burst of 10 sequential REQUEST messages.

**Stimulus:**
Send 10 consecutive REQUESTs.

**Expected Result:**
10 RESPONSE messages received, all with E_OK.

TC8-MSG-026 — Empty Payload Request (ETS_054)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_054
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_ets_054_empty_payload_request``

**Purpose:**
Verify that a REQUEST with an empty payload (length = 8, no payload bytes) is
handled correctly and elicits E_OK.

**Stimulus:**
Send REQUEST with zero-length payload.

**Expected Result:**
RESPONSE with return_code = E_OK.

TC8-MSG-027 — Fire-and-Forget Wrong Service: No Error (ETS_059)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_059
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_ets_059_fire_and_forget_wrong_service_no_error``

**Purpose:**
Verify that a REQUEST_NO_RETURN to an unknown service does not elicit an error
response.

**Stimulus:**
Send REQUEST_NO_RETURN to service_id = 0xDEAD.

**Expected Result:**
No response received.

TC8-MSG-028 — Two Sequential Requests (ETS_061)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_061
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_ets_061_two_sequential_requests``

**Purpose:**
Verify that two sequential REQUESTs each receive a correct RESPONSE.

**Stimulus:**
Send two consecutive REQUESTs with different session_ids.

**Expected Result:**
Two RESPONSEs received with matching session_ids and E_OK.

TC8-MSG-029 — NOTIFICATION as REQUEST Ignored (ETS_075)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_075
:Requirement: ``comp_req__tc8_conformance__msg_malformed``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_ets_075_notification_as_request_ignored``

**Purpose:**
Verify that the DUT ignores a NOTIFICATION message (message_type = 0x02) sent
as if it were a REQUEST (i.e., directed at the service port).

**Stimulus:**
Send SOME/IP message with message_type = 0x02 (NOTIFICATION) to the service port.

**Expected Result:**
No RESPONSE or ERROR received within the timeout.

TC8-MSG-030 — RESPONSE Uses Big-Endian Byte Order (ETS_005)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_005
:Requirement: ``comp_req__tc8_conformance__msg_resp_header``
:Test Function: ``TestSomeipResponseFields::test_ets_005_response_uses_big_endian_byte_order``

**Purpose:**
Verify that all multi-byte fields in a SOME/IP RESPONSE header (length, request_id,
interface_version, etc.) are encoded in big-endian byte order as required by the
SOME/IP protocol specification.

**Stimulus:**
Send REQUEST; capture RESPONSE bytes; inspect raw byte ordering of length and
request_id fields.

**Expected Result:**
Raw bytes of ``length`` and ``request_id`` fields match the big-endian encoding
of the decoded values (``struct.pack(">I", ...)``).

TC8-MSG-031 — Oversized Length Field Does Not Crash DUT (ETS_058)
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

:OA Reference: §5.1.6 — SOMEIP_ETS_058
:Requirement: ``comp_req__tc8_conformance__msg_malformed_handling``
:Test Function: ``TestSomeipFireAndForgetAndErrors::test_ets_058_oversized_length_field_no_crash``

**Purpose:**
Verify that the DUT does not crash or freeze when a SOME/IP message is received
with a length field value that far exceeds the actual UDP payload size.

**Stimulus:**
Send SOME/IP REQUEST where the length header field is set to 0xFFFFFFFF while the
UDP payload is only 16 bytes.

**Expected Result:**
DUT discards the malformed message; a subsequent valid REQUEST receives a correct
RESPONSE (DUT alive).
