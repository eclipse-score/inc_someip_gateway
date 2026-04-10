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

This document provides the single source of truth for tracing
OPEN Alliance TC8 test cases from the external specification through
the project's internal requirements to the implementing test functions.

Source Document
---------------

- **Title:** OA Automotive Ethernet ECU Test Specification — Layers 3–7
- **Version:** v3.0
- **Chapter:** 5 — SOME/IP
- **Publisher:** OPEN Alliance SIG

.. note::

   OA Spec References use test case identifiers from Chapter 5 of the
   OA Automotive Ethernet ECU Test Specification v3.0 (October 2019).
   ``SOMEIPSRV_*`` IDs are from §5.1.5 (SOME/IP Server Tests) and
   ``SOMEIP_ETS_*`` IDs are from §5.1.6 (Enhanced Testability Service Tests).

Full Traceability Matrix
------------------------

The following table links each OA TC8 specification test case to the
project's internal test ID, component requirement, and implementing
Python test function(s).  All requirement IDs use the
``comp_req__tc8_conformance__`` prefix (omitted for brevity).

Service Discovery
^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_08
     - TC8-SD-001
     - ``sd_offer_format``
     - ``test_service_discovery::test_tc8_sd_001_multicast_offer_on_startup``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_14–18
     - TC8-SD-002
     - ``sd_offer_format``
     - ``test_service_discovery::test_tc8_sd_002_offer_entry_format``
   * - §5.1.5.4 — SOMEIPSRV_SD_BEHAVIOR_02
     - TC8-SD-003
     - ``sd_cyclic_timing``
     - ``test_service_discovery::test_tc8_sd_003_cyclic_offer_timing``
   * - §5.1.6 — SOMEIP_ETS_171
     - TC8-SD-004
     - ``sd_find_response``
     - ``test_service_discovery::test_tc8_sd_004_find_known_service_unicast_offer``
   * - §5.1.5.4 — implied by SD_BEHAVIOR_03/04
     - TC8-SD-005
     - ``sd_find_response``
     - ``test_service_discovery::test_tc8_sd_005_find_unknown_service_no_response``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_13
     - TC8-SD-006
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::test_tc8_sd_006_subscribe_valid_eventgroup_ack``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_14; §5.1.6 — SOMEIP_ETS_140
     - TC8-SD-007
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::test_tc8_sd_007_subscribe_unknown_eventgroup_nack``
   * - §5.1.6 — SOMEIP_ETS_108, SOMEIP_ETS_092
     - TC8-SD-008
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::test_tc8_sd_008_stop_subscribe_ceases_notifications``
   * - §5.1.5.4 — SOMEIPSRV_SD_BEHAVIOR_01
     - TC8-SD-009
     - ``sd_phases_timing``
     - ``test_sd_phases_timing::test_tc8_sd_009_repetition_phase_intervals``
   * - §5.1.5.4 — SOMEIPSRV_SD_BEHAVIOR_01
     - TC8-SD-010
     - ``sd_phases_timing``
     - ``test_sd_phases_timing::test_tc8_sd_010_repetition_count_before_main_phase``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_01–07
     - TC8-SD-011
     - ``sd_endpoint_option``
     - ``test_service_discovery::test_tc8_sd_011_offer_ipv4_endpoint_option``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_02, FORMAT_07
     - TC8-SD-012
     - ``sd_reboot``
     - | ``test_sd_reboot::test_tc8_sd_012_reboot_flag_set_after_restart``
       | ``test_sd_reboot::test_tc8_sd_012_session_id_resets_after_restart``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_08–14
     - TC8-SD-013
     - ``sd_mcast_eg``
     - ``test_service_discovery::test_tc8_sd_013_subscribe_ack_has_multicast_option``
   * - §5.1.6 — SOMEIP_ETS_095
     - TC8-SD-014
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::test_tc8_sd_014_ttl_expiry_ceases_notifications``

