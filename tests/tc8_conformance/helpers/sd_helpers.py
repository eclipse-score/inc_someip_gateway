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
"""SD multicast capture and OFFER parsing for TC8 tests.

Single source of truth for SD parsing and multicast sockets; other helpers
(``timing.py``, ``sd_sender.py``) import from here.

Note: loopback multicast needs ``bazel test --config=qemu-integration`` (auto-configures
a private network namespace), or set ``TC8_HOST_IP`` to a non-loopback
interface address instead.
"""

import socket
import struct
import sys
import time
from typing import Callable, List, NamedTuple, Optional, Tuple

from helpers.constants import EVENTGROUP_ALL_EVENTS_AND_FIELDS, SD_MULTICAST_ADDR, SD_PORT, SERVICE_ID
from helpers.sd_sender import capture_unicast_sd_entries
from helpers.someip_types import (
    SOMEIPHeader,
    SOMEIPSDEntry,
    SOMEIPSDEntryType,
    SOMEIPSDHeader,
)

# SOME/IP SD messages are identified by service ID 0xFFFF.
SD_SERVICE_ID: int = 0xFFFF

#: Length of the SOME/IP header in front of the SD payload.
SOMEIP_HEADER_LEN: int = 16

#: IPv4 Endpoint Option type and the offsets of its L4 protocol and port fields.
IPV4_ENDPOINT_OPTION_TYPE: int = 0x04
L4PROTO_OFFSET: int = 9
PORT_OFFSET: int = 10

#: Time to wait for the answer to a SubscribeEventgroup.
SUBSCRIBE_ANSWER_SECS: float = 5.0


def create_udp_socket(bind_addr: str = "", port: int = 0) -> socket.socket:
    """Create a UDP socket, optionally bound to *bind_addr*:*port*."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if bind_addr or port:
        sock.bind((bind_addr, port))
    return sock


#: Kernel receive timestamps (SO_TIMESTAMPNS) give the packet arrival time,
#: independent of when the test reads the socket. ``None`` where the platform
#: does not expose it, in which case callers fall back to the time of the read.
#: Python's ``socket`` module does not define SO_TIMESTAMPNS, hence the Linux
#: value (35, SO_TIMESTAMPNS_OLD in the C headers) as fallback. Only valid for
#: 64-bit CPU architectures with a 64-bit ``time_t``; 32-bit ``time_t`` ABIs
#: use SO_TIMESTAMPNS_NEW (64).
_LINUX_SO_TIMESTAMPNS = 35 if sys.platform.startswith("linux") else None
SO_TIMESTAMPNS: Optional[int] = getattr(socket, "SO_TIMESTAMPNS", _LINUX_SO_TIMESTAMPNS)


def open_multicast_socket(
    host_ip: str,
    multicast_group: str = SD_MULTICAST_ADDR,
    port: int = SD_PORT,
) -> socket.socket:
    """Open a UDP socket and join *multicast_group*. Caller must close it."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except AttributeError:
        pass
    sock.bind(("", port))
    group_bytes = socket.inet_aton(multicast_group)
    iface_bytes = socket.inet_aton(host_ip)
    mreq = struct.pack("4s4s", group_bytes, iface_bytes)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    if SO_TIMESTAMPNS is not None:
        sock.setsockopt(socket.SOL_SOCKET, SO_TIMESTAMPNS, 1)
    return sock


def parse_sd_offers(data: bytes) -> List[SOMEIPSDEntry]:
    """Parse a UDP payload and return all OfferService entries.

    Returns ``[]`` if *data* is not a valid SOME/IP-SD message.
    """
    try:
        someip_msg, _ = SOMEIPHeader.parse(data)
    except Exception:
        return []

    if someip_msg.service_id != SD_SERVICE_ID:
        return []

    try:
        sd_header, _ = SOMEIPSDHeader.parse(someip_msg.payload)
    except Exception:
        return []

    sd_header = sd_header.resolve_options()

    return [entry for entry in sd_header.entries if entry.sd_type == SOMEIPSDEntryType.OfferService]


class RawEventgroupEntry(NamedTuple):
    """Fields of a Subscribe or SubscribeAck entry, including the Reserved field."""

    entry_type: int
    service_id: int
    instance_id: int
    major_version: int
    ttl: int
    reserved: int
    eventgroup_id: int


def _sd_arrays(sd_payload: bytes) -> Tuple[bytes, bytes]:
    """Return the raw entries array and options array of an SD payload."""
    entries_len = struct.unpack_from(">I", sd_payload, 4)[0]
    options_len = struct.unpack_from(">I", sd_payload, 8 + entries_len)[0]
    options_start = 12 + entries_len
    return sd_payload[8 : 8 + entries_len], sd_payload[options_start : options_start + options_len]


