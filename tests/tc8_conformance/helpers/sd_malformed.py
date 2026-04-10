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
"""Malformed SD packet builders for TC8 Group 4 robustness tests.

Each public function builds or sends a SOME/IP-SD UDP datagram with a
deliberate protocol violation.  The DUT must survive receipt of any of
these packets without crashing, hanging, or entering an incorrect state.

Wire format reminder
--------------------
SOME/IP header (16 bytes):
  [0-1]   service_id  = 0xFFFF
  [2-3]   method_id   = 0x8100
  [4-7]   length      (big-endian, bytes from byte 8 onward)
  [8-9]   client_id   = 0x0001
  [10-11] session_id  (big-endian)
  [12]    protocol_version  = 0x01
  [13]    interface_version = 0x01
  [14]    message_type      = 0x02 (NOTIFICATION)
  [15]    return_code       = 0x00

SD payload (starts at byte 16):
  [0]    flags    (0xC0 = reboot | unicast)
  [1-3]  reserved (0x000000)
  [4-7]  entries_array_length (big-endian)
  [8+]   entry bytes (entries_array_length bytes)
  [...] options_array_length (4 bytes big-endian)
  [...]  option bytes

SOME/IP length = 8 + len(SD payload).
"""

import ipaddress
import itertools
import socket
import struct
from typing import Optional, Tuple

from helpers.constants import SD_PORT
from someip.header import (
    IPv4EndpointOption,
    L4Protocols,
    SOMEIPSDEntry,
    SOMEIPSDEntryType,
)

# ---------------------------------------------------------------------------
# Module-level session counter (independent of sd_sender's counter)
# ---------------------------------------------------------------------------

_malformed_session: itertools.count = itertools.count(start=200)


def _next_session() -> int:
    """Return next session ID in range 1-65535 (skip 0)."""
    val = next(_malformed_session) & 0xFFFF
    return val or 200  # skip 0


# ---------------------------------------------------------------------------
# Core raw-packet builder
# ---------------------------------------------------------------------------

_SOMEIP_SD_HEADER_FMT = ">HHIHHBBBB"  # 16 bytes total
_SOMEIP_SD_SERVICE_ID = 0xFFFF
_SOMEIP_SD_METHOD_ID = 0x8100
_SD_FLAGS_REBOOT_UNICAST = 0xC0
_SD_FLAGS_NONE = 0x00


def _build_someip_header(session_id: int, payload_len: int) -> bytes:
    """Build the 16-byte SOME/IP header for an SD notification."""
    length = 8 + payload_len
    return struct.pack(
        _SOMEIP_SD_HEADER_FMT,
        _SOMEIP_SD_SERVICE_ID,  # service_id
        _SOMEIP_SD_METHOD_ID,  # method_id
        length,  # length
        0x0001,  # client_id
        session_id,  # session_id
        0x01,  # protocol_version
        0x01,  # interface_version (SD uses 0x01)
        0x02,  # message_type = NOTIFICATION
        0x00,  # return_code = E_OK
    )


def _build_sd_payload(
    flags: int,
    entries_bytes: bytes,
    options_bytes: bytes,
    entries_length_override: Optional[int] = None,
    options_length_override: Optional[int] = None,
) -> bytes:
    """Build the SD payload section (flags + array lengths + entries + options)."""
    entries_len = (
        entries_length_override
        if entries_length_override is not None
        else len(entries_bytes)
    )
    options_len = (
        options_length_override
        if options_length_override is not None
        else len(options_bytes)
    )
    return (
        struct.pack(">B3xI", flags, entries_len)  # flags(1)+reserved(3)+entries_len(4)
        + entries_bytes
        + struct.pack(">I", options_len)
        + options_bytes
    )


def build_raw_sd_packet(
    flags: int = _SD_FLAGS_REBOOT_UNICAST,
    entries_bytes: bytes = b"",
    options_bytes: bytes = b"",
    entries_length_override: Optional[int] = None,
    options_length_override: Optional[int] = None,
    session_id: int = 0,
    someip_length_override: Optional[int] = None,
    someip_service_id_override: Optional[int] = None,
    someip_method_id_override: Optional[int] = None,
) -> bytes:
    """Build a complete SOME/IP+SD packet with optional field overrides.

    Use *_override* parameters to inject specific protocol violations.
    """
    if session_id == 0:
        session_id = _next_session()
    sd_payload = _build_sd_payload(
        flags,
        entries_bytes,
        options_bytes,
        entries_length_override,
        options_length_override,
    )
    raw = bytearray(_build_someip_header(session_id, len(sd_payload)) + sd_payload)
    if someip_length_override is not None:
        struct.pack_into(">I", raw, 4, someip_length_override)
    if someip_service_id_override is not None:
        struct.pack_into(">H", raw, 0, someip_service_id_override)
    if someip_method_id_override is not None:
        struct.pack_into(">H", raw, 2, someip_method_id_override)
    return bytes(raw)