SOME/IP Message Format
^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - §5.1.5.6 — SOMEIPSRV_ONWIRE_05
     - TC8-MSG-001
     - ``msg_resp_header``
     - ``test_someip_message_format::test_tc8_msg_001_protocol_version``
   * - §5.1.5.6 — SOMEIPSRV_ONWIRE_07
     - TC8-MSG-002
     - ``msg_resp_header``
     - | ``test_someip_message_format::test_tc8_msg_002_message_type_response``
       | ``test_someip_message_format::test_tc8_msg_002_no_response_for_request_no_return``
   * - §5.1.5.6 — SOMEIPSRV_ONWIRE_10; §5.1.6 — SOMEIP_ETS_077
     - TC8-MSG-003
     - ``msg_error_codes``
     - ``test_someip_message_format::test_tc8_msg_003_unknown_service``
   * - §5.1.5.6 — SOMEIPSRV_ONWIRE_12; §5.1.6 — SOMEIP_ETS_076
     - TC8-MSG-004
     - ``msg_error_codes``
     - ``test_someip_message_format::test_tc8_msg_004_unknown_method``
   * - §5.1.5.6 — SOMEIPSRV_ONWIRE_03
     - TC8-MSG-005
     - ``msg_resp_header``
     - ``test_someip_message_format::test_tc8_msg_005_session_id_echo``
   * - §5.1.6 — SOMEIP_ETS_074
     - TC8-MSG-006
     - ``msg_error_codes``
     - ``test_someip_message_format::test_tc8_msg_006_wrong_interface_version``
   * - §5.1.6 — SOMEIP_ETS_054, 055, 058, 078
     - TC8-MSG-007
     - ``msg_malformed``
     - | ``test_someip_message_format::test_tc8_msg_007_truncated_message_no_crash``
       | ``test_someip_message_format::test_tc8_msg_007_wrong_protocol_version_no_crash``
       | ``test_someip_message_format::test_tc8_msg_007_oversized_length_field_no_crash``
   * - §5.1.5.6 — SOMEIPSRV_ONWIRE_03
     - TC8-MSG-008
     - ``msg_resp_header``
     - ``test_someip_message_format::test_tc8_msg_008_client_id_echo``

Event Notification
^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - §5.1.5.5 — SOMEIPSRV_BASIC_03; §5.1.6 — SOMEIP_ETS_147
     - TC8-EVT-001
     - ``evt_subscription``
     - ``test_event_notification::test_tc8_evt_001_notification_message_type``
   * - §5.1.5.5 — SOMEIPSRV_BASIC_03
     - TC8-EVT-002
     - ``evt_subscription``
     - ``test_event_notification::test_tc8_evt_002_correct_event_id``
   * - §5.1.6 — SOMEIP_ETS_147
     - TC8-EVT-003
     - ``evt_subscription``
     - ``test_event_notification::test_tc8_evt_003_notification_only_to_subscriber``
   * - §5.1.6 — SOMEIP_ETS_147 (pre-subscribe)
     - TC8-EVT-004
     - ``evt_subscription``
     - ``test_event_notification::test_tc8_evt_004_no_notification_before_subscribe``
   * - §5.1.6 — SOMEIP_ETS_150
     - TC8-EVT-005
     - ``evt_subscription``
     - ``test_event_notification::test_tc8_evt_005_multicast_notification_delivery``
   * - §5.1.6 — SOMEIP_ETS_108
     - TC8-EVT-006
     - ``evt_subscription``
     - ``test_event_notification::test_tc8_evt_006_stop_subscribe_ceases_notifications``
   * - §5.1.5.7 — SOMEIPSRV_RPC_16
     - TC8-EVT-007
     - ``fld_getter_setter``
     - ``test_event_notification::TestEventNotification::test_rpc_16_field_notifies_only_on_change``
   * - §5.1.5.7 — SOMEIPSRV_RPC_15
     - TC8-EVT-008
     - ``evt_subscription``
     - ``test_event_notification::TestEventNotificationFormat::test_rpc_15_cyclic_notification_rate``

Field Conformance
^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - §5.1.6 — SOMEIP_ETS_121
     - TC8-FLD-001
     - ``fld_initial_value``
     - ``test_field_conformance::test_tc8_fld_001_initial_notification_on_subscribe``
   * - §5.1.6 — SOMEIP_ETS_121
     - TC8-FLD-002
     - ``fld_initial_value``
     - ``test_field_conformance::test_tc8_fld_002_is_field_sends_initial_value_within_one_second``
   * - §5.1.5.7 — SOMEIPSRV_RPC_03; §5.1.6 — SOMEIP_ETS_166
     - TC8-FLD-003
     - ``fld_get_set``
     - ``test_field_conformance::test_tc8_fld_003_getter_returns_current_value``
   * - §5.1.5.7 — SOMEIPSRV_RPC_11; §5.1.6 — SOMEIP_ETS_166
     - TC8-FLD-004
     - ``fld_get_set``
     - ``test_field_conformance::test_tc8_fld_004_setter_updates_value_and_notifies``

