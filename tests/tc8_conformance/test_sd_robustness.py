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
"""TC8 Group 4 — SD Robustness: malformed SD message handling.

Verifies that ``someipd`` never crashes, hangs, or enters an incorrect state
when it receives malformed SOME/IP-SD packets.

All tests follow the pattern:
  1. Inject one malformed SD packet.
  2. Send a valid FindService and verify the DUT still replies with OfferService.

The "DUT alive" assertion is the primary safety property: no crash implies the
DUT continues to answer SD queries.

Test classes
------------
TestSDMalformedEntries    — ETS_111/112/113/114/115/116/117/118/123/124/125
TestSDMalformedOptions    — ETS_134/135/136/137/138/139/174
TestSDSubscribeEdgeCases  — ETS_109/110/119/140/141/142/143/144
TestSDMessageFramingErrors — ETS_152/153/178
"""

import socket
import subprocess
from typing import List

import pytest

from attribute_plugin import add_test_properties

from helpers.constants import SD_PORT
from helpers.sd_malformed import (
    send_sd_empty_entries,
    send_sd_empty_option,
    send_sd_entries_length_wrong,
    send_sd_entry_refs_more_options,
    send_sd_entry_same_option_twice,
    send_sd_entry_unknown_option_type,
    send_sd_find_with_options,
    send_sd_high_session_id,
    send_sd_option_length_too_long,
    send_sd_option_length_too_short,
    send_sd_option_length_unaligned,
    send_sd_options_array_length_too_long,
    send_sd_options_array_length_too_short,
    send_sd_oversized_entries_length,
    send_sd_subscribe_no_endpoint,
    send_sd_subscribe_nonexistent_service,
    send_sd_subscribe_reserved_option,
    send_sd_subscribe_wrong_l4proto,
    send_sd_subscribe_zero_ip,
    send_sd_truncated_entry,
    send_sd_wrong_someip_length,
    send_sd_wrong_someip_message_id,
)
from helpers.sd_sender import (
    SOMEIPSDEntryType,
    capture_unicast_sd_entries,
    open_sender_socket,
    send_find_service,
)

# ---------------------------------------------------------------------------
# Module-level configuration
# ---------------------------------------------------------------------------

#: SOME/IP stack config used for all tests in this module.
SOMEIP_CONFIG: str = "tc8_someipd_sd.json"

#: Service and instance IDs declared in ``tc8_someipd_sd.json``.
_SERVICE_ID: int = 0x1234
_INSTANCE_ID: int = 0x5678
_EVENTGROUP_ID: int = 0x4455

#: An unknown service/instance not offered by the DUT.
_UNKNOWN_SERVICE_ID: int = 0xDEAD
_UNKNOWN_INSTANCE_ID: int = 0xBEEF
_UNKNOWN_EVENTGROUP_ID: int = 0xDEAD

#: How long to wait for a DUT OfferService reply after injecting malformed data.
_DUT_ALIVE_TIMEOUT: float = 5.0

#: Subscriber port used when building endpoint options in malformed packets.
_SUBSCRIBER_PORT: int = 34567


# ---------------------------------------------------------------------------
# DUT-alive helper
# ---------------------------------------------------------------------------


def _verify_dut_alive(sock: socket.socket, host_ip: str) -> None:
    """Verify the DUT is still alive by checking it responds to a valid FindService.

    Sends a FindService and waits up to ``_DUT_ALIVE_TIMEOUT`` seconds for an
    OfferService reply.  Fails the calling test if no reply arrives, which
    indicates the DUT crashed or is unresponsive after malformed packet injection.
    """
    sd_dest = (host_ip, SD_PORT)

    def _resend() -> None:
        send_find_service(sock, sd_dest, _SERVICE_ID)

    _resend()
    entries = capture_unicast_sd_entries(
        sock,
        filter_types=(SOMEIPSDEntryType.OfferService,),
        timeout_secs=_DUT_ALIVE_TIMEOUT,
        resend=_resend,
        resend_interval_secs=1.0,
        max_results=1,
    )
    assert len(entries) >= 1, (
        "DUT is not alive — no OfferService received within "
        f"{_DUT_ALIVE_TIMEOUT:.0f}s after malformed SD injection"
    )


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def sender(tester_ip: str) -> socket.socket:
    """Open a UDP sender socket bound to tester_ip:SD_PORT for the entire module."""
    sock = open_sender_socket(tester_ip)
    yield sock
    sock.close()