# ---------------------------------------------------------------------------
# SD entry byte builders
# ---------------------------------------------------------------------------


def _find_service_entry_bytes(
    service_id: int,
    instance_id: int = 0xFFFF,
    major_version: int = 0xFF,
    ttl: int = 3,
    minor_version: int = 0xFFFFFFFF,
) -> bytes:
    """Build a 16-byte FindService (Type 0) SD entry."""
    ttl_3b = struct.pack(">I", ttl)[1:]  # 3 bytes big-endian
    return (
        bytes([0x00, 0x00, 0x00, 0x00])
        + struct.pack(">HH", service_id, instance_id)
        + bytes([major_version])
        + ttl_3b
        + struct.pack(">I", minor_version)
    )


def _subscribe_entry_bytes(
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    major_version: int = 0x00,
    ttl: int = 3,
    num_opts: int = 1,
    idx_opt: int = 0,
    reserved: bytes = b"\x00\x00",
) -> bytes:
    """Build a 16-byte SubscribeEventgroup (Type 0x06) SD entry.

    Wire layout (big-endian):
      byte 0:    type = 0x06
      byte 1:    index_first_option_run
      byte 2:    (num_options_1 << 4) | num_options_2
      byte 3:    0 (reserved/service_type)
      bytes 4-5: service_id
      bytes 6-7: instance_id
      byte 8:    major_version
      bytes 9-11: TTL (3 bytes)
      bytes 12-13: reserved (should be 0)
      bytes 14-15: eventgroup_id
    """
    ttl_3b = struct.pack(">I", ttl)[1:]
    return (
        bytes([0x06, idx_opt, (num_opts << 4), 0x00])
        + struct.pack(">HH", service_id, instance_id)
        + bytes([major_version])
        + ttl_3b
        + reserved
        + struct.pack(">H", eventgroup_id & 0xFFFF)
    )


def _endpoint_option_bytes(
    ip: str,
    port: int,
    l4proto: int = 0x11,  # 0x11 = UDP, 0x06 = TCP
    length_override: Optional[int] = None,
) -> bytes:
    """Build a 12-byte IPv4 Endpoint Option.

    Wire layout:
      [0-1]  length = 0x0009
      [2]    type   = 0x04
      [3]    reserved = 0x00
      [4-7]  IPv4 address
      [8]    reserved = 0x00
      [9]    l4proto
      [10-11] port
    """
    length_field = length_override if length_override is not None else 0x0009
    addr_bytes = socket.inet_aton(ip)
    return (
        struct.pack(">HBB", length_field, 0x04, 0x00)
        + addr_bytes
        + struct.pack(">BBH", 0x00, l4proto, port)
    )


def _unknown_option_bytes(option_type: int = 0x77, content_len: int = 4) -> bytes:
    """Build an SD option with an unknown type byte."""
    length_field = content_len + 1  # type byte counts in length
    padding = bytes(content_len)
    return struct.pack(">HBB", length_field, option_type, 0x00) + padding


# ---------------------------------------------------------------------------
# Public malformed-packet senders
# ---------------------------------------------------------------------------


def send_sd_empty_entries(
    sock: socket.socket,
    dest: Tuple[str, int],
) -> None:
    """ETS_111: SD packet with entries_array_length=0 (no entries, no options).

    DUT must not crash or send a spurious OfferService.
    """
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=b"",
        options_bytes=b"",
    )
    sock.sendto(pkt, dest)


def send_sd_find_with_options(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    host_ip: str,
    subscriber_port: int,
) -> None:
    """ETS_118: FindService entry with an endpoint option attached.

    SOME/IP-SD spec says options on FindService entries shall be ignored.
    DUT must still respond to FindService normally.
    """
    # Build FindService entry with num_options_1=1 so the DUT sees an option reference.
    ttl_3b = struct.pack(">I", 3)[1:]
    entry_bytes = (
        bytes(
            [0x00, 0x00, 0x10, 0x00]
        )  # type=Find, idx=0, num_1=1, num_2=0, service_type=0
        + struct.pack(">HH", service_id, 0xFFFF)
        + bytes([0xFF])
        + ttl_3b
        + struct.pack(">I", 0xFFFFFFFF)
    )
    opt_bytes = _endpoint_option_bytes(host_ip, subscriber_port)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=opt_bytes,
    )
    sock.sendto(pkt, dest)


