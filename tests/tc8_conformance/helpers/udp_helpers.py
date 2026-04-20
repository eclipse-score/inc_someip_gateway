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
from someip.header import SOMEIPHeader


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
) -> list[SOMEIPHeader]:
    """Receive exactly *count* SOME/IP responses from a UDP socket.

    Uses a single shared deadline across all *count* receive calls so the
    total wait never exceeds *timeout_secs*. Each ``recvfrom()`` call returns
    one complete SOME/IP message (the DUT sends one response datagram per
    request processed from the concatenated datagram).

    Raises ``socket.timeout`` if not all responses arrive in time.

    Used by: SOMEIP_ETS_069.
    """
    deadline = time.monotonic() + timeout_secs
    responses: list[SOMEIPHeader] = []
    while len(responses) < count:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise socket.timeout(
                f"udp_receive_responses: deadline exceeded after "
                f"{len(responses)}/{count} responses"
            )
        sock.settimeout(remaining)
        data, _ = sock.recvfrom(65535)
        msg, _ = SOMEIPHeader.parse(data)
        responses.append(msg)
    return responses
