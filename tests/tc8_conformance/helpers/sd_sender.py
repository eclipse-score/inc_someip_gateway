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
"""SD packet building, sending, and unicast capture for TC8 tests.

Helpers to send FindService / SubscribeEventgroup messages and
capture unicast SD responses and SOME/IP notifications.
"""

import ipaddress
import socket
import struct
import time
from typing import Callable, List, Optional, Tuple

from helpers.constants import SD_PORT
from helpers.someip_types import (
    IPv4EndpointOption,
    L4Protocols,
    SD_INTERFACE_VERSION,
    SD_METHOD,
    SD_SERVICE,
    SOMEIPHeader,
    SOMEIPMessageType,
    SOMEIPReturnCode,
    SOMEIPSDEntry,
    SOMEIPSDEntryType,
    SOMEIPSDHeader,
    SOMEIPSDOption,
)


# ---------------------------------------------------------------------------
# Socket management
# ---------------------------------------------------------------------------


def open_sender_socket(local_ip: str) -> socket.socket:
    """Open a UDP socket at ``(local_ip, SD_PORT)`` for SD send/receive.

    Binds to ``SD_PORT`` because the SOME/IP stack drops SD messages from
    other ports. ``local_ip`` must differ from the DUT address. Caller must
    close the socket.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except AttributeError:
        pass  # SO_REUSEPORT not available on all platforms
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(local_ip))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
    sock.bind((local_ip, SD_PORT))
    return sock


# ---------------------------------------------------------------------------
# SD packet serialisation helpers
# ---------------------------------------------------------------------------


# SD session counter, incremented per message to avoid DUT duplicate-detection.
# PRS_SOMEIPSD_00154 requires session_id to start at 0x0001 (0x0000 is reserved)
# and to increment with each SD message, wrapping from 0xFFFF back to 0x0001.
# NOTE: Assumes serial test execution (Bazel "exclusive" tag). Not thread-safe.
_last_session_id = 0


def _next_session_id() -> int:
    """Return the next SD session ID, wrapping from 0xFFFF to 0x0001 (0x0000 is reserved)."""
    global _last_session_id
    _last_session_id = _last_session_id % 0xFFFF + 1
    return _last_session_id


def _build_sd_packet(entry: SOMEIPSDEntry, session_id: int = 0) -> bytes:
    """Wrap a single SD entry into a SOME/IP-SD UDP datagram.

    When *session_id* is 0 (default), an auto-incrementing counter is used.
    """
    if session_id == 0:
        session_id = _next_session_id()
    options: List[SOMEIPSDOption] = []
    indexed = entry.assign_option_index(options)
    sd_hdr = SOMEIPSDHeader(entries=(indexed,), options=tuple(options))
    return SOMEIPHeader(
        service_id=SD_SERVICE,
        method_id=SD_METHOD,
        # SD is control traffic, not tied to any application client; the
        # SOME/IP-SD spec reserves client_id 0x0000 for this purpose.
        client_id=0x0000,
        session_id=session_id,
        interface_version=SD_INTERFACE_VERSION,
        message_type=SOMEIPMessageType.NOTIFICATION,
        return_code=SOMEIPReturnCode.E_OK,
        payload=sd_hdr.build(),
    ).build()


# ---------------------------------------------------------------------------
# Send helpers
# ---------------------------------------------------------------------------


def send_find_service(
    sock: socket.socket,
    sd_dest: Tuple[str, int],
    service_id: int,
    instance_id: int = 0xFFFF,
    major_version: int = 0xFF,
    minor_version: int = 0xFFFFFFFF,
    session_id: int = 0,
) -> None:
    """Send a FindService SD message to *sd_dest*."""
    entry = SOMEIPSDEntry(
        sd_type=SOMEIPSDEntryType.FindService,
        service_id=service_id,
        instance_id=instance_id,
        major_version=major_version,
        ttl=3,
        minver_or_counter=minor_version,
    )
    sock.sendto(_build_sd_packet(entry, session_id), sd_dest)


def send_subscribe_eventgroup(
    sock: socket.socket,
    sd_dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    major_version: int,
    subscriber_ip: str,
    subscriber_port: int = 0,
    ttl: int = 3,
    session_id: int = 0,
    l4proto: L4Protocols = L4Protocols.UDP,
    tcp_port: Optional[int] = None,
    udp_port: Optional[int] = None,
) -> None:
    """Send a SubscribeEventgroup (or StopSubscribe when ``ttl=0``).

    For eventgroups mixing TCP and UDP events, strict SOME/IP implementations
    require both endpoints in the subscription request: pass `tcp_port`
    and/or `udp_port` instead of `subscriber_port`/`l4proto` to attach
    multiple IPv4 Endpoint Options to the SD entry.
    """
    endpoint_opt = []

    if tcp_port is None and udp_port is None:
        endpoint_opt.append(
            IPv4EndpointOption(
                address=ipaddress.IPv4Address(subscriber_ip),
                l4proto=l4proto,
                port=subscriber_port,
            )
        )
    else:
        if udp_port is not None:
            endpoint_opt.append(
                IPv4EndpointOption(
                    address=ipaddress.IPv4Address(subscriber_ip),
                    l4proto=L4Protocols.UDP,
                    port=udp_port,
                )
            )
        if tcp_port is not None:
            endpoint_opt.append(
                IPv4EndpointOption(
                    address=ipaddress.IPv4Address(subscriber_ip),
                    l4proto=L4Protocols.TCP,
                    port=tcp_port,
                )
            )

    # Build the SOME/IP-SD Entry using the configured endpoint options
    entry = SOMEIPSDEntry(
        sd_type=SOMEIPSDEntryType.Subscribe,
        service_id=service_id,
        instance_id=instance_id,
        major_version=major_version,
        ttl=ttl,
        minver_or_counter=eventgroup_id & 0xFFFF,
        options_1=tuple(endpoint_opt),
    )
    sock.sendto(_build_sd_packet(entry, session_id), sd_dest)


def send_subscribe_eventgroup_reserved_set(
    sock: socket.socket,
    sd_dest: Tuple[str, int],
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    major_version: int,
    subscriber_ip: str,
    subscriber_port: int,
    ttl: int = 3,
    reserved_value: int = 0x0F,
    session_id: Optional[int] = None,
) -> None:
    """Send a SubscribeEventgroup with the reserved counter bits in the entry set non-zero.

    The SubscribeEventgroup SD entry encodes a 4-bit counter nibble and a
    12-bit reserved field in the upper 16 bits of the ``minver_or_counter``
    word. This function sets the 12 reserved bits to ``reserved_value`` to
    exercise SOMEIPSRV_SD_MESSAGE_19: the DUT shall send a NAck or silently
    ignore the subscribe.
    """
    if session_id is None:
        session_id = _next_session_id()
    endpoint_opt = IPv4EndpointOption(
        address=ipaddress.IPv4Address(subscriber_ip),
        l4proto=L4Protocols.UDP,
        port=subscriber_port,
    )
    entry = SOMEIPSDEntry(
        sd_type=SOMEIPSDEntryType.Subscribe,
        service_id=service_id,
        instance_id=instance_id,
        major_version=major_version,
        ttl=ttl,
        minver_or_counter=eventgroup_id & 0xFFFF,
        options_1=(endpoint_opt,),
    )
    raw = bytearray(_build_sd_packet(entry, session_id))

    # Patch bytes 36-39: the 4-byte ``minver_or_counter`` in the first SD entry.
    # Structure at those bytes is: [reserved(12 bits) | counter(4 bits) | eventgroup_id(16 bits)].
    # Set the 12 reserved bits (bits 31-20) to a non-zero pattern.
    _MINVER_OFFSET = 36
    original_word: int = struct.unpack_from("!I", raw, _MINVER_OFFSET)[0]
    patched_word: int = original_word | ((reserved_value & 0x0FFF) << 20)
    struct.pack_into("!I", raw, _MINVER_OFFSET, patched_word)

    sock.sendto(bytes(raw), sd_dest)


# ---------------------------------------------------------------------------
# Capture helpers
# ---------------------------------------------------------------------------


def capture_unicast_sd_entries(
    sock: socket.socket,
    filter_types: Optional[Tuple[SOMEIPSDEntryType, ...]] = None,
    timeout_secs: float = 5.0,
    resend: Optional[Callable[[], None]] = None,
    resend_interval_secs: float = 1.5,
    max_results: Optional[int] = None,
) -> List[SOMEIPSDEntry]:
    """Receive SD entries on *sock* within *timeout_secs*, optionally filtered by *filter_types*.

    When *resend* is provided it is called every *resend_interval_secs* while
    no matching entries have been captured, mirroring real-world SD client
    retry behavior. When *max_results* is set, returns early once that many
    matching entries are collected, so the subscription TTL has more time
    left to live.
    """
    collected: List[SOMEIPSDEntry] = []
    deadline = time.monotonic() + timeout_secs
    next_resend = (time.monotonic() + resend_interval_secs) if resend else None

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break

        # Early-exit once max_results matching entries are collected.
        if max_results is not None and len(collected) >= max_results:
            break

        # Resend if no matching entries yet and the interval has elapsed.
        if resend and next_resend and not collected and time.monotonic() >= next_resend:
            resend()
            next_resend = time.monotonic() + resend_interval_secs

        sock.settimeout(min(remaining, 0.5))
        try:
            data, _ = sock.recvfrom(65535)
        except socket.timeout:
            continue

        for entry in _parse_sd_entries(data):
            if filter_types is None or entry.sd_type in filter_types:
                collected.append(entry)

    return collected


def capture_some_ip_messages(
    sock: socket.socket,
    service_id: int,
    timeout_secs: float,
) -> List[SOMEIPHeader]:
    """Receive SOME/IP messages for *service_id* on *sock* within *timeout_secs*."""
    collected: List[SOMEIPHeader] = []
    deadline = time.monotonic() + timeout_secs

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        sock.settimeout(min(remaining, 0.5))
        try:
            data, _ = sock.recvfrom(65535)
        except socket.timeout:
            continue

        try:
            msg, _ = SOMEIPHeader.parse(data)
        except Exception:
            continue

        if msg.service_id == service_id:
            collected.append(msg)

    return collected


# ---------------------------------------------------------------------------
# Internal parsing
# ---------------------------------------------------------------------------


def _parse_sd_entries(data: bytes) -> List[SOMEIPSDEntry]:
    """Parse a UDP payload and return all SD entries (any type)."""
    try:
        someip_msg, _ = SOMEIPHeader.parse(data)
    except Exception:
        return []
    if someip_msg.service_id != SD_SERVICE:
        return []
    try:
        sd_header, _ = SOMEIPSDHeader.parse(someip_msg.payload)
    except Exception:
        return []
    # Without resolve_options(), options_1/options_2 on every entry stay empty
    # even when the wire packet carries options.
    return list(sd_header.resolve_options().entries)