def send_sd_entries_length_wrong(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    entries_length_override: int,
) -> None:
    """ETS_114/123/124/125: SD packet where entries_array_length mismatches actual entry bytes.

    DUT must discard and remain alive.
    """
    entry_bytes = _find_service_entry_bytes(service_id=service_id)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=b"",
        entries_length_override=entries_length_override,
    )
    sock.sendto(pkt, dest)


def send_sd_entry_refs_more_options(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    host_ip: str,
    subscriber_port: int,
) -> None:
    """ETS_115: SubscribeEventgroup entry num_options_1=3 but options array has only 1.

    DUT must discard the subscribe (and may send NAck) but must not crash.
    """
    # Build entry with num_options_1=3 (bits [7:4] of byte 2)
    ttl_3b = struct.pack(">I", 3)[1:]
    entry_bytes = (
        bytes([0x06, 0x00, 0x30, 0x00])  # type=Subscribe, idx=0, num_1=3, num_2=0
        + struct.pack(">HH", service_id, instance_id)
        + bytes([0x00])
        + ttl_3b
        + b"\x00\x00"
        + struct.pack(">H", eventgroup_id & 0xFFFF)
    )
    opt_bytes = _endpoint_option_bytes(host_ip, subscriber_port)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=opt_bytes,
    )
    sock.sendto(pkt, dest)


def send_sd_entry_unknown_option_type(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
) -> None:
    """ETS_116/174: SubscribeEventgroup with an option of unknown type 0x77.

    DUT may send NAck or silently discard — must not crash.
    """
    ttl_3b = struct.pack(">I", 3)[1:]
    entry_bytes = (
        bytes([0x06, 0x00, 0x10, 0x00])  # num_1=1
        + struct.pack(">HH", service_id, instance_id)
        + bytes([0x00])
        + ttl_3b
        + b"\x00\x00"
        + struct.pack(">H", eventgroup_id & 0xFFFF)
    )
    opt_bytes = _unknown_option_bytes(option_type=0x77, content_len=4)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=opt_bytes,
    )
    sock.sendto(pkt, dest)


def send_sd_entry_same_option_twice(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    host_ip: str,
    subscriber_port: int,
) -> None:
    """ETS_117: Two entries pointing to the same endpoint option via option index overlap.

    Builds two SubscribeEventgroup entries both referencing option index 0.
    DUT must not crash.
    """
    ttl_3b = struct.pack(">I", 3)[1:]
    entry1 = (
        bytes([0x06, 0x00, 0x10, 0x00])
        + struct.pack(">HH", service_id, instance_id)
        + bytes([0x00])
        + ttl_3b
        + b"\x00\x00"
        + struct.pack(">H", eventgroup_id & 0xFFFF)
    )
    entry2 = (
        bytes([0x06, 0x00, 0x10, 0x00])  # also references option index 0
        + struct.pack(">HH", service_id, instance_id)
        + bytes([0x00])
        + ttl_3b
        + b"\x00\x00"
        + struct.pack(">H", eventgroup_id & 0xFFFF)
    )
    opt_bytes = _endpoint_option_bytes(host_ip, subscriber_port)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry1 + entry2,
        options_bytes=opt_bytes,
    )
    sock.sendto(pkt, dest)


def send_sd_option_length_too_long(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    host_ip: str,
    subscriber_port: int,
    option_length_override: int,
) -> None:
    """ETS_134/135: IPv4EndpointOption with oversize length field.

    The option length field claims more bytes than the options array contains.
    DUT must discard and remain alive.
    """
    ttl_3b = struct.pack(">I", 3)[1:]
    entry_bytes = (
        bytes([0x06, 0x00, 0x10, 0x00])
        + struct.pack(">HH", service_id, instance_id)
        + bytes([0x00])
        + ttl_3b
        + b"\x00\x00"
        + struct.pack(">H", eventgroup_id & 0xFFFF)
    )
    opt_bytes = _endpoint_option_bytes(
        host_ip, subscriber_port, length_override=option_length_override
    )
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=opt_bytes,
    )
    sock.sendto(pkt, dest)


