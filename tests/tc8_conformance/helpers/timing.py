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
"""Timestamped SD capture for TC8 phase timing tests.

Captures OfferService entries with monotonic timestamps to verify
SD phase timing (initial wait, repetition, main phase).

Delegates parsing and socket setup to :mod:`helpers.sd_helpers`.
"""

import socket
import time
from typing import List, Tuple

from someip.header import SOMEIPSDEntry

from helpers.constants import SD_MULTICAST_ADDR, SD_PORT
from helpers.sd_helpers import open_multicast_socket, parse_sd_offers


def collect_sd_offers_from_socket(
    sock: socket.socket,
    count: int,
    timeout_secs: float,
) -> List[Tuple[float, SOMEIPSDEntry]]:
    """Collect *count* OfferService entries with timestamps from *sock*.

    Raises ``TimeoutError`` if fewer than *count* entries arrive in time.
    """
    deadline = time.monotonic() + timeout_secs
    collected: List[Tuple[float, SOMEIPSDEntry]] = []

    while time.monotonic() < deadline:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        sock.settimeout(min(remaining, 0.5))
        try:
            data, _ = sock.recvfrom(65535)
        except socket.timeout:
            continue

        recv_ts = time.monotonic()
        for entry in parse_sd_offers(data):
            collected.append((recv_ts, entry))

        if len(collected) >= count:
            return collected

    if len(collected) < count:
        raise TimeoutError(
            f"Captured only {len(collected)} SD OFFER entries within "
            f"{timeout_secs:.1f}s (expected at least {count})"
        )
    return collected  # pragma: no cover


def capture_sd_offers_with_timestamps(
    host_ip: str,
    multicast_group: str = SD_MULTICAST_ADDR,
    port: int = SD_PORT,
    count: int = 3,
    timeout_secs: float = 20.0,
) -> List[Tuple[float, SOMEIPSDEntry]]:
    """Open a multicast socket and capture *count* OfferService entries with timestamps."""
    sock = open_multicast_socket(host_ip, multicast_group, port)
    try:
        return collect_sd_offers_from_socket(sock, count, timeout_secs)
    finally:
        sock.close()
