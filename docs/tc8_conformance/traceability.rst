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

The following table links each OA TC8 specification test case to the
project's internal test ID, component requirement, and implementing
Python test function(s).  All requirement IDs use the
``comp_req__tc8_conformance__`` prefix (omitted for brevity).

Service Discovery
^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 6)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_08
     - TC8-SD-001
     - ``sd_offer_format``
     - ``test_service_discovery::test_tc8_sd_001_multicast_offer_on_startup``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_14-18
     - TC8-SD-002
     - ``sd_offer_format``
     - ``test_service_discovery::test_tc8_sd_002_offer_entry_format``
   * - Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_02
     - TC8-SD-003
     - ``sd_cyclic_timing``
     - ``test_service_discovery::test_tc8_sd_003_cyclic_offer_timing``
   * - Section 6.1.6, SOMEIP_ETS_171
     - TC8-SD-004
     - ``sd_find_response``
     - ``test_service_discovery::test_tc8_sd_004_find_known_service_unicast_offer``
   * - Section 6.1.5.4, implied by SD_BEHAVIOR_03/04
     - TC8-SD-005
     - ``sd_find_response``
     - ``test_service_discovery::test_tc8_sd_005_find_unknown_service_no_response``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_13
     - TC8-SD-006
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::test_tc8_sd_006_subscribe_valid_eventgroup_ack``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_14; Section 6.1.6, SOMEIP_ETS_140
     - TC8-SD-007
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::test_tc8_sd_007_subscribe_unknown_eventgroup_nack``
   * - Section 6.1.6, SOMEIP_ETS_108, SOMEIP_ETS_092
     - TC8-SD-008
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::test_tc8_sd_008_stop_subscribe_ceases_notifications``
   * - Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_01
     - TC8-SD-009
     - ``sd_phases_timing``
     - ``test_sd_phases_timing::test_tc8_sd_009_repetition_phase_intervals``
   * - Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_01
     - TC8-SD-010
     - ``sd_phases_timing``
     - ``test_sd_phases_timing::test_tc8_sd_010_repetition_count_before_main_phase``
   * - Section 6.1.5.2, SOMEIPSRV_OPTIONS_01-07
     - TC8-SD-011
     - ``sd_endpoint_option``
     - ``test_service_discovery::test_tc8_sd_011_offer_ipv4_endpoint_option``
   * - Section 6.1.5.1, SOMEIPSRV_FORMAT_02, FORMAT_07
     - TC8-SD-012
     - ``sd_reboot``
     - | ``test_sd_reboot::test_tc8_sd_012_reboot_flag_set_after_restart``
       | ``test_sd_reboot::test_tc8_sd_012_session_id_resets_after_restart``
   * - Section 6.1.5.2, SOMEIPSRV_OPTIONS_08-14
     - TC8-SD-013
     - ``sd_mcast_eg``
     - ``test_service_discovery::test_tc8_sd_013_subscribe_ack_has_multicast_option``
   * - Section 6.1.6, SOMEIP_ETS_095
     - TC8-SD-014
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::test_tc8_sd_014_ttl_expiry_ceases_notifications``

SD Entry Semantics
^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 6)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_01
     - TC8-SDM-001
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_01_instance_wildcard``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_02
     - TC8-SDM-002
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_02_instance_specific``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_03
     - TC8-SDM-003
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_03_major_version_wildcard``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_04
     - TC8-SDM-004
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_04_major_version_specific``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_05
     - TC8-SDM-005
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_05_minor_version_wildcard``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_06
     - TC8-SDM-006
     - ``sd_find_response``
     - ``test_service_discovery::TestSDVersionMatching::test_sd_message_06_minor_version_specific``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_14
     - TC8-SDM-007
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_14_wrong_major_version``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_15
     - TC8-SDM-008
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_15_wrong_service_id``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_16
     - TC8-SDM-009
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_16_wrong_instance_id``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_17
     - TC8-SDM-010
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_17_unknown_eventgroup_id``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_18
     - TC8-SDM-011
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_18_ttl_zero_stop_subscribe``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_19
     - TC8-SDM-012
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeNAck::test_sd_message_19_reserved_field_set``
   * - Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_03
     - TC8-SDM-013
     - ``sd_cyclic_timing``
     - ``test_service_discovery::TestSDFindServiceTiming::test_sd_behavior_03_unicast_findservice_timing``
   * - Section 6.1.5.4, SOMEIPSRV_SD_BEHAVIOR_04
     - TC8-SDM-014
     - ``sd_cyclic_timing``
     - ``test_service_discovery::TestSDFindServiceTiming::test_sd_behavior_04_multicast_findservice_timing``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_07
     - TC8-SDM-015
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdEntryOptionFields::test_sd_message_07_offer_entry_type_byte``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_08
     - TC8-SDM-016
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdEntryOptionFields::test_sd_message_08_offer_entry_option_run2_index_zero``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_09
     - TC8-SDM-017
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdEntryOptionFields::test_sd_message_09_offer_entry_num_options_2_zero``
   * - Section 6.1.5.3, SOMEIPSRV_SD_MESSAGE_11
     - TC8-SDM-018
     - ``sd_format_fields``
     - ``test_sd_format_compliance::TestSdEntryOptionFields::test_sd_message_11_subscribe_entry_type_byte``

.. note::

   **SOMEIPSRV_SD_MESSAGE_08 dual coverage:** ``TC8-SD-001`` verifies that an
   OfferService message is *present* on multicast at startup (behavioural
   assertion).  ``TC8-SDM-016`` verifies the *option_index_2 byte* in the
   serialised entry is zero. This is a distinct field-level assertion from
   the same spec requirement.