def send_sd_option_length_too_short(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    host_ip: str,
    subscriber_port: int,
) -> None:
    """ETS_136: IPv4EndpointOption with length field = 1 (too short for actual content).

    DUT must discard and remain alive.
    """
    send_sd_option_length_too_long(
        sock,
        dest,
        service_id,
        instance_id,
        eventgroup_id,
        host_ip,
        subscriber_port,
        option_length_override=0x0001,
    )


def send_sd_option_length_unaligned(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    host_ip: str,
    subscriber_port: int,
) -> None:
    """ETS_137: IPv4EndpointOption with odd length that doesn't align to option boundary.

    Uses length=0x000A (10) instead of 9; points one byte past the type+reserved into
    the next field.  DUT must discard and remain alive.
    """
    send_sd_option_length_too_long(
        sock,
        dest,
        service_id,
        instance_id,
        eventgroup_id,
        host_ip,
        subscriber_port,
        option_length_override=0x000A,
    )


def send_sd_options_array_length_too_long(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    host_ip: str,
    subscriber_port: int,
) -> None:
    """ETS_138: options_array_length claims more bytes than actually present.

    DUT must discard and remain alive.
    """
    ttl_3b = struct.pack(">I", 3)[1:]
    entry_bytes = (
        bytes([0x06, 0x00, 0x10, 0x00])
        + struct.pack(">HH", service_id, instance_id)
        + bytes([0x00])
        + ttl_3b
        + b"\x00\x00"
        + struct.pack(">H", eventgroup_id & 0xFFFF)
    )
    opt_bytes = _endpoint_option_bytes(host_ip, subscriber_port)
    # Override options_array_length to claim 100 bytes, but actual opt_bytes is 12
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=opt_bytes,
        options_length_override=100,
    )
    sock.sendto(pkt, dest)


def send_sd_options_array_length_too_short(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    host_ip: str,
    subscriber_port: int,
) -> None:
    """ETS_139: options_array_length claims fewer bytes than actually present.

    DUT must discard and remain alive.
    """
    ttl_3b = struct.pack(">I", 3)[1:]
    entry_bytes = (
        bytes([0x06, 0x00, 0x10, 0x00])
        + struct.pack(">HH", service_id, instance_id)
        + bytes([0x00])
        + ttl_3b
        + b"\x00\x00"
        + struct.pack(">H", eventgroup_id & 0xFFFF)
    )
    opt_bytes = _endpoint_option_bytes(host_ip, subscriber_port)
    # Override options_array_length to 2 (far fewer bytes than 12 actual)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=opt_bytes,
        options_length_override=2,
    )
    sock.sendto(pkt, dest)


def send_sd_subscribe_no_endpoint(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
) -> None:
    """ETS_109: SubscribeEventgroup with num_options_1=0 (no endpoint option).

    DUT must send NAck (SubscribeAck with TTL=0) or silently discard.
    Must not crash.
    """
    entry_bytes = _subscribe_entry_bytes(
        service_id=service_id,
        instance_id=instance_id,
        eventgroup_id=eventgroup_id,
        num_opts=0,
    )
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=b"",
    )
    sock.sendto(pkt, dest)


def send_sd_subscribe_zero_ip(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    subscriber_port: int,
) -> None:
    """ETS_110: SubscribeEventgroup with endpoint IP = 0.0.0.0 (unspecified).

    DUT must send NAck or silently discard.  Must not crash.
    """
    entry_bytes = _subscribe_entry_bytes(
        service_id=service_id,
        instance_id=instance_id,
        eventgroup_id=eventgroup_id,
        num_opts=1,
    )
    opt_bytes = _endpoint_option_bytes("0.0.0.0", subscriber_port)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=opt_bytes,
    )
    sock.sendto(pkt, dest)


def send_sd_subscribe_wrong_l4proto(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    host_ip: str,
    subscriber_port: int,
    l4proto: int = 0x00,
) -> None:
    """ETS_119: SubscribeEventgroup with unknown L4 protocol byte in endpoint option.

    Uses l4proto=0x00 (neither UDP=0x11 nor TCP=0x06).
    DUT must send NAck or silently discard.  Must not crash.
    """
    entry_bytes = _subscribe_entry_bytes(
        service_id=service_id,
        instance_id=instance_id,
        eventgroup_id=eventgroup_id,
        num_opts=1,
    )
    opt_bytes = _endpoint_option_bytes(host_ip, subscriber_port, l4proto=l4proto)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=opt_bytes,
    )
    sock.sendto(pkt, dest)


