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

"""Host unit tests for the SOME/IP + SD peer codec and validation logic."""

import socket
import struct
from typing import Iterable

import pytest

from event_exchange import someip_peer
from event_exchange.someip_peer import PeerConfig, SomeIpHostPeer, build_ping_body, parse_ping_body


class _FakeRecvSocket:
    """Deterministic UDP socket stub that yields pre-queued frames then times out."""

    def __init__(self, frames: Iterable[bytes]):
        self._frames = list(frames)
        self.sent: list[tuple[bytes, tuple[str, int]]] = []
        self.closed = False

    def recvfrom(self, _bufsize: int) -> tuple[bytes, tuple[str, int]]:
        if self._frames:
            return self._frames.pop(0), ("169.254.158.190", 30490)
        raise socket.timeout()

    def sendto(self, data: bytes, addr: tuple[str, int]) -> int:
        self.sent.append((bytes(data), addr))
        return len(data)

    def settimeout(self, _timeout: float) -> None:
        pass

    def close(self) -> None:
        self.closed = True


def _make_sd_datagram_from_raw_entry(raw_entry: bytes, options: bytes = b"") -> bytes:
    """Build a canonical SOME/IP-SD UDP datagram from hand-constructed entry bytes."""
    sd_payload = b"\x40\x00\x00\x00" + struct.pack("!I", len(raw_entry)) + raw_entry
    sd_payload += struct.pack("!I", len(options)) + options
    header = struct.pack("!HHIHHBBBB", 0xFFFF, 0x8100, len(sd_payload) + 8, 0, 1, 1, 1, 0x02, 0)
    return header + sd_payload


def _raw_ipv4_udp_option(ip: str, port: int) -> bytes:
    """Construct a 12-byte SOME/IP-SD IPv4 UDP endpoint option from wire spec."""
    return struct.pack("!HBB4sBBH", 9, 0x04, 0x00, socket.inet_aton(ip), 0x00, 17, port)


def _raw_offer_entry(service_id: int, instance_id: int, major_version: int, ttl: int) -> bytes:
    """Construct a 16-byte SOME/IP-SD OfferService entry referencing 1 option at index 0."""
    return (
        struct.pack("!BBBBHHB", 0x01, 0, 0, 0x10, service_id, instance_id, major_version)
        + ttl.to_bytes(3, "big")
        + b"\x00\x00\x00\x00"
    )


@pytest.mark.parametrize(
    ("entry_type", "counter", "expected_wire_bytes", "expected_counter"),
    [
        (someip_peer.SD_SUBSCRIBE, 0x0, b"\x00\x00", 0x0),
        (someip_peer.SD_SUBSCRIBE_ACK, 0x0, b"\x00\x00", 0x0),
        (someip_peer.SD_SUBSCRIBE, 0xA, b"\x00\x0a", 0xA),
        (someip_peer.SD_SUBSCRIBE_ACK, 0xA, b"\x00\x0a", 0xA),
        (someip_peer.SD_SUBSCRIBE, 0xF, b"\x00\x0f", 0xF),
        (someip_peer.SD_SUBSCRIBE_ACK, 0x1A, b"\x00\x0a", 0xA),
    ],
    ids=[
        "subscribe_counter_0",
        "subscribe_ack_counter_0",
        "subscribe_counter_a",
        "subscribe_ack_counter_a",
        "subscribe_counter_max_nibble",
        "subscribe_ack_counter_masked",
    ],
)
def test_sd_eventgroup_counter_wire_layout_and_parse(
    entry_type: int,
    counter: int,
    expected_wire_bytes: bytes,
    expected_counter: int,
) -> None:
    """Eventgroup counter lives in the low nibble of bytes 12..13 per SOME/IP-SD."""
    entry = someip_peer._build_sd_entry(
        entry_type,
        service_id=0x1235,
        instance_id=1,
        major_version=1,
        ttl=120,
        eventgroup_id=0x8002,
        eventgroup_counter=counter,
    )
    assert entry[12:14] == expected_wire_bytes
    assert entry[14:16] == b"\x80\x02"

    parsed = someip_peer._parse_sd_datagram(_make_sd_datagram_from_raw_entry(entry))
    assert len(parsed) == 1
    assert parsed[0].entry_type == entry_type
    assert parsed[0].eventgroup_counter == expected_counter
    assert parsed[0].eventgroup_id == 0x8002


