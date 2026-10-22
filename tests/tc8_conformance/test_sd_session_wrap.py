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
"""TC8-SDW-001: Session ID of the OfferService responses after the wrap-around.

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import socket
import struct
import subprocess
import time
from typing import List, Optional

from attribute_plugin import add_test_properties

from helpers.constants import INSTANCE_ID, SD_PORT, SERVICE_ID
from helpers.sd_helpers import SOMEIP_HEADER_LEN, raw_sd_entries
from helpers.sd_sender import open_sender_socket, send_find_service
from helpers.someip_types import SOMEIPSDEntryType

_SESSION_ID_OFFSET: int = 10
_BURST: int = 50
_DRAIN_SECS: float = 3.0
_DEADLINE_SECS: float = 240.0
_RCVBUF_BYTES: int = 8 * 1024 * 1024


def _offer_session_id(data: bytes) -> Optional[int]:
    """Return the Session ID of an SD datagram holding an OfferService for ``SERVICE_ID`` and ``INSTANCE_ID``."""
    if len(data) < SOMEIP_HEADER_LEN + 12:
        return None
    try:
        entries = raw_sd_entries(data[SOMEIP_HEADER_LEN:])
    except struct.error:
        return None
    for entry in entries:
        if entry[0] == SOMEIPSDEntryType.OfferService and struct.unpack_from(">HH", entry, 4) == (
            SERVICE_ID,
            INSTANCE_ID,
        ):
            return struct.unpack_from(">H", data, _SESSION_ID_OFFSET)[0]
    return None


def _drain(sock: socket.socket, timeout_secs: float, out: List[int]) -> None:
    """Append the Session IDs of the Offers waiting on *sock* to *out*."""
    sock.settimeout(timeout_secs)
    try:
        while True:
            session_id = _offer_session_id(sock.recv(65535))
            if session_id is not None:
                out.append(session_id)
            sock.settimeout(0.0)
    except (BlockingIOError, socket.timeout):
        pass


class TestSDSessionWrap:
    """Session ID of the unicast OfferService responses."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_152_session_id_one_after_wrap(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDW-001 (ETS_152): the first Session ID of the OfferService responses after 0xFFFF is 0x0001."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, _RCVBUF_BYTES)
        start = time.monotonic()
        sent = 0
        offers: List[int] = []
        wrap_at: Optional[int] = None
        try:
            while wrap_at is None and time.monotonic() - start < _DEADLINE_SECS:
                for _ in range(_BURST):
                    send_find_service(sock, (dut_ip, SD_PORT), service_id=SERVICE_ID, instance_id=INSTANCE_ID)
                sent += _BURST
                _drain(sock, 0.05, offers)
                wrap_at = next((i for i in range(1, len(offers)) if offers[i] < offers[i - 1]), None)
            if wrap_at is None:
                _drain(sock, _DRAIN_SECS, offers)
                wrap_at = next((i for i in range(1, len(offers)) if offers[i] < offers[i - 1]), None)
        finally:
            sock.close()
        elapsed = time.monotonic() - start

        assert wrap_at is not None, (
            f"ETS_152: no Session ID wrap-around seen in {len(offers)} Offers after {sent} FindService messages "
            f"({elapsed:.0f} s); last Session ID {offers[-1] if offers else None}"
        )
        boundary = (offers[wrap_at - 1], offers[wrap_at])
        assert boundary == (0xFFFF, 0x0001), (
            f"ETS_152: Session IDs at the wrap-around are 0x{boundary[0]:04x}, 0x{boundary[1]:04x}, "
            "expected 0xffff, 0x0001"
        )