def send_sd_subscribe_reserved_option(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    host_ip: str,
    subscriber_port: int,
) -> None:
    """ETS_144: SubscribeEventgroup with a reserved option type (0x20).

    DUT must send NAck or silently discard.  Must not crash.
    """
    entry_bytes = _subscribe_entry_bytes(
        service_id=service_id,
        instance_id=instance_id,
        eventgroup_id=eventgroup_id,
        num_opts=1,
    )
    opt_bytes = _unknown_option_bytes(option_type=0x20, content_len=8)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=opt_bytes,
    )
    sock.sendto(pkt, dest)


def send_sd_wrong_someip_length(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    length_override: int,
) -> None:
    """ETS_153: SOME/IP SD packet where the SOME/IP length field is incorrect.

    DUT must discard and remain alive.
    """
    entry_bytes = _find_service_entry_bytes(service_id=service_id)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=b"",
        someip_length_override=length_override,
    )
    sock.sendto(pkt, dest)


def send_sd_high_session_id(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    session_id: int,
) -> None:
    """ETS_152: SD FindService with a high/wrapped session ID (e.g., 0xFFFE, 0xFFFF).

    DUT must not reject or misinterpret packets based on tester's session ID.
    """
    entry_bytes = _find_service_entry_bytes(service_id=service_id)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=b"",
        session_id=session_id,
    )
    sock.sendto(pkt, dest)


def send_sd_wrong_someip_message_id(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id_override: int = 0x1234,
) -> None:
    """ETS_178: SD packet with wrong SOME/IP service_id (not 0xFFFF).

    DUT must silently discard (not SD traffic) and remain alive.
    """
    entry_bytes = _find_service_entry_bytes(service_id=0x1234)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=b"",
        someip_service_id_override=service_id_override,
    )
    sock.sendto(pkt, dest)


def send_sd_truncated_entry(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
) -> None:
    """ETS_125: SD packet with entries_array_length=16 but only 8 bytes of entry data.

    The entry is incomplete (truncated). DUT must discard and remain alive.
    """
    entry_bytes = _find_service_entry_bytes(service_id=service_id)[
        :8
    ]  # truncate to 8 bytes
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=b"",
        entries_length_override=16,  # claims 16 bytes but only 8 provided
    )
    sock.sendto(pkt, dest)


def send_sd_oversized_entries_length(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
) -> None:
    """ETS_123/124: entries_array_length far exceeds packet size.

    DUT must discard and remain alive.
    """
    entry_bytes = _find_service_entry_bytes(service_id=service_id)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=b"",
        entries_length_override=0xFFFF,  # wildly too large
    )
    sock.sendto(pkt, dest)


def send_sd_empty_option(
    sock: socket.socket,
    dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
) -> None:
    """ETS_112/113: SubscribeEventgroup with an option whose length field is 0 or 1.

    An option with length < 2 is malformed (type byte cannot fit).
    DUT must discard and remain alive.
    """
    entry_bytes = _subscribe_entry_bytes(
        service_id=service_id,
        instance_id=instance_id,
        eventgroup_id=eventgroup_id,
        num_opts=1,
    )
    # Build an option with length=0x0001 (only 1 content byte after length field — invalid)
    opt_bytes = struct.pack(">HBB", 0x0001, 0x04, 0x00) + b"\x00" * 8
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=opt_bytes,
    )
    sock.sendto(pkt, dest)


def send_sd_subscribe_nonexistent_service(
    sock: socket.socket,
    dest: Tuple[str, int],
    unknown_service_id: int,
    instance_id: int,
    eventgroup_id: int,
    host_ip: str,
    subscriber_port: int,
) -> None:
    """ETS_140-143: SubscribeEventgroup for a service_id not offered by DUT.

    DUT must send no SubscribeAck or a NAck. Must not crash.
    """
    entry_bytes = _subscribe_entry_bytes(
        service_id=unknown_service_id,
        instance_id=instance_id,
        eventgroup_id=eventgroup_id,
        num_opts=1,
    )
    opt_bytes = _endpoint_option_bytes(host_ip, subscriber_port)
    pkt = build_raw_sd_packet(
        flags=_SD_FLAGS_REBOOT_UNICAST,
        entries_bytes=entry_bytes,
        options_bytes=opt_bytes,
    )
    sock.sendto(pkt, dest)
