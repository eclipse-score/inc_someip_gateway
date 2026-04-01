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
"""Event subscription and notification capture for TC8 conformance tests.

Subscribes to eventgroups via SD and captures NOTIFICATION messages.
"""

import socket
import time
from typing import List

from someip.header import SOMEIPHeader, SOMEIPMessageType, L4Protocols

from helpers.sd_sender import (
    open_sender_socket,
    send_subscribe_eventgroup,
    capture_unicast_sd_entries,
    SOMEIPSDEntryType,
)


def subscribe_and_wait_ack(
    tester_ip: str,
    host_ip: str,
    sd_port: int,
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    major_version: int,
    notif_port: int,
    timeout_secs: float = 5.0,
    ttl: int = 3,
) -> socket.socket:
    """Subscribe to an eventgroup and wait for the Ack.

    Returns the SD socket (still open). Caller must close it.
    Raises AssertionError if no Ack is received.

    *ttl* controls the SD SubscribeEventgroup entry TTL (seconds).  Use a
    larger value (e.g. 30) when the test collects notifications over an
    interval longer than the default 3-second window.
    """
    sd_sock = open_sender_socket(tester_ip)
    try:

        def _send_sub() -> None:
            send_subscribe_eventgroup(
                sd_sock,
                (host_ip, sd_port),
                service_id,
                instance_id,
                eventgroup_id,
                major_version,
                subscriber_ip=tester_ip,
                subscriber_port=notif_port,
                ttl=ttl,
            )

        _send_sub()
        entries = capture_unicast_sd_entries(
            sd_sock,
            filter_types=(SOMEIPSDEntryType.SubscribeAck,),
            timeout_secs=timeout_secs,
            resend=_send_sub,
            max_results=1,  # Return as soon as first ACK arrives; preserves subscription TTL.
        )
        acks = [e for e in entries if e.eventgroup_id == eventgroup_id and e.ttl > 0]
        assert acks, (
            f"No SubscribeEventgroupAck received for eventgroup 0x{eventgroup_id:04x}"
        )
    except Exception:
        sd_sock.close()
        raise
    return sd_sock


def capture_notifications(
    sock: socket.socket,
    event_id: int,
    service_id: int,
    count: int = 1,
    timeout_secs: float = 5.0,
) -> List[SOMEIPHeader]:
    """Capture NOTIFICATION messages for a specific event on *sock*.

    Returns up to *count* matching notifications within *timeout_secs*.
    """
    collected: List[SOMEIPHeader] = []
    deadline = time.monotonic() + timeout_secs

    while time.monotonic() < deadline and len(collected) < count:
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

        if msg.service_id == service_id and msg.method_id == event_id:
            collected.append(msg)

    return collected


def capture_any_notifications(
    sock: socket.socket,
    service_id: int,
    timeout_secs: float = 5.0,
) -> List[SOMEIPHeader]:
    """Capture any SOME/IP messages for *service_id* on *sock*."""
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


def assert_notification_header(msg: SOMEIPHeader, expected_event_id: int) -> None:
    """Assert a SOME/IP message is a valid NOTIFICATION with the expected event_id."""
    assert msg.message_type == SOMEIPMessageType.NOTIFICATION, (
        f"TC8-EVT: message_type mismatch: got 0x{msg.message_type:02x}, "
        f"expected NOTIFICATION (0x{SOMEIPMessageType.NOTIFICATION:02x})"
    )
    assert msg.method_id == expected_event_id, (
        f"TC8-EVT: event_id mismatch: got 0x{msg.method_id:04x}, "
        f"expected 0x{expected_event_id:04x}"
    )


def subscribe_and_wait_ack_tcp(
    tester_ip: str,
    host_ip: str,
    sd_port: int,
    service_id: int,
    instance_id: int,
    eventgroup_id: int,
    major_version: int,
    notif_port: int,
    timeout_secs: float = 5.0,
) -> socket.socket:
    """Subscribe to an eventgroup with a TCP endpoint and wait for the Ack.

    Like subscribe_and_wait_ack() but the subscription advertises a TCP
    endpoint (L4Proto=TCP) so the DUT delivers notifications over TCP.
    Returns the SD socket (still open). Caller must close it.
    """
    sd_sock = open_sender_socket(tester_ip)
    try:

        def _send_sub() -> None:
            send_subscribe_eventgroup(
                sd_sock,
                (host_ip, sd_port),
                service_id,
                instance_id,
                eventgroup_id,
                major_version,
                subscriber_ip=tester_ip,
                subscriber_port=notif_port,
                l4proto=L4Protocols.TCP,
            )

        _send_sub()
        entries = capture_unicast_sd_entries(
            sd_sock,
            filter_types=(SOMEIPSDEntryType.SubscribeAck,),
            timeout_secs=timeout_secs,
            resend=_send_sub,
            max_results=1,
        )
        acks = [e for e in entries if e.eventgroup_id == eventgroup_id and e.ttl > 0]
        assert acks, (
            f"No SubscribeEventgroupAck received for TCP eventgroup 0x{eventgroup_id:04x}"
        )
    except Exception:
        sd_sock.close()
        raise
    return sd_sock
