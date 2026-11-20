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
"""Golden byte-vector regression tests for TC8 conformance helpers.

Pins the exact wire-format bytes ``message_builder.py``, ``sd_sender.py``,
and ``sd_malformed.py`` must produce, guarding against accidental changes to
packet structure. Session-id counters in those modules are reset before each
call that depends on them, so the golden bytes stay reproducible.
"""

from attribute_plugin import add_test_properties
import itertools

import helpers.sd_malformed as sd_malformed
import helpers.sd_sender as sd_sender
from helpers.message_builder import (
    build_notification_as_request,
    build_oversized_message,
    build_request,
    build_request_no_return,
    build_request_with_return_code,
    build_truncated_message,
    build_wrong_protocol_version_request,
)
from helpers.sd_sender import (
    L4Protocols,
    send_find_service,
    send_subscribe_eventgroup,
    send_subscribe_eventgroup_reserved_set,
)
from helpers.someip_types import SOMEIPHeader, SOMEIPMessageType, SOMEIPReturnCode

_DEST = ("127.0.0.1", 30490)


class _FakeSocket:
    """Minimal stand-in for ``socket.socket`` capturing the last ``sendto`` call.

    ``sd_sender`` and ``sd_malformed`` builders only ever call ``sendto`` once
    per invocation in this test's usage, so a single-slot capture is enough.
    """

    def __init__(self) -> None:
        self.sent: tuple[bytes, tuple[str, int]] | None = None

    def sendto(self, data: bytes, dest: tuple[str, int]) -> None:
        self.sent = (bytes(data), dest)


def _reset_sd_sender_counter() -> None:
    """Reset ``sd_sender``'s private session counter so golden bytes are reproducible."""
    sd_sender._session_counter = itertools.count(start=1)  # noqa: SLF001


def _reset_sd_malformed_counter() -> None:
    """Reset ``sd_malformed``'s private session counter so golden bytes are reproducible."""
    sd_malformed._malformed_session = itertools.count(start=200)  # noqa: SLF001


# ---------------------------------------------------------------------------
# helpers/message_builder.py
# ---------------------------------------------------------------------------