.. note::

   TC8-SDM-012 (SOMEIPSRV_SD_MESSAGE_19) is expected to **FAIL** against
   vsomeip 3.6.1: the stack sends a positive ACK instead of NAck for a
   SubscribeEventgroup with reserved bits set.

SD Lifecycle Advanced
^^^^^^^^^^^^^^^^^^^^^^

.. list-table::
   :header-rows: 1
   :widths: 25 12 18 45

   * - OA Spec Reference (Ch. 6)
     - Internal ID
     - Requirement
     - Test Function(s)
   * - Section 6.1.6, SOMEIP_ETS_088
     - TC8-SDLC-001
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_088_two_subscribes_same_session``
   * - Section 6.1.6, SOMEIP_ETS_092
     - TC8-SDLC-002
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_092_ttl_zero_stop_subscribe_no_nack``
   * - Section 6.1.6, SOMEIP_ETS_098
     - TC8-SDLC-003
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_098_subscribe_accepted_without_prior_rpc``
   * - Section 6.1.6, SOMEIP_ETS_107
     - TC8-SDLC-004
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_107_find_service_and_subscribe_processed_independently``
   * - Section 6.1.6, SOMEIP_ETS_120
     - TC8-SDLC-005
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_120_subscribe_endpoint_ip_matches_tester``
   * - Section 6.1.6, SOMEIP_ETS_122
     - TC8-SDLC-006
     - ``sd_offer_format``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_122_sd_interface_version_is_one``
   * - Section 6.1.6, SOMEIP_ETS_155
     - TC8-SDLC-007
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_155_resubscribe_after_stop``
   * - Section 6.1.6, SOMEIP_ETS_091
     - TC8-SDLC-008
     - ``sd_offer_format``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_091_session_id_increments``
   * - Section 6.1.6, SOMEIP_ETS_099
     - TC8-SDLC-009
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_099_initial_event_sent_after_subscribe``
   * - Section 6.1.6, SOMEIP_ETS_100
     - TC8-SDLC-010
     - ``sd_offer_format``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_100_no_findservice_emitted_by_server``
   * - Section 6.1.6, SOMEIP_ETS_101
     - TC8-SDLC-011
     - ``sd_sub_lifecycle``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_101_stop_offer_ceases_client_events``
   * - Section 6.1.6, SOMEIP_ETS_128
     - TC8-SDLC-012
     - ``sd_find_response``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_128_multicast_findservice_version_wildcard``
   * - Section 6.1.6, SOMEIP_ETS_130
     - TC8-SDLC-013
     - ``sd_find_response``
     - ``test_service_discovery::TestSDFindServiceAdvanced::test_ets_130_multicast_findservice_unicast_flag_clear``
   * - Section 6.1.6, SOMEIP_ETS_084
     - TC8-SDLC-014
     - ``sd_sub_lifecycle``
     - ``test_sd_client::TestSDClientStopSubscribe::test_ets_084_stop_subscribe_ceases_events``
   * - Section 6.1.6, SOMEIP_ETS_081
     - TC8-SDLC-015
     - ``sd_reboot``
     - ``test_sd_client::TestSDClientReboot::test_ets_081_reboot_flag_set_after_first_restart``
   * - Section 6.1.6, SOMEIP_ETS_082
     - TC8-SDLC-016
     - ``sd_reboot``
     - ``test_sd_client::TestSDClientReboot::test_ets_082_reboot_flag_set_after_second_restart``
   * - Section 6.1.6, SOMEIP_ETS_093
     - TC8-SDLC-017
     - ``sd_reboot``
     - ``test_sd_reboot::TestSDReboot::test_ets_093_reboot_on_unicast_channel``
   * - Section 6.1.6, SOMEIP_ETS_094
     - TC8-SDLC-018
     - ``sd_reboot``
     - ``test_sd_reboot::TestSDReboot::test_ets_094_server_reboot_session_id_resets``
   * - Section 6.1.6, SOMEIP_ETS_095
     - TC8-SDLC-019
     - ``sd_ttl_expiry``
     - ``test_service_discovery::TestSDSubscribeLifecycleAdvanced::test_ets_095_subscribe_ttl_expires_no_events``
   * - Section 6.1.6, SOMEIP_ETS_105
     - TC8-SDLC-020
     - ``sd_sub_lifecycle``
     - NOT IMPLEMENTED
   * - Section 6.1.6, SOMEIP_ETS_106
     - TC8-SDLC-021
     - ``sd_sub_lifecycle``
     - NOT IMPLEMENTED
   * - Section 6.1.6, SOMEIP_ETS_121
     - TC8-SDLC-022
     - ``fld_initial_value``
     - NOT IMPLEMENTED
   * - Section 6.1.6, SOMEIP_ETS_173
     - TC8-SDLC-023
     - ``sd_sub_lifecycle``
     - NOT IMPLEMENTED
   * - Section 6.1.6, SOMEIP_ETS_104
     - TC8-SDLC-024
     - ``sd_sub_lifecycle``
     - NOT IMPLEMENTED
   * - Section 6.1.6, SOMEIP_ETS_127
     - TC8-SDLC-025
     - ``sd_find_response``
     - NOT IMPLEMENTED

.. note::

   TC8-SDLC-011 (SOMEIP_ETS_101) is implemented as ``pytest.skip`` because
   stopping the DUT's own OfferService from an external tester requires a
   dedicated reverse-direction SD client target; the current target does not
   include that capability.
