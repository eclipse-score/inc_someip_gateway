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

Provides thin wrappers around the generic request/response transport in
socket_transport.py, tailored for field getter/setter interactions
(TC8-FLD-003 and TC8-FLD-004).
"""

from helpers.someip_types import SOMEIPHeader

from helpers.socket_transport import send_request, tcp_request_transport, udp_request_transport


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
    return send_request(
        udp_request_transport(host_ip, dut_port),
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
    return send_request(
        udp_request_transport(host_ip, dut_port),
        service_id,
        set_method_id,
        client_id,
        session_id,
        new_value,
        timeout_secs,
    )


# ---------------------------------------------------------------------------
# TCP variants for SOMEIPSRV_RPC_17 (reliable transport)
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
    return send_request(
        tcp_request_transport(host_ip, dut_port),
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
    return send_request(
        tcp_request_transport(host_ip, dut_port),
        service_id,
        set_method_id,
        client_id,
        session_id,
        new_value,
        timeout_secs,
    )