class TestMessageBuilderGoldenVectors:
    """Golden byte vectors for every public builder in ``message_builder.py``."""

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_build_request_empty_payload(self) -> None:
        """Check build request empty payload."""
        raw = build_request(0x1234, 0x0421, client_id=0x0001, session_id=0x0001, interface_version=1, payload=b"")
        assert raw.hex() == "12340421000000080001000101010000"
        parsed, rest = SOMEIPHeader.parse(raw)
        assert rest == b""
        assert parsed.service_id == 0x1234
        assert parsed.method_id == 0x0421
        assert parsed.client_id == 0x0001
        assert parsed.session_id == 0x0001
        assert parsed.message_type == SOMEIPMessageType.REQUEST
        assert parsed.payload == b""

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_build_request_with_payload(self) -> None:
        """Check build request with payload."""
        raw = build_request(
            0x1234,
            0x0421,
            client_id=0x0007,
            session_id=0x0099,
            interface_version=1,
            payload=b"\xde\xad\xbe\xef",
        )
        assert raw.hex() == "123404210000000c0007009901010000deadbeef"
        parsed, rest = SOMEIPHeader.parse(raw)
        assert rest == b""
        assert parsed.client_id == 0x0007
        assert parsed.session_id == 0x0099
        assert parsed.message_type == SOMEIPMessageType.REQUEST
        assert parsed.payload == b"\xde\xad\xbe\xef"

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_build_request_no_return(self) -> None:
        """Check build request no return."""
        raw = build_request_no_return(
            0x1234,
            0x0421,
            client_id=0x0001,
            session_id=0x0001,
            interface_version=1,
            payload=b"\x01\x02",
        )
        assert raw.hex() == "123404210000000a00010001010101000102"
        parsed, rest = SOMEIPHeader.parse(raw)
        assert rest == b""
        assert parsed.message_type == SOMEIPMessageType.REQUEST_NO_RETURN
        assert parsed.payload == b"\x01\x02"

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_build_truncated_message(self) -> None:
        """Check build truncated message."""
        raw = build_truncated_message()
        # Only 7 bytes are returned, one short of the minimum 8-byte SOME/IP header.
        assert raw.hex() == "12340421000000"
        assert len(raw) == 7

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_build_wrong_protocol_version_request(self) -> None:
        """Check build wrong protocol version request."""
        raw = build_wrong_protocol_version_request(
            0x1234, 0x0421, client_id=0x0001, session_id=0x0001, interface_version=1
        )
        # Byte 12 (protocol_version) is patched to 0xff, which is not the valid value 0x01.
        assert raw.hex() == "123404210000000800010001ff010000"

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_build_oversized_message(self) -> None:
        """Check build oversized message."""
        raw = build_oversized_message(0x1234, 0x0421, client_id=0x0001, session_id=0x0001, interface_version=1)
        # The length field claims 0x7ff3 (32755) payload bytes, but the packet is only 16 bytes long.
        assert raw.hex() == "1234042100007ff30001000101010000"

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_build_notification_as_request(self) -> None:
        """Check build notification as request."""
        raw = build_notification_as_request(
            0x1234,
            0x0421,
            client_id=0x0001,
            session_id=0x0001,
            interface_version=1,
            payload=b"\xaa",
        )
        assert raw.hex() == "12340421000000090001000101010200aa"
        # The message is well-formed, but its message_type is NOTIFICATION even though it is
        # sent as a client request, which is not allowed by the SOME/IP spec.
        parsed, rest = SOMEIPHeader.parse(raw)
        assert rest == b""
        assert parsed.message_type == SOMEIPMessageType.NOTIFICATION
        assert parsed.payload == b"\xaa"

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_build_request_with_return_code(self) -> None:
        """Check build request with return code."""
        raw = build_request_with_return_code(
            0x1234,
            0x0421,
            0x05,
            client_id=0x0001,
            session_id=0x0001,
            interface_version=1,
            payload=b"\xbb",
        )
        assert raw.hex() == "12340421000000090001000101010005bb"
        # The message is well-formed, but its return_code is set to a non-zero value even
        # though a REQUEST must always carry return_code E_OK.
        parsed, rest = SOMEIPHeader.parse(raw)
        assert rest == b""
        assert parsed.message_type == SOMEIPMessageType.REQUEST
        assert parsed.return_code == SOMEIPReturnCode.E_NOT_REACHABLE
        assert parsed.payload == b"\xbb"


# ---------------------------------------------------------------------------
# helpers/sd_sender.py
# ---------------------------------------------------------------------------