@pytest.mark.parametrize(
    ("reserved_and_counter_bytes", "expected_counter"),
    [
        (b"\x00\x0a", 0xA),
        (b"\x00\x10", None),
        (b"\xa0\x00", None),
    ],
    ids=[
        "valid_low_nibble_0x000a",
        "nonzero_reserved_bit_0x0010",
        "inverted_high_nibble_0xa000",
    ],
)
def test_parse_sd_datagram_rejects_nonzero_reserved_bits(
    reserved_and_counter_bytes: bytes,
    expected_counter: int | None,
) -> None:
    """Subscribe/Ack entries with any non-zero upper 12 reserved bits are rejected."""
    raw_entry = (
        bytes([0x06, 0x00, 0x00, 0x00])
        + struct.pack("!HHB", 0x1235, 0x0001, 0x01)
        + (120).to_bytes(3, "big")
        + reserved_and_counter_bytes
        + struct.pack("!H", 0x8002)
    )
    parsed = someip_peer._parse_sd_datagram(_make_sd_datagram_from_raw_entry(raw_entry))
    if expected_counter is None:
        assert parsed == []
    else:
        assert len(parsed) == 1
        assert parsed[0].eventgroup_counter == expected_counter


def test_await_dut_offer_filters_stop_offer_and_wrong_major_version() -> None:
    """_await_dut_offer ignores TTL=0 StopOffer and major_version mismatches."""
    peer = SomeIpHostPeer(PeerConfig(host_ip="127.0.0.1", dut_ip="127.0.0.1", sd_timeout_s=0.3))
    opt = _raw_ipv4_udp_option("169.254.158.190", 31234)
    stop_offer = _make_sd_datagram_from_raw_entry(_raw_offer_entry(0x1234, 1, major_version=1, ttl=0), opt)
    wrong_major = _make_sd_datagram_from_raw_entry(_raw_offer_entry(0x1234, 1, major_version=2, ttl=120), opt)
    valid_offer = _make_sd_datagram_from_raw_entry(_raw_offer_entry(0x1234, 1, major_version=1, ttl=120), opt)

    peer._sd_mcast_sock = _FakeRecvSocket([stop_offer, wrong_major, valid_offer])
    assert peer._await_dut_offer() == ("169.254.158.190", 31234)


def test_await_subscribe_ack_filters_nack_and_wrong_major_version() -> None:
    """_await_subscribe_ack ignores TTL=0 NACK and major_version mismatches."""
    peer = SomeIpHostPeer(PeerConfig(host_ip="127.0.0.1", dut_ip="127.0.0.1"))
    peer._subscribe_acks.put(someip_peer._SdEntry(someip_peer.SD_SUBSCRIBE_ACK, 0x1234, 1, 1, 0, 0x8001, 0))
    peer._subscribe_acks.put(someip_peer._SdEntry(someip_peer.SD_SUBSCRIBE_ACK, 0x1234, 1, 2, 120, 0x8001, 0))
    assert not peer._await_subscribe_ack(timeout_s=0.1)
    assert not peer.wait_for_subscribe_ack(timeout_s=0.0)

    peer._subscribe_acks.put(someip_peer._SdEntry(someip_peer.SD_SUBSCRIBE_ACK, 0x1234, 1, 1, 120, 0x8001, 0))
    assert peer._await_subscribe_ack(timeout_s=0.2)
    assert peer.wait_for_subscribe_ack(timeout_s=0.0)


