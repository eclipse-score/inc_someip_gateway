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
"""Field GET/SET request helpers for TC8-FLD conformance tests.

Provides thin wrappers around the message_builder and raw socket primitives
already established in test_someip_message_format.py, tailored for field
getter/setter interactions (TC8-FLD-003 and TC8-FLD-004).

The public send_*_field* functions only differ in which transport (UDP or
TCP) carries the request. The shared request-build/send/receive/parse
logic lives in _send_field_request(), which takes an injected transport
callable so it does not need to know which transport is in use.
"""

from typing import Callable

from helpers.someip_types import SOMEIPHeader

from helpers.message_builder import build_request
from helpers.sd_helpers import create_udp_socket
from helpers.tcp_helpers import tcp_connect, tcp_receive_response, tcp_send_request

# A transport takes the raw request bytes and a timeout, and returns the
# parsed response. It owns the full socket lifecycle (open/connect, send,
# receive, close).
_FieldTransport = Callable[[bytes, float], SOMEIPHeader]


def _udp_transport(host_ip: str, dut_port: int) -> _FieldTransport:
    """Build a UDP transport for _send_field_request().

    Opens a fresh UDP socket, sends the request to (host_ip, dut_port),
    waits for a single response datagram, and returns the parsed message.
    """

    def _transport(request_bytes: bytes, timeout_secs: float) -> SOMEIPHeader:
        sock = create_udp_socket(port=0)
        try:
            sock.sendto(request_bytes, (host_ip, dut_port))
            sock.settimeout(timeout_secs)
            data, _ = sock.recvfrom(65535)
            resp, _ = SOMEIPHeader.parse(data)
            return resp
        finally:
            sock.close()

    return _transport


def _tcp_transport(host_ip: str, dut_port: int) -> _FieldTransport:
    """Build a TCP transport for _send_field_request().

    Connects to (host_ip, dut_port), sends the request, and returns the
    parsed response using SOME/IP TCP stream framing.
    """

    def _transport(request_bytes: bytes, timeout_secs: float) -> SOMEIPHeader:
        sock = tcp_connect(host_ip, dut_port, timeout_secs=timeout_secs)
        try:
            tcp_send_request(sock, request_bytes)
            return tcp_receive_response(sock, timeout_secs=timeout_secs)
        finally:
            sock.close()

    return _transport


def _send_field_request(
    transport: _FieldTransport,
    service_id: int,
    method_id: int,
    client_id: int,
    session_id: int,
    payload: bytes,
    timeout_secs: float,
) -> SOMEIPHeader:
    """Build a field GET/SET request and send it through *transport*.

    Raises ``socket.timeout`` if no response arrives within *timeout_secs*.
    """
    request_bytes = build_request(
        service_id,
        method_id,
        client_id=client_id,
        session_id=session_id,
        payload=payload,
    )
    return transport(request_bytes, timeout_secs)


def send_get_field(
    host_ip: str,
    service_id: int,
    get_method_id: int,
    dut_port: int,
    client_id: int = 0x0020,
    session_id: int = 0x0001,
    timeout_secs: float = 3.0,
) -> SOMEIPHeader:
    """Send a GET field request and return the RESPONSE.

    Raises ``socket.timeout`` if no response arrives within *timeout_secs*.
    """
    return _send_field_request(
        _udp_transport(host_ip, dut_port),
        service_id,
        get_method_id,
        client_id,
        session_id,
        b"",
        timeout_secs,
    )


def send_set_field(
    host_ip: str,
    service_id: int,
    set_method_id: int,
    new_value: bytes,
    dut_port: int,
    client_id: int = 0x0020,
    session_id: int = 0x0002,
    timeout_secs: float = 3.0,
) -> SOMEIPHeader:
    """Send a SET field request with *new_value* as payload and return the RESPONSE.

    Raises ``socket.timeout`` if no response arrives within *timeout_secs*.
    """
    return _send_field_request(
        _udp_transport(host_ip, dut_port),
        service_id,
        set_method_id,
        client_id,
        session_id,
        new_value,
        timeout_secs,
    )


# ---------------------------------------------------------------------------
# TCP variants — SOMEIPSRV_RPC_17 (reliable transport)
# ---------------------------------------------------------------------------


def send_get_field_tcp(
    host_ip: str,
    service_id: int,
    get_method_id: int,
    dut_port: int,
    client_id: int = 0x0040,
    session_id: int = 0x0010,
    timeout_secs: float = 3.0,
) -> SOMEIPHeader:
    """Send a GET field request over TCP and return the RESPONSE.

    TCP variant of send_get_field() for SOMEIPSRV_RPC_17 testing.
    """
    return _send_field_request(
        _tcp_transport(host_ip, dut_port),
        service_id,
        get_method_id,
        client_id,
        session_id,
        b"",
        timeout_secs,
    )


def send_set_field_tcp(
    host_ip: str,
    service_id: int,
    set_method_id: int,
    new_value: bytes,
    dut_port: int,
    client_id: int = 0x0040,
    session_id: int = 0x0011,
    timeout_secs: float = 3.0,
) -> SOMEIPHeader:
    """Send a SET field request over TCP with *new_value* and return the RESPONSE.

    TCP variant of send_set_field() for SOMEIPSRV_RPC_17 testing.
    """
    return _send_field_request(
        _tcp_transport(host_ip, dut_port),
        service_id,
        set_method_id,
        client_id,
        session_id,
        new_value,
        timeout_secs,
    )