class TestSdSenderGoldenVectors:
    """Golden byte vectors for the public builders in ``sd_sender.py``.

    All calls pass ``session_id`` explicitly, except
    ``send_subscribe_eventgroup_reserved_set``, whose counter is reset
    before the call instead.
    """

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_send_find_service(self) -> None:
        """Check send find service."""
        sock = _FakeSocket()
        send_find_service(
            sock,
            _DEST,
            0x1234,
            instance_id=0x0001,
            major_version=1,
            minor_version=0,
            session_id=0x0007,
        )
        assert sock.sent is not None
        data, dest = sock.sent
        assert dest == _DEST
        assert data.hex() == "ffff810000000024000000070101020040000000000000100000000012340001010000030000000000000000"
        # Parse the bytes back and check the actual SD entry fields, not just the hex.
        entries = sd_sender._parse_sd_entries(data)  # noqa: SLF001
        assert len(entries) == 1
        entry = entries[0]
        assert entry.sd_type == sd_sender.SOMEIPSDEntryType.FindService
        assert entry.service_id == 0x1234
        assert entry.instance_id == 0x0001
        assert entry.major_version == 1
        assert entry.ttl == 3

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_send_subscribe_eventgroup_single_protocol(self) -> None:
        """Check send subscribe eventgroup single protocol."""
        sock = _FakeSocket()
        send_subscribe_eventgroup(
            sock,
            _DEST,
            0x1234,
            0x0001,
            0x0010,
            1,
            "192.168.1.50",
            subscriber_port=30510,
            ttl=3,
            session_id=0x0008,
            l4proto=L4Protocols.UDP,
        )
        assert sock.sent is not None
        data, _ = sock.sent
        assert (
            data.hex() == "ffff81000000003000000008010102004000000000000010060000101234000101000003"
            "000000100000000c00090400c0a801320011772e"
        )
        # Parse the bytes back and check the actual SD entry and option fields.
        entries = sd_sender._parse_sd_entries(data)  # noqa: SLF001
        assert len(entries) == 1
        entry = entries[0]
        assert entry.sd_type == sd_sender.SOMEIPSDEntryType.Subscribe
        assert entry.service_id == 0x1234
        assert entry.instance_id == 0x0001
        assert entry.minver_or_counter & 0xFFFF == 0x0010  # eventgroup_id
        assert len(entry.options_1) == 1
        option = entry.options_1[0]
        assert isinstance(option, sd_sender.IPv4EndpointOption)
        assert str(option.address) == "192.168.1.50"
        assert option.port == 30510
        assert option.l4proto == L4Protocols.UDP

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_send_subscribe_eventgroup_mixed_protocol(self) -> None:
        """Check send subscribe eventgroup mixed protocol."""
        sock = _FakeSocket()
        send_subscribe_eventgroup(
            sock,
            _DEST,
            0x1234,
            0x0001,
            0x0010,
            1,
            "192.168.1.50",
            ttl=3,
            session_id=0x0009,
            tcp_port=30511,
            udp_port=30512,
        )
        assert sock.sent is not None
        data, _ = sock.sent
        assert (
            data.hex() == "ffff81000000003c00000009010102004000000000000010060000201234000101000003"
            "000000100000001800090400c0a801320011773000090400c0a801320006772f"
        )
        # Parse the bytes back and check both endpoint options were carried through.
        entries = sd_sender._parse_sd_entries(data)  # noqa: SLF001
        assert len(entries) == 1
        entry = entries[0]
        assert len(entry.options_1) == 2
        udp_option, tcp_option = entry.options_1
        assert isinstance(udp_option, sd_sender.IPv4EndpointOption)
        assert udp_option.port == 30512
        assert udp_option.l4proto == L4Protocols.UDP
        assert isinstance(tcp_option, sd_sender.IPv4EndpointOption)
        assert tcp_option.port == 30511
        assert tcp_option.l4proto == L4Protocols.TCP

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_subscribe_eventgroup_reserved_set(self) -> None:
        """Check send subscribe eventgroup reserved set."""
        # Non-deterministic without this reset (finding 1). See module docstring.
        _reset_sd_sender_counter()
        sock = _FakeSocket()
        send_subscribe_eventgroup_reserved_set(
            sock,
            _DEST,
            0x1234,
            0x0001,
            0x0010,
            1,
            "192.168.1.50",
            30510,
            ttl=3,
            reserved_value=0x0F,
        )
        assert sock.sent is not None
        data, _ = sock.sent
        # The 12 reserved bits next to the eventgroup counter are set to 0x0f instead of 0,
        # which the SD entry is not supposed to carry.
        assert (
            data.hex() == "ffff8100000000300000000101010200400000000000001006000010123400010100000300"
            "f000100000000c00090400c0a801320011772e"
        )


# ---------------------------------------------------------------------------
# helpers/sd_sender.py: wire-format parse round trip (internal ``_parse_sd_entries``)
# ---------------------------------------------------------------------------