def test_dut_to_host_subscribe_retries_increment_session_id() -> None:
    """Each SubscribeEventgroup retry carries a fresh monotonically increasing SOME/IP session ID."""
    peer = SomeIpHostPeer(
        PeerConfig(
            host_ip="127.0.0.1",
            dut_ip="127.0.0.1",
            sd_timeout_s=0.12,
            subscribe_retry_s=0.03,
        )
    )
    opt = _raw_ipv4_udp_option("169.254.158.190", 31234)
    valid_offer = _make_sd_datagram_from_raw_entry(_raw_offer_entry(0x1234, 1, major_version=1, ttl=120), opt)
    fake_unicast = _FakeRecvSocket([])
    peer._sd_mcast_sock = _FakeRecvSocket([valid_offer])
    peer._sd_unicast_sock = fake_unicast

    peer._dut_to_host_loop()

    assert len(fake_unicast.sent) >= 2
    session_ids = [struct.unpack_from("!H", pkt, 10)[0] for pkt, _ in fake_unicast.sent]
    assert session_ids == list(range(1, len(session_ids) + 1))


def test_offer_until_dut_subscribes_filters_stop_subscribe_and_wrong_major_version() -> None:
    """_offer_until_dut_subscribes ignores TTL=0 StopSubscribe and major_version mismatches."""
    peer = SomeIpHostPeer(PeerConfig(host_ip="127.0.0.1", dut_ip="127.0.0.1", sd_timeout_s=0.3))
    peer._send_offer = lambda: None
    ep = (("169.254.158.190", someip_peer.UDP_PROTOCOL, 31235),)

    peer._subscriptions.put(someip_peer._SdEntry(someip_peer.SD_SUBSCRIBE, 0x1235, 1, 1, 0, 0x8002, 1, ep))
    peer._subscriptions.put(someip_peer._SdEntry(someip_peer.SD_SUBSCRIBE, 0x1235, 1, 2, 120, 0x8002, 2, ep))
    peer._subscriptions.put(someip_peer._SdEntry(someip_peer.SD_SUBSCRIBE, 0x1235, 1, 1, 120, 0x8002, 5, ep))

    assert peer._offer_until_dut_subscribes() == ("169.254.158.190", 31235, 5)


@pytest.mark.parametrize(
    ("packet", "expected_iface_ver"),
    [
        (b"\x12\x34" * 7 + b"\x00", 1),
        (struct.pack("!HHIHHBBBB", 0x1234, 0x8001, 7, 0, 1, 1, 1, 0x02, 0), 1),
        (struct.pack("!HHIHHBBBB", 0x1234, 0x8001, 12, 0, 1, 1, 1, 0x02, 0) + b"\x01\x02", 1),
        (struct.pack("!HHIHHBBBB", 0x1234, 0x8001, 8, 0, 1, 2, 1, 0x02, 0), 1),
        (struct.pack("!HHIHHBBBB", 0x1234, 0x8001, 8, 0, 1, 1, 2, 0x02, 0), 1),
        (struct.pack("!HHIHHBBBB", 0x1234, 0x8001, 8, 0, 1, 1, 1, 0x02, 0x02), 1),
    ],
    ids=[
        "short_15_bytes",
        "length_below_minimum",
        "truncated_payload",
        "bad_protocol_version",
        "bad_interface_version",
        "nonzero_return_code",
    ],
)
def test_parse_someip_rejects_malformed_frames(packet: bytes, expected_iface_ver: int) -> None:
    """Malformed headers, bad versions, and non-zero return codes raise ValueError."""
    with pytest.raises(ValueError):
        someip_peer._parse_someip(packet, expected_interface_version=expected_iface_ver)