TCP Transport Binding
^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - §5.1.5.7 — SOMEIPSRV_RPC_01
     - TC8-TCP-001
     - ``tcp_transport``
     - ``test_someip_message_format::test_tc8_rpc_01_tcp_request_response``
   * - §5.1.5.7 — SOMEIPSRV_RPC_01
     - TC8-TCP-002
     - ``tcp_transport``
     - ``test_someip_message_format::test_tc8_rpc_01_tcp_session_id_echo``
   * - §5.1.5.7 — SOMEIPSRV_RPC_01
     - TC8-TCP-003
     - ``tcp_transport``
     - ``test_someip_message_format::test_tc8_rpc_01_tcp_client_id_echo``
   * - §5.1.5.7 — SOMEIPSRV_RPC_02
     - TC8-TCP-004
     - ``tcp_transport``
     - ``test_someip_message_format::test_tc8_rpc_02_tcp_multiple_methods_single_connection``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_15
     - TC8-TCP-005
     - ``tcp_transport``
     - ``test_someip_message_format::test_tc8_sd_options_15_tcp_endpoint_advertised``
   * - §5.1.5.7 — SOMEIPSRV_RPC_17
     - TC8-TCP-006
     - ``tcp_transport``
     - ``test_field_conformance::test_tc8_rpc_17_tcp_field_getter``
   * - §5.1.5.7 — SOMEIPSRV_RPC_17
     - TC8-TCP-007
     - ``tcp_transport``
     - ``test_field_conformance::test_tc8_rpc_17_tcp_field_setter``
   * - §5.1.5.7 — SOMEIPSRV_RPC_17
     - TC8-TCP-008
     - ``tcp_transport``
     - ``test_event_notification::test_tc8_rpc_17_tcp_event_notification_delivery``
   * - SOMEIP_ETS_068
     - TC8-TCP-009
     - ``tcp_transport``
     - ``test_someip_message_format::test_tc8_ets_068_unaligned_someip_messages_over_tcp``

.. note::

   **SOMEIPSRV_RPC_17 partial coverage:** TC8-TCP-006, TC8-TCP-007,
   and TC8-TCP-008 verify TCP transport for field GET/SET and event
   notification operations using a single service instance; the full
   SOMEIPSRV_RPC_17 requirement (each service instance on a separate
   TCP connection) is not covered — multi-instance TCP is a known gap.

UDP Transport Binding
^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - SOMEIP_ETS_069
     - TC8-UDP-001
     - ``udp_transport``
     - ``test_someip_message_format::test_tc8_ets_069_unaligned_someip_messages_over_udp``

Multi-service and Multi-instance
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - §5.1.5.7 — SOMEIPSRV_RPC_13
     - TC8-MULTI-001
     - ``multi_service_routing``
     - ``test_multi_service::TestMultiServiceInstanceRouting::test_rpc_13_multi_service_config_loads_and_primary_service_offered``
   * - §5.1.5.7 — SOMEIPSRV_RPC_14
     - TC8-MULTI-002
     - ``multi_service_routing``
     - | ``test_multi_service::TestMultiServiceInstanceRouting::test_rpc_14_service_a_advertises_configured_udp_port``
       | ``test_multi_service::TestMultiServiceInstanceRouting::test_rpc_14_no_unexpected_service_ids_in_offers``

