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
"""Generic request/response socket transport for TC8 conformance helpers.

Provides an injectable transport abstraction (UDP or TCP) that owns the full
socket lifecycle (open/connect, send, receive, close) for a single
request/response exchange, plus send_request() which builds a SOME/IP
request and sends it through a given transport.
"""

from typing import Callable

from helpers.someip_types import SOMEIPHeader

from helpers.message_builder import build_request
from helpers.sd_helpers import create_udp_socket
from helpers.tcp_helpers import tcp_connect, tcp_receive_response, tcp_send_request

# A transport takes the raw request bytes and a timeout, and returns the
# parsed response. It owns the full socket lifecycle (open/connect, send,
# receive, close).
RequestTransport = Callable[[bytes, float], SOMEIPHeader]


def udp_request_transport(host_ip: str, dut_port: int) -> RequestTransport:
    """Build a UDP transport for send_request().

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


def tcp_request_transport(host_ip: str, dut_port: int) -> RequestTransport:
    """Build a TCP transport for send_request().

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


def send_request(
    transport: RequestTransport,
    service_id: int,
    method_id: int,
    client_id: int,
    session_id: int,
    payload: bytes,
    timeout_secs: float,
) -> SOMEIPHeader:
    """Build a SOME/IP request and send it through *transport*.

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