def raw_sd_entries(sd_payload: bytes) -> List[bytes]:
    """Split the entries array of an SD payload into its 16-byte entries."""
    entries, _ = _sd_arrays(sd_payload)
    return [entries[i : i + 16] for i in range(0, len(entries), 16)]


def offers_for_service(sd_payload: bytes, instance_id: Optional[int] = None) -> List[bytes]:
    """Return the raw OfferService entries for ``SERVICE_ID`` (and *instance_id* if given) in an SD payload."""
    return [
        e
        for e in raw_sd_entries(sd_payload)
        if e[0] == SOMEIPSDEntryType.OfferService
        and struct.unpack_from(">H", e, 4)[0] == SERVICE_ID
        and (instance_id is None or struct.unpack_from(">H", e, 6)[0] == instance_id)
    ]


def raw_sd_options(sd_payload: bytes) -> List[bytes]:
    """Split the options array of an SD payload into raw options (Length and Type included)."""
    _, options = _sd_arrays(sd_payload)
    result: List[bytes] = []
    pos = 0
    while pos + 3 <= len(options):
        end = pos + 3 + struct.unpack_from(">H", options, pos)[0]
        result.append(options[pos:end])
        pos = end
    return result


def raw_entry_options(sd_payload: bytes, entry: bytes) -> List[bytes]:
    """Return the raw options referenced by both option runs of a raw 16-byte entry."""
    options = raw_sd_options(sd_payload)
    first_1, first_2, counts = entry[1], entry[2], entry[3]
    return options[first_1 : first_1 + (counts >> 4)] + options[first_2 : first_2 + (counts & 0x0F)]


def parse_raw_eventgroup_entry(entry: bytes) -> RawEventgroupEntry:
    """Decode a raw 16-byte Subscribe or SubscribeAck entry."""
    service_id, instance_id, major_and_ttl, reserved_and_counter, eventgroup_id = struct.unpack_from(">HHIHH", entry, 4)
    return RawEventgroupEntry(
        entry_type=entry[0],
        service_id=service_id,
        instance_id=instance_id,
        major_version=major_and_ttl >> 24,
        ttl=major_and_ttl & 0xFFFFFF,
        reserved=reserved_and_counter >> 4,
        eventgroup_id=eventgroup_id,
    )


def capture_sd_offers(
    host_ip: str,
    multicast_group: str = SD_MULTICAST_ADDR,
    port: int = SD_PORT,
    min_count: int = 1,
    timeout_secs: float = 5.0,
) -> List[SOMEIPSDEntry]:
    """Join the SD multicast group and collect OfferService entries.

    Returns as soon as *min_count* entries are captured.
    Raises ``TimeoutError`` if not enough entries arrive within *timeout_secs*.
    Raises ``OSError`` if the multicast socket setup fails.
    """
    sock = open_multicast_socket(host_ip, multicast_group, port)
    try:
        deadline = time.monotonic() + timeout_secs
        collected: List[SOMEIPSDEntry] = []

        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(min(remaining, 1.0))
            try:
                data, _ = sock.recvfrom(65535)
            except socket.timeout:
                continue

            offers = parse_sd_offers(data)
            collected.extend(offers)

            if len(collected) >= min_count:
                return collected

    finally:
        sock.close()

    if len(collected) < min_count:
        raise TimeoutError(
            f"Captured only {len(collected)} SD OFFER entries within "
            f"{timeout_secs:.1f}s (expected at least {min_count})"
        )

    return collected  # pragma: no cover, only reached if min_count == 0


def capture_subscribe_answers(
    sock: socket.socket, resend: Callable[[], None], max_results: Optional[int] = None
) -> List[SOMEIPSDEntry]:
    """Send with *resend* and return the Ack and NAck entries for ``EVENTGROUP_ALL_EVENTS_AND_FIELDS`` on *sock*.

    Collects for the whole answer window; pass *max_results* to return early when only a positive answer is needed.
    """
    resend()
    entries = capture_unicast_sd_entries(
        sock,
        filter_types=(SOMEIPSDEntryType.SubscribeAck,),
        timeout_secs=SUBSCRIBE_ANSWER_SECS,
        resend=resend,
        max_results=max_results,
    )
    return [e for e in entries if e.eventgroup_id == EVENTGROUP_ALL_EVENTS_AND_FIELDS]


def assert_nack(ets: str, answers: List[SOMEIPSDEntry]) -> None:
    """Assert a SubscribeEventgroupNAck (TTL 0) and no SubscribeEventgroupAck (TTL > 0)."""
    assert any(e.ttl == 0 for e in answers), (
        f"{ets}: No SubscribeEventgroupNAck received within {SUBSCRIBE_ANSWER_SECS:.0f} s"
    )
    assert not any(e.ttl > 0 for e in answers), f"{ets}: SubscribeEventgroupAck received, expected NAck"