SD Format and Options Compliance
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_01
     - TC8-SDF-001
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsOfferService::test_format_01_client_id_is_zero``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_02
     - TC8-SDF-002
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsOfferService::test_format_02_session_id_is_nonzero_and_in_range``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_04
     - TC8-SDF-003
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsOfferService::test_format_04_interface_version_is_one``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_05
     - TC8-SDF-004
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsOfferService::test_format_05_message_type_is_notification``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_06
     - TC8-SDF-005
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsOfferService::test_format_06_return_code_is_e_ok``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_09
     - TC8-SDF-006
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsOfferService::test_format_09_sd_flags_reserved_bits_are_zero``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_10
     - TC8-SDF-007
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsOfferService::test_format_10_sd_entry_reserved_bytes_are_zero``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_11
     - TC8-SDF-008
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdOfferEntryFields::test_format_11_entry_is_16_bytes``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_12
     - TC8-SDF-009
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdOfferEntryFields::test_format_12_first_option_run_index_is_zero``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_13
     - TC8-SDF-010
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdOfferEntryFields::test_format_13_num_options_matches_options_list``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_15
     - TC8-SDF-011
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdOfferEntryFields::test_format_15_instance_id_matches_config``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_16
     - TC8-SDF-012
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdOfferEntryFields::test_format_16_major_version_matches_config``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_18
     - TC8-SDF-013
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdOfferEntryFields::test_format_18_minor_version_matches_config``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_19
     - TC8-SDF-014
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsSubscribeAck::test_format_19_ack_entry_type_is_subscribe_ack``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_20
     - TC8-SDF-015
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsSubscribeAck::test_format_20_ack_entry_is_16_bytes``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_21
     - TC8-SDF-016
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsSubscribeAck::test_format_21_ack_option_run_index_is_zero_when_no_options``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_23
     - TC8-SDF-017
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsSubscribeAck::test_format_23_ack_service_id_matches_config``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_24
     - TC8-SDF-018
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsSubscribeAck::test_format_24_ack_instance_id_matches_config``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_25
     - TC8-SDF-019
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsSubscribeAck::test_format_25_ack_major_version_matches_config``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_26
     - TC8-SDF-020
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsSubscribeAck::test_format_26_ack_ttl_is_nonzero``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_27
     - TC8-SDF-021
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsSubscribeAck::test_format_27_ack_reserved_field_is_zero``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_28
     - TC8-SDF-022
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdHeaderFieldsSubscribeAck::test_format_28_ack_eventgroup_id_matches_subscribe``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_01
     - TC8-SDF-023
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsEndpoint::test_options_01_endpoint_option_length_is_nine``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_02
     - TC8-SDF-024
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsEndpoint::test_options_02_endpoint_option_type_is_0x04``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_03
     - TC8-SDF-025
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsEndpoint::test_options_03_endpoint_option_reserved_after_type_is_zero``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_05
     - TC8-SDF-026
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsEndpoint::test_options_05_endpoint_option_reserved_before_protocol_is_zero``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_06
     - TC8-SDF-027
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsEndpoint::test_options_06_endpoint_option_protocol_is_udp``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_08
     - TC8-SDF-028
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsMulticast::test_options_08_multicast_option_length_is_nine``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_09
     - TC8-SDF-029
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsMulticast::test_options_09_multicast_option_type_is_0x14``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_10
     - TC8-SDF-030
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsMulticast::test_options_10_multicast_option_reserved_is_zero``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_11
     - TC8-SDF-031
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsMulticast::test_options_11_multicast_address_matches_config``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_12
     - TC8-SDF-032
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsMulticast::test_options_12_multicast_option_reserved_before_port_is_zero``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_13
     - TC8-SDF-033
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsMulticast::test_options_13_multicast_option_protocol_is_udp``
   * - §5.1.5.2 — SOMEIPSRV_OPTIONS_14
     - TC8-SDF-034
     - ``sd_options_fields``
     - ``test_sd_format_compliance::TestSdOptionsMulticast::test_options_14_multicast_port_matches_config``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_12
     - TC8-SDF-035
     - ``sd_stop_sub_fmt``
     - ``test_sd_format_compliance::TestSdStopSubscribeFormat::test_sd_message_12_stop_subscribe_entry_format``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_03
     - TC8-SDF-036
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdMissingFormatFields::test_format_03_protocol_version_is_one``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_08
     - TC8-SDF-037
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdMissingFormatFields::test_format_07_unicast_flag_set``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_14
     - TC8-SDF-038
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdMissingFormatFields::test_format_14_entry_type_is_offer``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_17
     - TC8-SDF-039
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdMissingFormatFields::test_format_17_ttl_is_nonzero``
   * - §5.1.5.1 — SOMEIPSRV_FORMAT_22
     - TC8-SDF-040
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdMissingFormatFields::test_format_22_ack_num_options_1_matches``

.. note::

   TC8-SDF-028 through TC8-SDF-034 (SOMEIPSRV_OPTIONS_08–14, multicast option
   fields) require a non-loopback interface and are skipped on loopback with
   ``@pytest.mark.network``.

.. note::

   **TC8-SDF-037 naming:** The test function is named
   ``test_format_07_unicast_flag_set`` but verifies the **Unicast Flag (bit 6)**
   of the SD Flags byte, which corresponds to ``SOMEIPSRV_FORMAT_08`` in the OA
   spec.  ``SOMEIPSRV_FORMAT_07`` (Reboot Flag) is a separate requirement covered
   by ``TC8-SD-012``.

SD Entry Semantics
^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_01
     - TC8-SDM-001
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_01_instance_wildcard``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_02
     - TC8-SDM-002
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_02_instance_specific``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_03
     - TC8-SDM-003
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_03_major_version_wildcard``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_04
     - TC8-SDM-004
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_04_major_version_specific``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_05
     - TC8-SDM-005
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_05_minor_version_wildcard``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_06
     - TC8-SDM-006
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_06_minor_version_specific``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_14
     - TC8-SDM-007
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_14_wrong_major_version``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_15
     - TC8-SDM-008
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_15_wrong_service_id``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_16
     - TC8-SDM-009
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_16_wrong_instance_id``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_17
     - TC8-SDM-010
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_17_unknown_eventgroup_id``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_18
     - TC8-SDM-011
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_18_ttl_zero_stop_subscribe``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_19
     - TC8-SDM-012
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_19_reserved_field_set``
   * - §5.1.5.4 — SOMEIPSRV_SD_BEHAVIOR_03
     - TC8-SDM-013
     - ``sd_cyclic_timing``
     - ``test_service_discovery::TestSDFindServiceTiming::test_sd_behavior_03_unicast_findservice_timing``
   * - §5.1.5.4 — SOMEIPSRV_SD_BEHAVIOR_04
     - TC8-SDM-014
     - ``sd_cyclic_timing``
     - ``test_service_discovery::TestSDFindServiceTiming::test_sd_behavior_04_multicast_findservice_timing``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_07
     - TC8-SDM-015
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdEntryOptionFields::test_sd_message_07_offer_entry_type_byte``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_08
     - TC8-SDM-016
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdEntryOptionFields::test_sd_message_08_offer_entry_option_run2_index_zero``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_09
     - TC8-SDM-017
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdEntryOptionFields::test_sd_message_09_offer_entry_num_options_2_zero``
   * - §5.1.5.3 — SOMEIPSRV_SD_MESSAGE_11
     - TC8-SDM-018
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdEntryOptionFields::test_sd_message_11_subscribe_entry_type_byte``