class TestSdSenderWireFormatParsing:
    """Frozen wire-format byte blobs and their expected parsed results.

    Hardcoded independently so this class validates the parser even if the
    builder tests above are removed.
    """

    _WIRE_SUBSCRIBE_SINGLE_HEX = (
        "ffff81000000003000000008010102004000000000000010060000101234000101000003"
        "000000100000000c00090400c0a801320011772e"
    )
    _WIRE_SUBSCRIBE_MIXED_HEX = (
        "ffff81000000003c00000009010102004000000000000010060000201234000101000003"
        "000000100000001800090400c0a801320011773000090400c0a801320006772f"
    )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_parse_single_protocol_subscribe(self) -> None:
        """Check parse single protocol subscribe."""
        entries = sd_sender._parse_sd_entries(  # noqa: SLF001
            bytes.fromhex(self._WIRE_SUBSCRIBE_SINGLE_HEX)
        )
        assert len(entries) == 1
        entry = entries[0]
        assert entry.sd_type == sd_sender.SOMEIPSDEntryType.Subscribe
        assert entry.service_id == 0x1234
        assert entry.instance_id == 0x0001
        assert len(entry.options_1) == 1
        assert len(entry.options_2) == 0
        option = entry.options_1[0]
        assert isinstance(option, sd_sender.IPv4EndpointOption)
        assert str(option.address) == "192.168.1.50"
        assert option.port == 30510
        assert option.l4proto == L4Protocols.UDP

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_parse_mixed_protocol_subscribe(self) -> None:
        """Check parse mixed protocol subscribe."""
        entries = sd_sender._parse_sd_entries(  # noqa: SLF001
            bytes.fromhex(self._WIRE_SUBSCRIBE_MIXED_HEX)
        )
        assert len(entries) == 1
        entry = entries[0]
        assert len(entry.options_1) == 2
        udp_option, tcp_option = entry.options_1
        assert isinstance(udp_option, sd_sender.IPv4EndpointOption)
        assert udp_option.port == 30512
        assert udp_option.l4proto == L4Protocols.UDP
        assert isinstance(tcp_option, sd_sender.IPv4EndpointOption)
        assert tcp_option.port == 30511
        assert tcp_option.l4proto == L4Protocols.TCP


# ---------------------------------------------------------------------------
# helpers/sd_malformed.py: deterministic low-level builders
# ---------------------------------------------------------------------------


class TestSdMalformedPureBuilders:
    """Golden byte vectors for the fully deterministic (no counter) builders."""

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_build_raw_sd_packet_empty(self) -> None:
        """Check build raw sd packet empty."""
        # No entries and no options at all: the shortest possible SD packet.
        pkt = sd_malformed.build_raw_sd_packet(session_id=0x0042)
        assert pkt.hex() == "ffff8100000000140000004201010200c00000000000000000000000"

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_find_service_entry_bytes(self) -> None:
        """Check find service entry bytes."""
        entry = sd_malformed._find_service_entry_bytes(0x1234)  # noqa: SLF001
        assert entry.hex() == "000000001234ffffff000003ffffffff"

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_subscribe_entry_bytes(self) -> None:
        """Check subscribe entry bytes."""
        entry = sd_malformed._subscribe_entry_bytes(0x1234, 0x0001, 0x0010)  # noqa: SLF001
        assert entry.hex() == "06000010123400010000000300000010"

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_endpoint_option_bytes(self) -> None:
        """Check endpoint option bytes."""
        opt = sd_malformed._endpoint_option_bytes("192.168.1.50", 30510)  # noqa: SLF001
        assert opt.hex() == "00090400c0a801320011772e"

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_unknown_option_bytes(self) -> None:
        """Check unknown option bytes."""
        # Option type byte 0x77 is not a defined SD option type.
        opt = sd_malformed._unknown_option_bytes(0x77, 4)  # noqa: SLF001
        assert opt.hex() == "0005770000000000"

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_build_raw_sd_packet_with_length_override(self) -> None:
        """Check build raw sd packet with length override."""
        # The entries length field claims 999 bytes, but the actual entry is only 16 bytes.
        entry = sd_malformed._find_service_entry_bytes(0x1234)  # noqa: SLF001
        pkt = sd_malformed.build_raw_sd_packet(
            entries_bytes=entry,
            options_bytes=b"",
            session_id=0x0043,
            entries_length_override=999,
        )
        assert pkt.hex() == ("ffff8100000000240000004301010200c0000000000003e7000000001234ffffff000003ffffffff00000000")


# ---------------------------------------------------------------------------
# helpers/sd_malformed.py: public send_sd_* wrappers
# ---------------------------------------------------------------------------


