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
"""TC8 SOME/IP request handling over TCP: TC8-TCP-001 and TC8-TCP-002.

Separate from ``test_msg_format.py`` because it needs the service config with a TCP endpoint.
See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import socket
import subprocess

import pytest
from attribute_plugin import add_test_properties

from helpers.constants import DUT_RELIABLE_PORT, METHOD_ECHO_UINT8, METHOD_ECHO_UINT8_ARRAY, SERVICE_ID
from helpers.message_builder import build_request
from helpers.someip_assertions import assert_return_code, assert_valid_response
from helpers.someip_types import SOMEIPHeader, SOMEIPReturnCode
from helpers.tcp_helpers import tcp_connect, tcp_receive_n_responses, tcp_receive_response, tcp_send_concatenated

#: Service config with a TCP endpoint.
SOMEIP_CONFIG: str = "tc8_someipd_service.json"

#: Expected failure for tests that need a SOME/IP method reply from the DUT.
NO_ECHO_METHODS = pytest.mark.xfail(
    strict=True,
    reason="DUT offers no echo methods (to_num_of_methods(0) in local_service_instance.cpp), so no reply is sent",
)

_CLIENT_ID: int = 0x0010


def _tcp_request(dut_ip: str, data: bytes) -> SOMEIPHeader:
    sock = tcp_connect(dut_ip, DUT_RELIABLE_PORT)
    try:
        sock.sendall(data)
        return tcp_receive_response(sock)
    finally:
        sock.close()


def _assert_ok_response(msg: SOMEIPHeader, method_id: int = METHOD_ECHO_UINT8) -> None:
    assert_valid_response(msg, SERVICE_ID, method_id)
    assert_return_code(msg, SOMEIPReturnCode.E_OK)


def _assert_request_id(msg: SOMEIPHeader, client_id: int, session_id: int) -> None:
    assert (msg.client_id, msg.session_id) == (client_id, session_id), (
        f"Request ID (client, session) = (0x{msg.client_id:04x}, 0x{msg.session_id:04x}), "
        f"expected (0x{client_id:04x}, 0x{session_id:04x})"
    )


class TestTcpTransport:
    """SOME/IP request handling over TCP (Sections 6.1.5.7 and 6.1.6)."""

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__tcp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_01_single_tcp_connection(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-TCP-001 (SOMEIPSRV_RPC_01): a REQUEST over a TCP connection gets a RESPONSE with E_OK."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x0050)
        msg = _tcp_request(dut_ip, req)
        _assert_ok_response(msg)
        _assert_request_id(msg, _CLIENT_ID, 0x0050)

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__tcp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_068_unaligned_messages_tcp(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-TCP-002 (SOMEIP_ETS_068): three REQUESTs in one TCP segment, the third at offset 34, get a RESPONSE each."""
        assert dut.poll() is None, "DUT is not running"
        sessions = (0x0071, 0x0072, 0x0073)
        messages = [
            build_request(SERVICE_ID, METHOD_ECHO_UINT8_ARRAY, client_id=_CLIENT_ID, session_id=s, payload=p)
            for s, p in zip(sessions, (b"", b"\xaa\xbb", b""))
        ]
        sock = tcp_connect(dut_ip, DUT_RELIABLE_PORT)
        try:
            tcp_send_concatenated(sock, messages)
            try:
                responses = tcp_receive_n_responses(sock, count=3, timeout_secs=5.0)
            except socket.timeout as exc:
                pytest.fail(f"TC8-TCP-002: {exc}")
        finally:
            sock.close()
        for resp in responses:
            _assert_ok_response(resp, METHOD_ECHO_UINT8_ARRAY)
        assert sorted(r.session_id for r in responses) == list(sessions), (
            f"TC8-TCP-002: session IDs of the RESPONSEs {[hex(r.session_id) for r in responses]}"
        )