.. note::

   **SOMEIPSRV_SD_MESSAGE_08 dual coverage:** ``TC8-SD-001`` verifies that an
   OfferService message is *present* on multicast at startup (behavioural
   assertion).  ``TC8-SDM-016`` verifies the *option_index_2 byte* in the
   serialised entry is zero — a distinct field-level assertion from the same
   spec requirement.

.. note::

   TC8-SDM-012 (SOMEIPSRV_SD_MESSAGE_19) is expected to **FAIL** against
   vsomeip 3.6.1: the stack sends a positive ACK instead of NAck for a
   SubscribeEventgroup with reserved bits set.  See the "Known SOME/IP Stack
   Limitations" section in :doc:`/architecture/tc8_conformance_testing`.

SD Lifecycle Advanced
^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - §5.1.6 — SOMEIP_ETS_088
     - TC8-SDLC-001
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_088_two_subscribes_same_session``
   * - §5.1.6 — SOMEIP_ETS_092
     - TC8-SDLC-002
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_092_ttl_zero_stop_subscribe_no_nack``
   * - §5.1.6 — SOMEIP_ETS_098
     - TC8-SDLC-003
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_098_subscribe_accepted_without_prior_rpc``
   * - §5.1.6 — SOMEIP_ETS_107
     - TC8-SDLC-004
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_107_find_service_and_subscribe_processed_independently``
   * - §5.1.6 — SOMEIP_ETS_120
     - TC8-SDLC-005
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_120_subscribe_endpoint_ip_matches_tester``
   * - §5.1.6 — SOMEIP_ETS_122
     - TC8-SDLC-006
     - ``sd_offer_format``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_122_sd_interface_version_is_one``
   * - §5.1.6 — SOMEIP_ETS_155
     - TC8-SDLC-007
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_155_resubscribe_after_stop``
   * - §5.1.6 — SOMEIP_ETS_091
     - TC8-SDLC-008
     - ``sd_offer_format``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_091_session_id_increments``
   * - §5.1.6 — SOMEIP_ETS_099
     - TC8-SDLC-009
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_099_initial_event_sent_after_subscribe``
   * - §5.1.6 — SOMEIP_ETS_100
     - TC8-SDLC-010
     - ``sd_offer_format``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_100_no_findservice_emitted_by_server``
   * - §5.1.6 — SOMEIP_ETS_101
     - TC8-SDLC-011
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_101_stop_offer_ceases_client_events``
   * - §5.1.6 — SOMEIP_ETS_128
     - TC8-SDLC-012
     - ``sd_find_response``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_128_multicast_findservice_version_wildcard``
   * - §5.1.6 — SOMEIP_ETS_130
     - TC8-SDLC-013
     - ``sd_find_response``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_130_multicast_findservice_unicast_flag_clear``
   * - §5.1.6 — SOMEIP_ETS_084
     - TC8-SDLC-014
     - ``sd_sub_lifecycle``
     - ``test_sd_client::TestSDClientStopSubscribe::test_ets_084_stop_subscribe_ceases_events``
   * - §5.1.6 — SOMEIP_ETS_081
     - TC8-SDLC-015
     - ``sd_reboot``
     - ``test_sd_client::TestSDClientReboot::test_ets_081_reboot_flag_set_after_first_restart``
   * - §5.1.6 — SOMEIP_ETS_082
     - TC8-SDLC-016
     - ``sd_reboot``
     - ``test_sd_client::TestSDClientReboot::test_ets_082_reboot_flag_set_after_second_restart``
   * - §5.1.6 — SOMEIP_ETS_093
     - TC8-SDLC-017
     - ``sd_reboot``
     - ``test_sd_reboot::TestSDReboot::test_ets_093_reboot_on_unicast_channel``
   * - §5.1.6 — SOMEIP_ETS_094
     - TC8-SDLC-018
     - ``sd_reboot``
     - ``test_sd_reboot::TestSDReboot::test_ets_094_server_reboot_session_id_resets``
   * - §5.1.6 — SOMEIP_ETS_095
     - TC8-SDLC-019
     - ``sd_ttl_expiry``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_095_subscribe_ttl_expires_no_events``
   * - §5.1.6 — SOMEIP_ETS_105
     - TC8-SDLC-020
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_105_initial_event_udp_unicast``
   * - §5.1.6 — SOMEIP_ETS_106
     - TC8-SDLC-021
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_106_subscribe_eventgroup_ack_received``
   * - §5.1.6 — SOMEIP_ETS_121
     - TC8-SDLC-022
     - ``fld_initial_value``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_121_initial_field_event_after_subscribe``
   * - §5.1.6 — SOMEIP_ETS_173
     - TC8-SDLC-023
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_173_unicast_subscribe_receives_ack``
   * - §5.1.6 — SOMEIP_ETS_104
     - TC8-SDLC-024
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_104_last_value_udp_multicast``
   * - §5.1.6 — SOMEIP_ETS_127
     - TC8-SDLC-025
     - ``sd_find_response``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_127_multicast_findservice_response``

