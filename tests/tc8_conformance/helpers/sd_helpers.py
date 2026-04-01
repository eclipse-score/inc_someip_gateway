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
"""
SD multicast capture and OFFER parsing for TC8 tests.

Single source of truth for SD parsing and multicast sockets.
Other helpers (``timing.py``, ``sd_sender.py``) import from here.

Uses blocking sockets (no asyncio).

Note: Loopback multicast needs ``sudo ip route add 224.0.0.0/4 dev lo``.
Set ``TC8_HOST_IP`` to a real NIC address to avoid this.
"""

import socket
import struct
import time
from typing import List

from helpers.constants import SD_MULTICAST_ADDR, SD_PORT
from someip.header import (
    SOMEIPHeader,
    SOMEIPSDEntry,
    SOMEIPSDEntryType,
    SOMEIPSDHeader,
)

# SOME/IP SD messages are identified by service ID 0xFFFF.
SD_SERVICE_ID: int = 0xFFFF


def create_udp_socket(bind_addr: str = "", port: int = 0) -> socket.socket:
    """Create a UDP socket, optionally bound to *bind_addr*:*port*."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if bind_addr or port:
        sock.bind((bind_addr, port))
    return sock


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

    return [
        entry
        for entry in sd_header.entries
        if entry.sd_type == SOMEIPSDEntryType.OfferService
    ]


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

    return collected  # pragma: no cover — reached only if min_count == 0