class TestSdMalformedSendersGoldenVectors:
    """Golden byte vectors for every public ``send_sd_*`` wrapper.

    ``send_sd_high_session_id`` takes ``session_id`` explicitly; every other
    wrapper relies on the shared, private ``_malformed_session`` counter, so
    each test resets it immediately before the call.
    """

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_high_session_id(self) -> None:
        """Check send sd high session id."""
        # Session id is set close to the 16-bit maximum (0xfffe).
        sock = _FakeSocket()
        sd_malformed.send_sd_high_session_id(sock, _DEST, 0x1234, 0xFFFE)
        assert sock.sent is not None
        data, _ = sock.sent
        assert data.hex() == (
            "ffff8100000000240000fffe01010200c000000000000010000000001234ffffff000003ffffffff00000000"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_empty_entries(self) -> None:
        """Check send sd empty entries."""
        # The entries array is empty: no service entries at all.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_empty_entries(sock, _DEST)
        assert sock.sent[0].hex() == "ffff810000000014000000c801010200c00000000000000000000000"

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="requirements-analysis",
    )
    def test_send_sd_find_with_options(self) -> None:
        """Check send sd find with options."""
        # The FindService entry references an option, but the spec says options
        # attached to a FindService entry must be ignored.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_find_with_options(sock, _DEST, 0x1234, "192.168.1.50", 30510)
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010000000101234ffffff000003ffffffff"
            "0000000c00090400c0a801320011772e"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_entries_length_wrong(self) -> None:
        """Check send sd entries length wrong."""
        # The entries array length field claims 999 bytes, but only one 16-byte entry is sent.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_entries_length_wrong(sock, _DEST, 0x1234, 999)
        assert sock.sent[0].hex() == (
            "ffff810000000024000000c801010200c0000000000003e7000000001234ffffff000003ffffffff00000000"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_entry_refs_more_options(self) -> None:
        """Check send sd entry refs more options."""
        # The entry claims it has 3 options, but only 1 option is actually in the packet.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_entry_refs_more_options(sock, _DEST, 0x1234, 0x0001, 0x0010, 1, "192.168.1.50", 30510)
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010060000301234000101000003"
            "000000100000000c00090400c0a801320011772e"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_entry_unknown_option_type(self) -> None:
        """Check send sd entry unknown option type."""
        # The option type byte is 0x77, which is not a known SD option type.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_entry_unknown_option_type(sock, _DEST, 0x1234, 0x0001, 0x0010)
        assert sock.sent[0].hex() == (
            "ffff81000000002c000000c801010200c00000000000001006000010123400010000000300000010000000080005770000000000"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_entry_same_option_twice(self) -> None:
        """Check send sd entry same option twice."""
        # Both entries point at the same option instead of each having their own.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_entry_same_option_twice(sock, _DEST, 0x1234, 0x0001, 0x0010, "192.168.1.50", 30510)
        assert sock.sent[0].hex() == (
            "ffff810000000040000000c801010200c00000000000002006000010123400010000000300"
            "000010060000101234000100000003000000100000000c00090400c0a801320011772e"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_option_length_too_long(self) -> None:
        """Check send sd option length too long."""
        # The option length field claims 0xff bytes, far more than the option itself carries.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_option_length_too_long(sock, _DEST, 0x1234, 0x0001, 0x0010, "192.168.1.50", 30510, 0x00FF)
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010060000101234000100000003"
            "000000100000000c00ff0400c0a801320011772e"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_option_length_too_short(self) -> None:
        """Check send sd option length too short."""
        # The option length field is set to 1, too small to hold even the type byte.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_option_length_too_short(sock, _DEST, 0x1234, 0x0001, 0x0010, "192.168.1.50", 30510)
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010060000101234000100000003"
            "000000100000000c00010400c0a801320011772e"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_option_length_unaligned(self) -> None:
        """Check send sd option length unaligned."""
        # The option length field is 10 instead of the correct 9, one byte off.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_option_length_unaligned(sock, _DEST, 0x1234, 0x0001, 0x0010, "192.168.1.50", 30510)
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010060000101234000100000003"
            "000000100000000c000a0400c0a801320011772e"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_options_array_length_too_long(self) -> None:
        """Check send sd options array length too long."""
        # The options array length field claims 100 bytes, but only 12 bytes of option data
        # are actually sent.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_options_array_length_too_long(sock, _DEST, 0x1234, 0x0001, 0x0010, "192.168.1.50", 30510)
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010060000101234000100000003"
            "000000100000006400090400c0a801320011772e"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_options_array_length_too_short(self) -> None:
        """Check send sd options array length too short."""
        # The options array length field claims only 2 bytes, but 12 bytes of option data
        # are actually sent.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_options_array_length_too_short(sock, _DEST, 0x1234, 0x0001, 0x0010, "192.168.1.50", 30510)
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010060000101234000100000003"
            "000000100000000200090400c0a801320011772e"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_subscribe_empty_options(self) -> None:
        """Check send sd subscribe empty options."""
        # The subscribe entry has empty Options Array.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_subscribe_empty_options(sock, _DEST, 0x1234, 0x0001, 0x0010, 1)
        assert sock.sent[0].hex() == (
            "ffff810000000024000000c801010200c0000000000000100600000012340001010000030000001000000000"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_subscribe_zero_ip(self) -> None:
        """Check send sd subscribe zero ip."""
        # The endpoint option address is 0.0.0.0, which is not a usable address.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_subscribe_zero_ip(sock, _DEST, 0x1234, 0x0001, 0x0010, 1, 30510)
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010060000101234000101000003"
            "000000100000000c00090400000000000011772e"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_subscribe_wrong_l4proto(self) -> None:
        """Check send sd subscribe wrong l4proto."""
        # The l4proto byte is 0x00, which is neither UDP (0x11) nor TCP (0x06).
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_subscribe_wrong_l4proto(
            sock, _DEST, 0x1234, 0x0001, 0x0010, 1, "192.168.1.50", 30510, l4proto=0x00
        )
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010060000101234000101000003"
            "000000100000000c00090400c0a801320000772e"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_subscribe_reserved_option(self) -> None:
        """Check send sd subscribe reserved option."""
        # The option type byte is 0x20, a reserved value the DUT should not see in practice.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_subscribe_reserved_option(sock, _DEST, 0x1234, 0x0001, 0x0010, 1, "192.168.1.50", 30510)
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010060000101234000101000003"
            "000000100000000c000920000000000000000000"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_wrong_someip_length(self) -> None:
        """Check send sd wrong someip length."""
        # The SOME/IP length field is patched to 999, which does not match the actual packet size.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_wrong_someip_length(sock, _DEST, 0x1234, 999)
        assert sock.sent[0].hex() == (
            "ffff8100000003e7000000c801010200c000000000000010000000001234ffffff000003ffffffff00000000"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_wrong_someip_message_id(self) -> None:
        """Check send sd wrong someip message id."""
        # A subscribe whose SOME/IP Method ID is changed from the SD value 0x8100 to 0x8101.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_wrong_someip_message_id(sock, _DEST, 0x1234, 0x5678, 0x0002, 1, "192.168.1.10", 30000)
        assert sock.sent[0].hex() == (
            "ffff810100000030000000c801010200c000000000000010060000101234567801000003000000020000000c00090400c0a8010a00117530"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_truncated_entry(self) -> None:
        """Check send sd truncated entry."""
        # The entry is cut to 8 bytes, half of the required 16 bytes.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_truncated_entry(sock, _DEST, 0x1234)
        assert sock.sent[0].hex() == ("ffff81000000001c000000c801010200c000000000000010000000001234ffff00000000")

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_oversized_entries_length(self) -> None:
        """Check send sd oversized entries length."""
        # The entries length field claims 0xffff bytes, far more than the actual packet.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_oversized_entries_length(sock, _DEST, 0x1234)
        assert sock.sent[0].hex() == (
            "ffff810000000024000000c801010200c00000000000ffff000000001234ffffff000003ffffffff00000000"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_empty_option(self) -> None:
        """Check send sd empty option."""
        # The option length field is set to 1, too short to be a valid option.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_empty_option(sock, _DEST, 0x1234, 0x0001, 0x0010, 1)
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010060000101234000101000003"
            "000000100000000c000104000000000000000000"
        )

    @add_test_properties(
        partially_verifies=["tool_req__tc8_harness_selfcheck"],
        test_type="interface-test",
        derivation_technique="error-guessing",
    )
    def test_send_sd_subscribe_nonexistent_service(self) -> None:
        """Check send sd subscribe nonexistent service."""
        # The subscribe is for service id 0x9999, which the DUT does not offer.
        _reset_sd_malformed_counter()
        sock = _FakeSocket()
        sd_malformed.send_sd_subscribe_nonexistent_service(sock, _DEST, 0x9999, 0x0001, 0x0010, "192.168.1.50", 30510)
        assert sock.sent[0].hex() == (
            "ffff810000000030000000c801010200c000000000000010060000109999000100000003"
            "000000100000000c00090400c0a801320011772e"
        )