.. note::

   TC8-SDLC-011 (SOMEIP_ETS_101) is implemented as ``pytest.skip`` because
   stopping the DUT's own OfferService from an external tester requires a
   dedicated reverse-direction SD client target; the current target does not
   include that capability.

SD Robustness
^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - §5.1.6 — SOMEIP_ETS_111
     - TC8-SDROBUST-001
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedEntries::test_ets_111_empty_entries_array``
   * - §5.1.6 — SOMEIP_ETS_112/113
     - TC8-SDROBUST-002
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedEntries::test_ets_112_empty_option_zero_length``
   * - §5.1.6 — SOMEIP_ETS_114
     - TC8-SDROBUST-003
     - ``sd_robustness``
     - | ``test_sd_robustness::TestSDMalformedEntries::test_ets_114_entries_length_zero``
       | ``test_sd_robustness::TestSDMalformedEntries::test_ets_114_entries_length_mismatched``
   * - §5.1.6 — SOMEIP_ETS_115
     - TC8-SDROBUST-004
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedEntries::test_ets_115_entry_refs_more_options_than_exist``
   * - §5.1.6 — SOMEIP_ETS_116
     - TC8-SDROBUST-005
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedEntries::test_ets_116_entry_unknown_option_type``
   * - §5.1.6 — SOMEIP_ETS_117
     - TC8-SDROBUST-006
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedEntries::test_ets_117_two_entries_same_option``
   * - §5.1.6 — SOMEIP_ETS_118
     - TC8-SDROBUST-007
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedEntries::test_ets_118_find_service_with_endpoint_option``
   * - §5.1.6 — SOMEIP_ETS_123/124
     - TC8-SDROBUST-008
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedEntries::test_ets_123_entries_length_wildly_too_large``
   * - §5.1.6 — SOMEIP_ETS_125
     - TC8-SDROBUST-009
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedEntries::test_ets_125_truncated_entry``
   * - §5.1.6 — SOMEIP_ETS_134
     - TC8-SDROBUST-010
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedOptions::test_ets_134_option_length_much_too_large``
   * - §5.1.6 — SOMEIP_ETS_135
     - TC8-SDROBUST-011
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedOptions::test_ets_135_option_length_one_too_large``
   * - §5.1.6 — SOMEIP_ETS_136
     - TC8-SDROBUST-012
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedOptions::test_ets_136_option_length_too_short``
   * - §5.1.6 — SOMEIP_ETS_137
     - TC8-SDROBUST-013
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedOptions::test_ets_137_option_length_unaligned``
   * - §5.1.6 — SOMEIP_ETS_138
     - TC8-SDROBUST-014
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedOptions::test_ets_138_options_array_length_too_large``
   * - §5.1.6 — SOMEIP_ETS_139
     - TC8-SDROBUST-015
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedOptions::test_ets_139_options_array_length_too_short``
   * - §5.1.6 — SOMEIP_ETS_174
     - TC8-SDROBUST-016
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedOptions::test_ets_174_unknown_option_type_0x77``
   * - §5.1.6 — SOMEIP_ETS_109
     - TC8-SDROBUST-017
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDSubscribeEdgeCases::test_ets_109_subscribe_no_endpoint_option``
   * - §5.1.6 — SOMEIP_ETS_110
     - TC8-SDROBUST-018
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDSubscribeEdgeCases::test_ets_110_subscribe_endpoint_ip_zero``
   * - §5.1.6 — SOMEIP_ETS_119
     - TC8-SDROBUST-019
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDSubscribeEdgeCases::test_ets_119_subscribe_unknown_l4proto``
   * - §5.1.6 — SOMEIP_ETS_140
     - TC8-SDROBUST-020
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDSubscribeEdgeCases::test_ets_140_subscribe_unknown_service_id``
   * - §5.1.6 — SOMEIP_ETS_141
     - TC8-SDROBUST-021
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDSubscribeEdgeCases::test_ets_141_subscribe_unknown_instance_id``
   * - §5.1.6 — SOMEIP_ETS_142
     - TC8-SDROBUST-022
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDSubscribeEdgeCases::test_ets_142_subscribe_unknown_eventgroup_id``
   * - §5.1.6 — SOMEIP_ETS_143
     - TC8-SDROBUST-023
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDSubscribeEdgeCases::test_ets_143_subscribe_all_ids_unknown``
   * - §5.1.6 — SOMEIP_ETS_144
     - TC8-SDROBUST-024
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDSubscribeEdgeCases::test_ets_144_subscribe_reserved_option_type``
   * - §5.1.6 — SOMEIP_ETS_152
     - TC8-SDROBUST-025
     - ``sd_robustness``
     - | ``test_sd_robustness::TestSDMessageFramingErrors::test_ets_152_high_session_id_0xfffe``
       | ``test_sd_robustness::TestSDMessageFramingErrors::test_ets_152_session_id_0xffff``
       | ``test_sd_robustness::TestSDMessageFramingErrors::test_ets_152_session_id_one``
   * - §5.1.6 — SOMEIP_ETS_153
     - TC8-SDROBUST-026
     - ``sd_robustness``
     - | ``test_sd_robustness::TestSDMessageFramingErrors::test_ets_153_someip_length_too_small``
       | ``test_sd_robustness::TestSDMessageFramingErrors::test_ets_153_someip_length_too_large``
   * - §5.1.6 — SOMEIP_ETS_178
     - TC8-SDROBUST-027
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMessageFramingErrors::test_ets_178_wrong_someip_service_id``
   * - §5.1.6 — SOMEIP_ETS_058
     - TC8-SDROBUST-028
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMessageFramingErrors::test_ets_058_someip_length_way_too_long``
   * - §5.1.6 — SOMEIP_ETS_113
     - TC8-SDROBUST-029
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedEntries::test_ets_113_empty_options_array_with_subscribe``
   * - §5.1.6 — SOMEIP_ETS_124
     - TC8-SDROBUST-030
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedEntries::test_ets_124_entries_length_too_long_by_small_margin``
   * - §5.1.6 — SOMEIP_ETS_154
     - TC8-SDROBUST-031
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDSubscribeEdgeCases::test_ets_154_subscribe_nonroutable_endpoint_ip``
   * - §5.1.6 — SOMEIP_ETS_162
     - TC8-SDROBUST-032
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDSubscribeEdgeCases::test_ets_162_subscribe_endpoint_ip_not_subscriber``
   * - §5.1.6 — SOMEIP_ETS_163
     - TC8-SDROBUST-033
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDSubscribeEdgeCases::test_ets_163_subscribe_endpoint_dut_address``
   * - §5.1.6 — SOMEIP_ETS_175
     - TC8-SDROBUST-034
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedOptions::test_ets_175_unreferenced_option_ignored``
   * - §5.1.6 — SOMEIP_ETS_176
     - TC8-SDROBUST-035
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedOptions::test_ets_176_trailing_data_after_options_array``
   * - §5.1.6 — SOMEIP_ETS_177
     - TC8-SDROBUST-036
     - ``sd_robustness``
     - ``test_sd_robustness::TestSDMalformedOptions::test_ets_177_trailing_data_wrong_options_length``