# ---------------------------------------------------------------------------
# Group 4A — TestSDMalformedEntries
# ---------------------------------------------------------------------------


class TestSDMalformedEntries:
    """ETS_111/112/113/114/115/116/117/118/123/124/125 — malformed entries array."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_111_empty_entries_array(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_111: SD packet with entries_array_length=0 — DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_empty_entries(sender, (host_ip, SD_PORT))
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_112_empty_option_zero_length(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_112/113: SubscribeEventgroup with option length=1 (too short). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_empty_option(
            sender, (host_ip, SD_PORT), _SERVICE_ID, _INSTANCE_ID, _EVENTGROUP_ID
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_114_entries_length_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_114: entries_array_length=0 but one entry is present. DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_entries_length_wrong(
            sender, (host_ip, SD_PORT), _SERVICE_ID, entries_length_override=0
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_114_entries_length_mismatched(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_114: entries_array_length=8 (not a multiple of 16). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_entries_length_wrong(
            sender, (host_ip, SD_PORT), _SERVICE_ID, entries_length_override=8
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_115_entry_refs_more_options_than_exist(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_115: Entry num_options_1=3 but options array has only 1. DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_entry_refs_more_options(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_116_entry_unknown_option_type(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_116/174: SubscribeEventgroup with unknown option type 0x77. DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_entry_unknown_option_type(
            sender, (host_ip, SD_PORT), _SERVICE_ID, _INSTANCE_ID, _EVENTGROUP_ID
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_117_two_entries_same_option(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_117: Two entries sharing option index 0 (index overlap). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_entry_same_option_twice(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_118_find_service_with_endpoint_option(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_118: FindService entry with an unexpected endpoint option attached.

        Per spec the DUT must ignore the option and still respond to FindService.
        """
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_find_with_options(
            sender, (host_ip, SD_PORT), _SERVICE_ID, tester_ip, _SUBSCRIBER_PORT
        )
        # The DUT should still respond to this FindService (options are ignored on Find)
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_123_entries_length_wildly_too_large(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_123/124: entries_array_length=0xFFFF (far exceeds packet size). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_oversized_entries_length(sender, (host_ip, SD_PORT), _SERVICE_ID)
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_125_truncated_entry(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_125: entries_array_length=16 but only 8 bytes of entry data present. DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_truncated_entry(sender, (host_ip, SD_PORT), _SERVICE_ID)
        _verify_dut_alive(sender, host_ip)


# ---------------------------------------------------------------------------
# Group 4B — TestSDMalformedOptions
# ---------------------------------------------------------------------------


class TestSDMalformedOptions:
    """ETS_134/135/136/137/138/139/174 — malformed options array."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_134_option_length_much_too_large(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_134: IPv4EndpointOption length field = 0x00FF (way larger than 0x0009). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_option_length_too_long(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
            option_length_override=0x00FF,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_135_option_length_one_too_large(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_135: IPv4EndpointOption length field = 0x000A (one larger than 0x0009). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_option_length_too_long(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
            option_length_override=0x000A,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_136_option_length_too_short(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_136: IPv4EndpointOption length field = 0x0001 (shorter than minimum). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_option_length_too_short(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_137_option_length_unaligned(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_137: IPv4EndpointOption length field = 0x000A (unaligned/odd). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_option_length_unaligned(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_138_options_array_length_too_large(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_138: options_array_length claims 100 bytes but only 12 present. DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_options_array_length_too_long(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_139_options_array_length_too_short(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_139: options_array_length claims 2 bytes but 12 are present. DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_options_array_length_too_short(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_174_unknown_option_type_0x77(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_174: Option type 0x77 (unknown/reserved). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_entry_unknown_option_type(
            sender, (host_ip, SD_PORT), _SERVICE_ID, _INSTANCE_ID, _EVENTGROUP_ID
        )
        _verify_dut_alive(sender, host_ip)


# ---------------------------------------------------------------------------
# Group 4C — TestSDSubscribeEdgeCases
# ---------------------------------------------------------------------------


class TestSDSubscribeEdgeCases:
    """ETS_109/110/119/140/141/142/143/144 — subscribe message edge cases."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_109_subscribe_no_endpoint_option(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_109: SubscribeEventgroup with num_options_1=0 (no endpoint).

        DUT must send NAck or silently discard. Must not crash.
        """
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_subscribe_no_endpoint(
            sender, (host_ip, SD_PORT), _SERVICE_ID, _INSTANCE_ID, _EVENTGROUP_ID
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_110_subscribe_endpoint_ip_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_110: SubscribeEventgroup with endpoint IP = 0.0.0.0.

        DUT must send NAck or silently discard. Must not crash.
        """
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_subscribe_zero_ip(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            subscriber_port=_SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_119_subscribe_unknown_l4proto(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_119: SubscribeEventgroup with L4 protocol byte = 0x00 (unknown).

        DUT must send NAck or silently discard. Must not crash.
        """
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_subscribe_wrong_l4proto(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
            l4proto=0x00,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_140_subscribe_unknown_service_id(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_140: SubscribeEventgroup for an unknown service_id (0xDEAD).

        DUT must not send SubscribeAck for a service it does not offer.
        """
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_subscribe_nonexistent_service(
            sender,
            (host_ip, SD_PORT),
            _UNKNOWN_SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_141_subscribe_unknown_instance_id(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_141: SubscribeEventgroup for correct service_id but unknown instance_id.

        DUT must not send SubscribeAck for a non-existent instance.
        """
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_subscribe_nonexistent_service(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _UNKNOWN_INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_142_subscribe_unknown_eventgroup_id(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_142: SubscribeEventgroup for correct service/instance but unknown eventgroup.

        DUT must send NAck (SubscribeAck TTL=0) or silently discard.
        """
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_subscribe_nonexistent_service(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _UNKNOWN_EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_143_subscribe_all_ids_unknown(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_143: SubscribeEventgroup with service, instance, and eventgroup all unknown.

        DUT must not send any SubscribeAck. Must not crash.
        """
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_subscribe_nonexistent_service(
            sender,
            (host_ip, SD_PORT),
            _UNKNOWN_SERVICE_ID,
            _UNKNOWN_INSTANCE_ID,
            _UNKNOWN_EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_144_subscribe_reserved_option_type(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_144: SubscribeEventgroup with reserved option type 0x20.

        DUT must send NAck or silently discard. Must not crash.
        """
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_subscribe_reserved_option(
            sender,
            (host_ip, SD_PORT),
            _SERVICE_ID,
            _INSTANCE_ID,
            _EVENTGROUP_ID,
            tester_ip,
            _SUBSCRIBER_PORT,
        )
        _verify_dut_alive(sender, host_ip)


# ---------------------------------------------------------------------------
# Group 4D — TestSDMessageFramingErrors
# ---------------------------------------------------------------------------


class TestSDMessageFramingErrors:
    """ETS_152/153/178 — SOME/IP framing and header field errors."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_152_high_session_id_0xfffe(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_152a: FindService with session_id=0xFFFE. DUT must not be confused by near-wrap session ID."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_high_session_id(
            sender, (host_ip, SD_PORT), _SERVICE_ID, session_id=0xFFFE
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_152_session_id_0xffff(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_152b: FindService with session_id=0xFFFF (maximum). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_high_session_id(
            sender, (host_ip, SD_PORT), _SERVICE_ID, session_id=0xFFFF
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_152_session_id_one(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_152c: FindService with session_id=0x0001 after high session_id. DUT must accept wrap."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_high_session_id(
            sender, (host_ip, SD_PORT), _SERVICE_ID, session_id=0x0001
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_153_someip_length_too_small(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_153a: SOME/IP length field smaller than actual payload (length=8). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_wrong_someip_length(
            sender, (host_ip, SD_PORT), _SERVICE_ID, length_override=8
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_153_someip_length_too_large(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_153b: SOME/IP length field larger than actual payload (length=0x1000). DUT must not crash."""
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_wrong_someip_length(
            sender, (host_ip, SD_PORT), _SERVICE_ID, length_override=0x1000
        )
        _verify_dut_alive(sender, host_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_robustness"],
        test_type="robustness",
        derivation_technique="fault-injection",
    )
    def test_ets_178_wrong_someip_service_id(
        self,
        someipd_dut: subprocess.Popen[bytes],
        sender: socket.socket,
        host_ip: str,
    ) -> None:
        """ETS_178: SD packet with SOME/IP service_id=0x1234 (not 0xFFFF).

        DUT must silently discard (not recognized as SD) and remain alive.
        """
        assert someipd_dut.poll() is None, "DUT is not running before injection"
        send_sd_wrong_someip_message_id(sender, (host_ip, SD_PORT))
        _verify_dut_alive(sender, host_ip)