@pytest.mark.parametrize(
    "bad_frame",
    [
        b"\x12\x34\x80\x01",
        struct.pack("!HHIHHBBBB", 0x9999, 0x8001, 72, 0, 1, 1, 1, 0x02, 0) + build_ping_body(0),
        struct.pack("!HHIHHBBBB", 0x1234, 0x8001, 72, 0, 1, 1, 2, 0x02, 0) + build_ping_body(0),
        struct.pack("!HHIHHBBBB", 0x1234, 0x8001, 72, 0, 1, 1, 1, 0x02, 0x01) + build_ping_body(0),
        struct.pack("!HHIHHBBBB", 0x1234, 0x8001, 72, 0, 1, 1, 1, 0x02, 0)
        + build_ping_body(0)[:-1]
        + bytes([build_ping_body(0)[-1] ^ 0xFF]),
    ],
    ids=[
        "truncated_frame",
        "unexpected_service_id",
        "wrong_interface_version",
        "nonzero_return_code",
        "corrupted_payload_byte",
    ],
)
def test_collect_dut_notifications_records_error_and_stops(bad_frame: bytes) -> None:
    """Invalid frames and corrupted payloads record an error and stop collection."""
    peer = SomeIpHostPeer(
        PeerConfig(host_ip="127.0.0.1", dut_ip="127.0.0.1", tx_expected_samples=2, tx_collect_timeout_s=0.5)
    )
    good_followup = struct.pack("!HHIHHBBBB", 0x1234, 0x8001, 72, 0, 1, 1, 1, 0x02, 0) + build_ping_body(0)
    peer._rx_notif_sock = _FakeRecvSocket([bad_frame, good_followup])

    peer._collect_dut_notifications()

    assert peer.errors
    assert peer.dut_to_host_valid_samples == []
    assert peer.wait_for_tx_samples(timeout_s=0.0)


def test_collect_dut_notifications_accepts_major_version_and_reports_deadline() -> None:
    """Configured major_version!=1 notifications are accepted and incomplete counts fail on deadline."""
    peer = SomeIpHostPeer(
        PeerConfig(
            host_ip="127.0.0.1",
            dut_ip="127.0.0.1",
            major_version=2,
            tx_expected_samples=2,
            tx_collect_timeout_s=0.03,
        )
    )
    frame_v2 = struct.pack("!HHIHHBBBB", 0x1234, 0x8001, 72, 0, 1, 1, 2, 0x02, 0) + build_ping_body(0)
    peer._rx_notif_sock = _FakeRecvSocket([frame_v2])

    peer._collect_dut_notifications()

    assert peer.dut_to_host_valid_samples == [0]
    assert peer.errors == ["only collected 1/2 DUT notifications before deadline"]
    assert peer.wait_for_tx_samples(timeout_s=0.0)


def test_ping_body_detects_single_byte_corruption_and_wrong_counter() -> None:
    """parse_ping_body matches exact pinned wire bytes and rejects altered bytes or wrong sequence."""
    body0 = build_ping_body(0)
    assert len(body0) == 64
    assert body0[:16] == b"\xd6\xb1\x00\x17\x48\x43\x58\x45\x00\x00\x00\x00\x00\x00\x00\x00"
    assert body0[16:20] == b"\x00\x01\x02\x03"
    assert parse_ping_body(body0, 0) == 0
    assert parse_ping_body(body0, 1) is None
    assert parse_ping_body(body0[:-1] + bytes([body0[-1] ^ 0x01]), 0) is None


def test_start_closes_already_opened_sockets_on_partial_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    """A bind failure mid-start closes previously opened sockets and leaves clean state."""
    fake_mcast = _FakeRecvSocket([])
    monkeypatch.setattr(someip_peer, "_multicast_socket", lambda _ip: fake_mcast)

    def _fail_unicast(_ip: str) -> socket.socket:
        raise OSError("simulated bind failure")

    monkeypatch.setattr(someip_peer, "_unicast_sd_socket", _fail_unicast)

    peer = SomeIpHostPeer(PeerConfig(host_ip="127.0.0.1", dut_ip="127.0.0.1"))
    with pytest.raises(OSError, match="simulated bind failure"):
        peer.start()

    assert fake_mcast.closed
    assert peer._sd_mcast_sock is None
    assert peer._sd_unicast_sock is None
    assert peer._threads == []