SOME/IP Message Protocol Compliance
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 5)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - §5.1.5.5 — SOMEIPSRV_BASIC_01
     - TC8-MSG-009
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipBasicIdentifiers::test_basic_01_correct_service_id_gets_response``
   * - §5.1.5.5 — SOMEIPSRV_BASIC_02
     - TC8-MSG-010
     - ``msg_error_codes``
     - ``test_someip_message_format::TestSomeipBasicIdentifiers::test_basic_02_unknown_service_id_no_response_or_error``
   * - §5.1.5.5 — SOMEIPSRV_BASIC_03
     - TC8-MSG-011
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipBasicIdentifiers::test_basic_03_event_method_id_no_response``
   * - §5.1.5.6 — SOMEIPSRV_ONWIRE_01
     - TC8-MSG-012
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipResponseFields::test_onwire_01_response_source_address``
   * - §5.1.5.6 — SOMEIPSRV_ONWIRE_02
     - TC8-MSG-013
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipResponseFields::test_onwire_02_method_id_msb_zero_in_response``
   * - §5.1.5.6 — SOMEIPSRV_ONWIRE_04
     - TC8-MSG-014
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipResponseFields::test_onwire_04_request_id_reuse``
   * - §5.1.5.6 — SOMEIPSRV_ONWIRE_06
     - TC8-MSG-015
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipResponseFields::test_onwire_06_interface_version_echoed``
   * - §5.1.5.6 — SOMEIPSRV_ONWIRE_11
     - TC8-MSG-016
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipResponseFields::test_onwire_11_normal_response_return_code_ok``
   * - §5.1.5.7 — SOMEIPSRV_RPC_18
     - TC8-MSG-017
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipResponseFields::test_rpc_18_message_id_echoed``
   * - §5.1.5.7 — SOMEIPSRV_RPC_20
     - TC8-MSG-018
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipResponseFields::test_rpc_20_interface_version_copied_from_request``
   * - §5.1.5.7 — SOMEIPSRV_RPC_05
     - TC8-MSG-019
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_rpc_05_fire_and_forget_no_error``
   * - §5.1.5.7 — SOMEIPSRV_RPC_06
     - TC8-MSG-020
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_rpc_06_return_code_upper_bits_zero``
   * - §5.1.5.7 — SOMEIPSRV_RPC_07
     - TC8-MSG-021
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_rpc_07_request_with_return_code_bits_set``
   * - §5.1.5.7 — SOMEIPSRV_RPC_08
     - TC8-MSG-022
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_rpc_08_request_with_error_return_code_no_reply``
   * - §5.1.5.7 — SOMEIPSRV_RPC_09
     - TC8-MSG-023
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_rpc_09_error_response_no_payload``
   * - §5.1.5.7 — SOMEIPSRV_RPC_10
     - TC8-MSG-024
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_rpc_10_fire_and_forget_reserved_type_no_error``
   * - §5.1.6 — SOMEIP_ETS_004
     - TC8-MSG-025
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_ets_004_burst_10_sequential_requests``
   * - §5.1.6 — SOMEIP_ETS_054
     - TC8-MSG-026
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_ets_054_empty_payload_request``
   * - §5.1.6 — SOMEIP_ETS_059
     - TC8-MSG-027
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_ets_059_fire_and_forget_wrong_service_no_error``
   * - §5.1.6 — SOMEIP_ETS_061
     - TC8-MSG-028
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_ets_061_two_sequential_requests``
   * - §5.1.6 — SOMEIP_ETS_075
     - TC8-MSG-029
     - ``msg_malformed``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_ets_075_notification_as_request_ignored``
   * - §5.1.6 — SOMEIP_ETS_005
     - TC8-MSG-030
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipResponseFields::test_ets_005_response_uses_big_endian_byte_order``
   * - §5.1.6 — SOMEIP_ETS_058
     - TC8-MSG-031
     - ``msg_malformed_handling``
     - ``test_someip_message_format::TestSomeipFireAndForgetAndErrors::test_ets_058_oversized_length_field_no_crash``
   * - §5.1.5.7 — SOMEIPSRV_RPC_19
     - TC8-MSG-032
     - ``msg_resp_header``
     - ``test_someip_message_format::TestSomeipResponseFields::test_rpc_19_session_id_echoed_in_error``

