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
"""UDP transport helpers for SOME/IP over UDP (unreliable binding).

SOME/IP PRS_SOMEIP_00142 and PRS_SOMEIP_00569 require the receiver to
parse each SOME/IP message within a UDP datagram sequentially using the
length field as the sole framing indicator.
"""

import socket
import time
from typing import Callable, List

from helpers.someip_types import SOMEIPHeader

# Called once per parsed SOME/IP message. Returns True to stop the receive
# loop early (the caller has everything it needs), False to keep waiting.
_MessageHandler = Callable[[SOMEIPHeader], bool]


def parse_datagram(data: bytes) -> List[SOMEIPHeader]:
    """Return all SOME/IP messages packed into a single UDP datagram.

    A UDP datagram may bundle multiple SOME/IP messages back to back
    (PRS_SOMEIP_00142 / PRS_SOMEIP_00569). SOMEIPHeader.parse() returns
    (message, remaining_bytes); looping over the remainder ensures every
    message in the datagram is parsed, not just the first one.
    """
    messages: List[SOMEIPHeader] = []
    buf = data
    while buf:
        try:
            msg, buf = SOMEIPHeader.parse(buf)
            messages.append(msg)
        except Exception:
            break
    return messages


def receive_until(
    sock: socket.socket,
    timeout_secs: float,
    on_message: _MessageHandler,
) -> bool:
    """Shared deadline-based receive loop for SOME/IP messages over UDP.

    Repeatedly calls recvfrom() until *timeout_secs* elapses. Every SOME/IP
    message found in each datagram is parsed (a single datagram may bundle
    several messages) and passed to *on_message*. The loop stops as soon as
    *on_message* returns True.

    Returns True if *on_message* signalled completion, False if the
    deadline was reached first.
    """
    deadline = time.monotonic() + timeout_secs
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            return False
        sock.settimeout(min(remaining, 0.5))
        try:
            data, _ = sock.recvfrom(65535)
        except socket.timeout:
            continue
        for msg in parse_datagram(data):
            if on_message(msg):
                return True


def udp_send_concatenated(
    sock: socket.socket,
    addr: tuple[str, int],
    messages: list[bytes],
) -> None:
    """Send multiple SOME/IP messages concatenated into ONE UDP datagram.

    SOME/IP PRS_SOMEIP_00142 and PRS_SOMEIP_00569 require the DUT to parse
    multiple SOME/IP messages packed into a single UDP datagram. This helper
    concatenates all *messages* and delivers them as one ``sendto()`` call
    so the DUT receives them in a single datagram.

    Used by: SOMEIP_ETS_069.
    """
    sock.sendto(b"".join(messages), addr)


def udp_receive_responses(
    sock: socket.socket,
    count: int,
    timeout_secs: float = 5.0,
) -> List[SOMEIPHeader]:
    """Receive exactly *count* SOME/IP responses from a UDP socket.

    Uses a single shared deadline across the whole wait so the total wait
    never exceeds *timeout_secs*. The DUT may bundle multiple responses
    into a single datagram; every message in each datagram is parsed, not
    just the first one.

    Raises ``socket.timeout`` if not all responses arrive in time.

    Used by: SOMEIP_ETS_069.
    """
    responses: List[SOMEIPHeader] = []

    def _collect(msg: SOMEIPHeader) -> bool:
        responses.append(msg)
        return len(responses) >= count

    completed = receive_until(sock, timeout_secs, _collect)
    if not completed:
        raise socket.timeout(f"udp_receive_responses: deadline exceeded after {len(responses)}/{count} responses")
    return responses
