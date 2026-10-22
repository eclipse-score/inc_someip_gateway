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
"""TC8-SDX-001 to SDX-021: malformed SubscribeEventgroup messages (arrays, options, lengths, message ID).

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import socket
import struct
import subprocess
from typing import Callable, List

import pytest
from attribute_plugin import add_test_properties

from helpers.constants import EVENTGROUP_ALL_EVENTS_AND_FIELDS, INSTANCE_ID, MAJOR_VERSION, SD_PORT, SERVICE_ID
from helpers.sd_malformed import (
    _endpoint_option_bytes,
    _subscribe_entry_bytes,
    _unknown_option_bytes,
    build_raw_sd_packet,
    send_sd_entry_refs_more_options,
    send_sd_subscribe_empty_options,
    send_sd_wrong_someip_message_id,
)
from helpers.sd_helpers import SUBSCRIBE_ANSWER_SECS, assert_nack, capture_subscribe_answers
from helpers.sd_sender import capture_unicast_sd_entries, open_sender_socket, send_subscribe_eventgroup
from helpers.someip_types import SOMEIPSDEntry

_ENTRY_LEN: int = 16
_UNUSED_DATA: bytes = bytes.fromhex("30303a3031")


def _subscribe_entry() -> bytes:
    return _subscribe_entry_bytes(SERVICE_ID, INSTANCE_ID, EVENTGROUP_ALL_EVENTS_AND_FIELDS, MAJOR_VERSION, num_opts=1)


def _subscribe_entry_two_options() -> bytes:
    return _subscribe_entry_bytes(SERVICE_ID, INSTANCE_ID, EVENTGROUP_ALL_EVENTS_AND_FIELDS, MAJOR_VERSION, num_opts=2)


def _someip_length(pkt: bytes) -> int:
    return struct.unpack_from(">I", pkt, 4)[0]


def _assert_nack_or_ignore(ets: str, answers: List[SOMEIPSDEntry], dut_ip: str, tester_ip: str) -> None:
    """Assert that the DUT sent no Ack, then that a valid Subscribe is still acknowledged (DUT alive)."""
    assert not any(e.ttl > 0 for e in answers), f"{ets}: SubscribeEventgroupAck received, expected NAck or no reaction"
    sock = open_sender_socket(tester_ip)
    try:
        port = sock.getsockname()[1]

        def _subscribe() -> None:
            send_subscribe_eventgroup(
                sock,
                (dut_ip, SD_PORT),
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_ALL_EVENTS_AND_FIELDS,
                MAJOR_VERSION,
                tester_ip,
                subscriber_port=port,
            )

        probe = capture_subscribe_answers(sock, _subscribe)
    finally:
        sock.close()
    assert any(e.ttl > 0 for e in probe), f"{ets}: DUT does not acknowledge a valid Subscribe afterwards"


def _assert_ack(ets: str, answers: List[SOMEIPSDEntry]) -> None:
    """Assert a SubscribeEventgroupAck (TTL > 0)."""
    assert any(e.ttl > 0 for e in answers), (
        f"{ets}: No SubscribeEventgroupAck received within {SUBSCRIBE_ANSWER_SECS:.0f} s"
    )


def _assert_ack_or_ignore(ets: str, answers: List[SOMEIPSDEntry]) -> None:
    """Assert that the DUT either sent an Ack or did not react, and sent no NAck."""
    assert not any(e.ttl == 0 for e in answers), f"{ets}: SubscribeEventgroupNAck received, expected Ack or no reaction"


class TestSDMalformedArrays:
    """SubscribeEventgroup messages with malformed Entries Array, Options Array or option references."""

    def _answers(self, dut_ip: str, tester_ip: str, build: Callable[[int], bytes]) -> List[SOMEIPSDEntry]:
        """Send the datagram returned by *build(sender_port)* and return the Ack and NAck entries received."""
        sock = open_sender_socket(tester_ip)
        try:
            pkt = build(sock.getsockname()[1])
            return capture_subscribe_answers(sock, lambda: sock.sendto(pkt, (dut_ip, SD_PORT)))
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_111_empty_entries_array(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDX-001 (ETS_111): Subscribe stating Entries Array length 0 is ignored, no reaction."""
        assert dut.poll() is None, "DUT is not running"
        sock = open_sender_socket(tester_ip)
        try:
            options = _endpoint_option_bytes(tester_ip, sock.getsockname()[1])
            pkt = build_raw_sd_packet(
                entries_bytes=_subscribe_entry(), options_bytes=options, entries_length_override=0
            )
            sock.sendto(pkt, (dut_ip, SD_PORT))
            received = capture_unicast_sd_entries(sock, timeout_secs=SUBSCRIBE_ANSWER_SECS)
            assert not received, f"ETS_111: DUT reacted to the SubscribeEventgroup: {received}"
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_112_empty_option(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDX-002 (ETS_112): Subscribe with IPv4 Option length 0 is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"
        answers = self._answers(
            dut_ip,
            tester_ip,
            lambda port: build_raw_sd_packet(
                entries_bytes=_subscribe_entry(),
                options_bytes=_endpoint_option_bytes(tester_ip, port, length_override=0),
            ),
        )
        assert_nack("ETS_112", answers)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_113_empty_options_array(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDX-003 (ETS_113): Subscribe with Options Array length 0 is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"
        sock = open_sender_socket(tester_ip)
        try:

            def _send() -> None:
                send_sd_subscribe_empty_options(
                    sock, (dut_ip, SD_PORT), SERVICE_ID, INSTANCE_ID, EVENTGROUP_ALL_EVENTS_AND_FIELDS
                )

            assert_nack("ETS_113", capture_subscribe_answers(sock, _send))
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_114_entries_length_wrong_combined(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-004 (ETS_114): Subscribe with two correct entries and a shortened Entries Array length gets a NAck."""
        assert dut.poll() is None, "DUT is not running"
        answers = self._answers(
            dut_ip,
            tester_ip,
            lambda port: build_raw_sd_packet(
                entries_bytes=_subscribe_entry() * 2,
                options_bytes=_endpoint_option_bytes(tester_ip, port),
                entries_length_override=_ENTRY_LEN,
            ),
        )
        assert_nack("ETS_114", answers)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_115_entry_references_more_options_than_exist(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-005 (ETS_115): Subscribe referencing more options than exist is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"
        sock = open_sender_socket(tester_ip)
        try:
            port = sock.getsockname()[1]

            def _send() -> None:
                send_sd_entry_refs_more_options(
                    sock, (dut_ip, SD_PORT), SERVICE_ID, INSTANCE_ID, EVENTGROUP_ALL_EVENTS_AND_FIELDS, tester_ip, port
                )

            assert_nack("ETS_115", capture_subscribe_answers(sock, _send))
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_116_entry_references_non_existing_option_type(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-006 (ETS_116): Subscribe referencing an option of unknown type 0x77 is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"
        answers = self._answers(
            dut_ip,
            tester_ip,
            lambda _port: build_raw_sd_packet(
                entries_bytes=_subscribe_entry(), options_bytes=_unknown_option_bytes(option_type=0x77)
            ),
        )
        assert_nack("ETS_116", answers)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_117_entry_references_options_of_same_kind(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-007 (ETS_117): Subscribe with two options of the same type gets a NAck or no reaction."""
        assert dut.poll() is None, "DUT is not running"
        entry = _subscribe_entry_bytes(
            SERVICE_ID, INSTANCE_ID, EVENTGROUP_ALL_EVENTS_AND_FIELDS, MAJOR_VERSION, num_opts=2
        )
        answers = self._answers(
            dut_ip,
            tester_ip,
            lambda port: build_raw_sd_packet(
                entries_bytes=entry, options_bytes=_endpoint_option_bytes(tester_ip, port) * 2
            ),
        )
        _assert_nack_or_ignore("ETS_117", answers, dut_ip, tester_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason=(
            "TC8-SDX-008 / OA ETS_123: a SubscribeEventgroup with an Entries Array length exceeding the total length of the message is expected to be "
            "answered with a SubscribeEventgroupNAck. Observed: no NAck is received"
        ),
    )
    def test_ets_123_length_of_entry_array_longer_than_message_allows(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-008 (ETS_123): Subscribe with an Entries Array length above the message length gets a NAck."""
        assert dut.poll() is None, "DUT is not running"

        def _build(port: int) -> bytes:
            options = _endpoint_option_bytes(tester_ip, port)
            total = len(build_raw_sd_packet(entries_bytes=_subscribe_entry(), options_bytes=options))
            return build_raw_sd_packet(
                entries_bytes=_subscribe_entry(), options_bytes=options, entries_length_override=total + 1
            )

        assert_nack("ETS_123", self._answers(dut_ip, tester_ip, _build))

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason=(
            "TC8-SDX-009 / OA ETS_124: a SubscribeEventgroup with an Entries Array length exceeding the total length of the message by at least 20 bytes is expected to be "
            "answered with a SubscribeEventgroupNAck. Observed: no NAck is received"
        ),
    )
    def test_ets_124_length_of_entry_array_too_long(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-009 (ETS_124): Subscribe with an Entries Array length 20 bytes above the message length gets a NAck."""
        assert dut.poll() is None, "DUT is not running"

        def _build(port: int) -> bytes:
            options = _endpoint_option_bytes(tester_ip, port)
            total = len(build_raw_sd_packet(entries_bytes=_subscribe_entry(), options_bytes=options))
            return build_raw_sd_packet(
                entries_bytes=_subscribe_entry(), options_bytes=options, entries_length_override=total + 20
            )

        assert_nack("ETS_124", self._answers(dut_ip, tester_ip, _build))

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason=(
            "TC8-SDX-010 / OA ETS_125: a SubscribeEventgroup with an Entries Array length less than the sum of the lengths indicated by each option is expected to be "
            "answered with a SubscribeEventgroupNAck. Observed: no NAck is received"
        ),
    )
    def test_ets_125_length_of_entry_array_too_short(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-010 (ETS_125): Subscribe with a too short Entries Array length is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"
        answers = self._answers(
            dut_ip,
            tester_ip,
            lambda port: build_raw_sd_packet(
                entries_bytes=_subscribe_entry(),
                options_bytes=_endpoint_option_bytes(tester_ip, port),
                entries_length_override=_ENTRY_LEN - 1,
            ),
        )
        assert_nack("ETS_125", answers)


class TestSDMalformedOptionsAndHeader:
    """SubscribeEventgroup messages with malformed option lengths, SOME/IP length, trailing data or message ID."""

    _answers = TestSDMalformedArrays._answers

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_134_option_length_ends_past_options_array_var_a(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-011 (ETS_134): SOME/IP length and Options Array length both cut by 4 gets NAck or no reaction."""
        assert dut.poll() is None, "DUT is not running"

        def _build(port: int) -> bytes:
            options = _endpoint_option_bytes(tester_ip, port)
            actual = _someip_length(build_raw_sd_packet(entries_bytes=_subscribe_entry(), options_bytes=options))
            return build_raw_sd_packet(
                entries_bytes=_subscribe_entry(),
                options_bytes=options,
                options_length_override=len(options) - 4,
                someip_length_override=actual - 4,
            )

        answers = self._answers(dut_ip, tester_ip, _build)
        _assert_nack_or_ignore("ETS_134", answers, dut_ip, tester_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_135_option_length_ends_past_options_array_var_b(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-012 (ETS_135): an option longer than the indicated Options Array length gets NAck or no reaction."""
        assert dut.poll() is None, "DUT is not running"
        answers = self._answers(
            dut_ip,
            tester_ip,
            lambda port: build_raw_sd_packet(
                entries_bytes=_subscribe_entry(),
                options_bytes=_endpoint_option_bytes(tester_ip, port),
                options_length_override=8,
            ),
        )
        _assert_nack_or_ignore("ETS_135", answers, dut_ip, tester_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_136_option_length_shorter_gt_0_as_specified_for_type(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-013 (ETS_136): UDP option length 4 instead of 9 gets NAck or no reaction."""
        assert dut.poll() is None, "DUT is not running"
        answers = self._answers(
            dut_ip,
            tester_ip,
            lambda port: build_raw_sd_packet(
                entries_bytes=_subscribe_entry(),
                options_bytes=_endpoint_option_bytes(tester_ip, port, length_override=4),
            ),
        )
        _assert_nack_or_ignore("ETS_136", answers, dut_ip, tester_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_137_option_shorter_with_unaligned_next_option(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-014 (ETS_137): TCP option length 14 plus UDP option length 4, array length kept, gets a NAck."""
        assert dut.poll() is None, "DUT is not running"

        def _build(port: int) -> bytes:
            tcp = _endpoint_option_bytes(tester_ip, port, l4proto=0x06, length_override=14) + bytes(5)
            udp = _endpoint_option_bytes(tester_ip, port, length_override=4)[:7]
            return build_raw_sd_packet(entries_bytes=_subscribe_entry_two_options(), options_bytes=tcp + udp)

        assert_nack("ETS_137", self._answers(dut_ip, tester_ip, _build))

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_138_options_array_longer_than_message_allows(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-015 (ETS_138): Options Array length 0x28 above the actual length gets an Ack or no reaction."""
        assert dut.poll() is None, "DUT is not running"
        answers = self._answers(
            dut_ip,
            tester_ip,
            lambda port: build_raw_sd_packet(
                entries_bytes=_subscribe_entry(),
                options_bytes=_endpoint_option_bytes(tester_ip, port),
                options_length_override=0x28,
            ),
        )
        _assert_ack_or_ignore("ETS_138", answers)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_139_options_array_too_short(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDX-016 (ETS_139): Options Array length 2 below the actual length gets a NAck."""
        assert dut.poll() is None, "DUT is not running"
        answers = self._answers(
            dut_ip,
            tester_ip,
            lambda port: build_raw_sd_packet(
                entries_bytes=_subscribe_entry(),
                options_bytes=_endpoint_option_bytes(tester_ip, port),
                options_length_override=2,
            ),
        )
        assert_nack("ETS_139", answers)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_153_someip_length_shorter_as_expected(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-017 (ETS_153): SOME/IP length one byte below the actual length gets NAck or no reaction."""
        assert dut.poll() is None, "DUT is not running"

        def _build(port: int) -> bytes:
            options = _endpoint_option_bytes(tester_ip, port)
            actual = _someip_length(build_raw_sd_packet(entries_bytes=_subscribe_entry(), options_bytes=options))
            return build_raw_sd_packet(
                entries_bytes=_subscribe_entry(), options_bytes=options, someip_length_override=actual - 1
            )

        _assert_nack_or_ignore("ETS_153", self._answers(dut_ip, tester_ip, _build), dut_ip, tester_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_174_unknown_option_type(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDX-018 (ETS_174): Subscribe referencing an unknown option type and a UDP option gets a NAck."""
        assert dut.poll() is None, "DUT is not running"
        answers = self._answers(
            dut_ip,
            tester_ip,
            lambda port: build_raw_sd_packet(
                entries_bytes=_subscribe_entry_two_options(),
                options_bytes=_unknown_option_bytes(option_type=0x77, content_len=8)
                + _endpoint_option_bytes(tester_ip, port),
            ),
        )
        assert_nack("ETS_174", answers)

    def _subscribe_with_trailing_data(
        self, dut_ip: str, tester_ip: str, sock: socket.socket, counted: bool
    ) -> List[SOMEIPSDEntry]:
        base = build_raw_sd_packet(
            entries_bytes=_subscribe_entry(),
            options_bytes=_endpoint_option_bytes(tester_ip, sock.getsockname()[1]),
        )
        length = _someip_length(base) + (len(_UNUSED_DATA) if counted else 0)
        pkt = bytearray(base + _UNUSED_DATA)
        struct.pack_into(">I", pkt, 4, length)
        return capture_subscribe_answers(sock, lambda: sock.sendto(bytes(pkt), (dut_ip, SD_PORT)))

    def _stop_subscribe(self, dut_ip: str, tester_ip: str, sock: socket.socket) -> None:
        entry = _subscribe_entry_bytes(SERVICE_ID, INSTANCE_ID, EVENTGROUP_ALL_EVENTS_AND_FIELDS, MAJOR_VERSION, ttl=0)
        options = _endpoint_option_bytes(tester_ip, sock.getsockname()[1])
        sock.sendto(build_raw_sd_packet(entries_bytes=entry, options_bytes=options), (dut_ip, SD_PORT))

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_176_unused_data_after_options_array(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-019 (ETS_176): Subscribe with unused data after the Options Array gets an Ack, counted or not."""
        assert dut.poll() is None, "DUT is not running"
        sock = open_sender_socket(tester_ip)
        try:
            _assert_ack("ETS_176 (counted)", self._subscribe_with_trailing_data(dut_ip, tester_ip, sock, True))
            _assert_ack("ETS_176 (not counted)", self._subscribe_with_trailing_data(dut_ip, tester_ip, sock, False))
        finally:
            self._stop_subscribe(dut_ip, tester_ip, sock)
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_177_unused_data_after_options_array_wrong_length(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-020 (ETS_177): Subscribe with unused data not counted in the SOME/IP length gets an Ack."""
        assert dut.poll() is None, "DUT is not running"
        sock = open_sender_socket(tester_ip)
        try:
            _assert_ack("ETS_177", self._subscribe_with_trailing_data(dut_ip, tester_ip, sock, False))
        finally:
            self._stop_subscribe(dut_ip, tester_ip, sock)
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason=(
            "TC8-SDX-021 / OA ETS_178: a SubscribeEventgroup whose SOME/IP header carries a wrong Message ID for Service Discovery "
            "is expected to be answered with a SubscribeEventgroupNAck. Observed: no NAck is received"
        ),
    )
    def test_ets_178_subscribe_using_wrong_someip_messageid(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDX-021 (ETS_178): Subscribe with a wrong SOME/IP Message ID gets a NAck."""
        assert dut.poll() is None, "DUT is not running"
        sock = open_sender_socket(tester_ip)
        try:
            port = sock.getsockname()[1]

            def _send() -> None:
                send_sd_wrong_someip_message_id(
                    sock, (dut_ip, SD_PORT), SERVICE_ID, INSTANCE_ID, EVENTGROUP_ALL_EVENTS_AND_FIELDS, tester_ip, port
                )

            assert_nack("ETS_178", capture_subscribe_answers(sock, _send))
        finally:
            sock.close()