.. note::

   TC8-MSG-022 (SOMEIPSRV_RPC_08) is expected to **FAIL** against vsomeip
   3.6.1: the stack replies to a REQUEST with non-zero return_code when the
   spec requires no reply.  See the "Known SOME/IP Stack Limitations" section
   in :doc:`/architecture/tc8_conformance_testing`.

   TC8-MSG-010 (SOMEIPSRV_BASIC_02) is expected to **FAIL** against vsomeip
   3.6.1: the stack silently drops unknown-service requests rather than
   responding with E_UNKNOWN_SERVICE.

Coverage Summary
----------------

.. list-table::
   :header-rows: 1
   :widths: 20 15 15 15

   * - TC8 Area
     - Total Test IDs
     - Implemented
     - OA Spec Mapped
   * - Service Discovery
     - 14
     - 14
     - 14
   * - SD Format and Options Compliance
     - 40
     - 40
     - 40
   * - SD Entry Semantics
     - 18
     - 18
     - 18
   * - SD Lifecycle Advanced
     - 25
     - 25
     - 25
   * - SD Robustness
     - 36
     - 36
     - 36
   * - Message Format (existing)
     - 8
     - 8
     - 8
   * - SOME/IP Message Protocol Compliance
     - 24
     - 24
     - 24
   * - Event Notification
     - 8
     - 8
     - 8
   * - Field Conformance
     - 4
     - 4
     - 4
   * - TCP Transport Binding
     - 9
     - 9
     - 9
   * - UDP Transport Binding
     - 1
     - 1
     - 1
   * - Multi-service and Multi-instance
     - 2
     - 2
     - 2
   * - **Total**
     - **189**
     - **189**
     - **189**

.. note::

   Some TC8 test IDs are implemented by multiple test functions to separate
   independent assertions.  The 189 IDs above correspond to approximately
   215 pytest functions in total.

.. note::

   Three tests are expected to **FAIL** against vsomeip 3.6.1 due to known
   stack limitations.  See the "Known SOME/IP Stack Limitations" section in
   :doc:`/architecture/tc8_conformance_testing`.

.. note::

   Coverage is reported against the subset of TC8 test cases implemented.
   For the full OA TC8 v3.0 Chapter 5 scope analysis see
   :doc:`/architecture/tc8_conformance_testing`.

How to Update
-------------

When adding a new TC8 test case:

1. Read the OA specification Chapter 5 to identify the exact test case
   section number and title.
2. Add a row to the appropriate area table above with the OA reference,
   internal TC8 ID, requirement, and test function.
3. Ensure the test function calls ``record_property("FullyVerifies", ...)``
   with the matching ``comp_req__tc8_conformance__<id>``.
4. Update the Coverage Summary counts.
