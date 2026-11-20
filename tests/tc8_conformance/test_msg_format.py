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
"""TC8 SOME/IP message format tests: TC8-MSG-001 to TC8-MSG-025 and TC8-UDP-001.

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import dataclasses
import socket
import subprocess
import time
from typing import List, Tuple

import pytest
from attribute_plugin import add_test_properties

from helpers.constants import (
    DUT_UNRELIABLE_PORT,
    MAJOR_VERSION,
    METHOD_ECHO_UINT8,
    METHOD_ECHO_UINT8_ARRAY,
    SERVICE_ID,
)
from helpers.message_builder import (
    build_notification_as_request,
    build_oversized_message,
    build_request,
    build_request_no_return,
    build_request_with_return_code,
    build_wrong_protocol_version_request,
)
from helpers.someip_assertions import assert_return_code, assert_valid_response
from helpers.someip_types import SOMEIPHeader, SOMEIPMessageType, SOMEIPReturnCode
from helpers.udp_helpers import udp_receive_responses, udp_send_concatenated

#: Service config with a UDP endpoint. The TCP tests are in ``test_msg_format_tcp.py``.
SOMEIP_CONFIG: str = "tc8_someipd_sd.json"

#: Expected failure for tests that need a SOME/IP method reply from the DUT.
NO_ECHO_METHODS = pytest.mark.xfail(
    strict=True,
    reason="OA SOMEIPSRV_ONWIRE and SOMEIPSRV_RPC tests: a method REQUEST is expected to get a RESPONSE or ERROR. "
    "Observed: a REQUEST gets no reply, except one ERROR for a REQUEST with Protocol Version 0xFF.",
)

_UNKNOWN_METHOD_ID: int = 0xBEEF
_UNKNOWN_SERVICE_ID: int = 0xBEEF
_UNKNOWN_INTERFACE_VERSION: int = 0xFF
_CLIENT_ID: int = 0x0010
_REPLY_SECS: float = 3.0
_LISTEN_SECS: float = 2.0
_BURST: int = 10


@dataclasses.dataclass(frozen=True)
class _Reply:
    """A parsed reply with its raw bytes and source address."""

    msg: SOMEIPHeader
    raw: bytes
    addr: Tuple[str, int]


def _parse(raw: bytes) -> SOMEIPHeader:
    try:
        msg, _ = SOMEIPHeader.parse(raw)
    except ValueError as exc:
        pytest.fail(f"reply is not a valid SOME/IP message ({exc}): {raw.hex()}")
    return msg


def _udp_socket() -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("", 0))
    return sock


def _request(dut_ip: str, data: bytes, timeout_secs: float = _REPLY_SECS) -> _Reply:
    """Send *data* to the DUT over UDP and return the first reply; fail if none arrives."""
    sock = _udp_socket()
    try:
        sock.sendto(data, (dut_ip, DUT_UNRELIABLE_PORT))
        sock.settimeout(timeout_secs)
        try:
            raw, addr = sock.recvfrom(65535)
        except socket.timeout:
            pytest.fail(f"no reply within {timeout_secs} s")
        return _Reply(_parse(raw), raw, addr)
    finally:
        sock.close()


def _listen(dut_ip: str, data: bytes, window_secs: float = _LISTEN_SECS) -> List[_Reply]:
    """Send *data* to the DUT over UDP and return every reply received within the window."""
    sock = _udp_socket()
    replies: List[_Reply] = []
    try:
        sock.sendto(data, (dut_ip, DUT_UNRELIABLE_PORT))
        deadline = time.monotonic() + window_secs
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(remaining)
            try:
                raw, addr = sock.recvfrom(65535)
            except socket.timeout:
                break
            replies.append(_Reply(_parse(raw), raw, addr))
    finally:
        sock.close()
    return replies


def _assert_ok_response(msg: SOMEIPHeader, method_id: int = METHOD_ECHO_UINT8) -> None:
    assert_valid_response(msg, SERVICE_ID, method_id)
    assert_return_code(msg, SOMEIPReturnCode.E_OK)


def _assert_request_id(msg: SOMEIPHeader, client_id: int, session_id: int) -> None:
    assert (msg.client_id, msg.session_id) == (client_id, session_id), (
        f"Request ID (client, session) = (0x{msg.client_id:04x}, 0x{msg.session_id:04x}), "
        f"expected (0x{client_id:04x}, 0x{session_id:04x})"
    )


def _assert_error(
    msg: SOMEIPHeader,
    service_id: int,
    method_id: int,
    code: SOMEIPReturnCode,
    client_id: int,
    session_id: int,
) -> None:
    """ERROR reply with the given return code, copying Message ID and Request ID of the request."""
    assert msg.message_type == SOMEIPMessageType.ERROR, (
        f"message type 0x{msg.message_type:02x}, expected ERROR (0x{SOMEIPMessageType.ERROR:02x})"
    )
    assert_return_code(msg, code)
    assert (msg.service_id, msg.method_id) == (service_id, method_id), (
        f"Message ID = (0x{msg.service_id:04x}, 0x{msg.method_id:04x}), expected (0x{service_id:04x}, 0x{method_id:04x})"
    )
    _assert_request_id(msg, client_id, session_id)


def _control(dut_ip: str) -> None:
    """A valid REQUEST must get a RESPONSE, so that a later absence of replies is not vacuous."""
    reply = _request(dut_ip, build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x0001))
    _assert_ok_response(reply.msg)


def _assert_no_message_type(replies: List[_Reply], message_type: SOMEIPMessageType, label: str) -> None:
    found = [r.msg for r in replies if r.msg.message_type == message_type]
    assert not found, f"{label}: {len(found)} message(s) of type {message_type.name} received"


def _assert_none_or_error(replies: List[_Reply], code: SOMEIPReturnCode, label: str) -> None:
    """The DUT either ignores the message or answers with an ERROR carrying *code*."""
    for reply in replies:
        assert reply.msg.message_type == SOMEIPMessageType.ERROR, (
            f"{label}: message type 0x{reply.msg.message_type:02x}, expected ERROR or no reply"
        )
        assert_return_code(reply.msg, code)


class TestMessageFormat:
    """SOMEIPSRV_BASIC and SOMEIPSRV_ONWIRE tests (Sections 6.1.5.5 and 6.1.5.6)."""

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_basic_01_define_service_using_service_id(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-001 (SOMEIPSRV_BASIC_01): REQUEST to a known Service ID gets a RESPONSE with E_OK."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x0001)
        _assert_ok_response(_request(dut_ip, req).msg)

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_01_response_addresses(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-002 (SOMEIPSRV_ONWIRE_01): RESPONSE source address and port match the REQUEST destination."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x0002)
        reply = _request(dut_ip, req)
        _assert_ok_response(reply.msg)
        assert reply.addr == (dut_ip, DUT_UNRELIABLE_PORT), (
            f"TC8-MSG-002: RESPONSE source {reply.addr}, expected {(dut_ip, DUT_UNRELIABLE_PORT)}"
        )

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_02_response_method_id_msb(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-003 (SOMEIPSRV_ONWIRE_02): the Method ID of a RESPONSE has bit 15 set to 0."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x0003)
        msg = _request(dut_ip, req).msg
        _assert_ok_response(msg)
        assert msg.method_id & 0x8000 == 0, f"TC8-MSG-003: RESPONSE Method ID 0x{msg.method_id:04x} has bit 15 set"

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_03_copy_request_id(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-004 (SOMEIPSRV_ONWIRE_03): the RESPONSE copies client ID and session ID of the REQUEST."""
        assert dut.poll() is None, "DUT is not running"
        client_id, session_id = 0x0011, 0x1234
        req = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=client_id, session_id=session_id)

        udp = _request(dut_ip, req).msg
        _assert_ok_response(udp)
        _assert_request_id(udp, client_id, session_id)

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_04_request_id_reuse(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-005 (SOMEIPSRV_ONWIRE_04): a Request ID can be reused once its RESPONSE has arrived."""
        assert dut.poll() is None, "DUT is not running"
        client_id, session_id = 0x0001, 0x0042
        req = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=client_id, session_id=session_id)
        for _ in range(2):
            msg = _request(dut_ip, req).msg
            _assert_ok_response(msg)
            _assert_request_id(msg, client_id, session_id)

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_05_protocol_version(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-006 (SOMEIPSRV_ONWIRE_05): Protocol Version 0xFF is answered with an ERROR, 0x01 with a RESPONSE."""
        assert dut.poll() is None, "DUT is not running"
        wrong = build_wrong_protocol_version_request(SERVICE_ID, METHOD_ECHO_UINT8, _CLIENT_ID, 0x0005)
        error = _request(dut_ip, wrong).msg
        _assert_error(
            error, SERVICE_ID, METHOD_ECHO_UINT8, SOMEIPReturnCode.E_WRONG_PROTOCOL_VERSION, _CLIENT_ID, 0x0005
        )

        valid = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x0006)
        msg = _request(dut_ip, valid).msg
        _assert_ok_response(msg)
        _assert_request_id(msg, _CLIENT_ID, 0x0006)

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_06_interface_version(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-007 (SOMEIPSRV_ONWIRE_06): Interface Version of the RESPONSE and ERROR for an unknown version."""
        assert dut.poll() is None, "DUT is not running"
        valid = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x0007)
        msg = _request(dut_ip, valid).msg
        _assert_ok_response(msg)
        _assert_request_id(msg, _CLIENT_ID, 0x0007)
        assert msg.interface_version == MAJOR_VERSION, (
            f"TC8-MSG-007: RESPONSE Interface Version 0x{msg.interface_version:02x}, expected 0x{MAJOR_VERSION:02x}"
        )

        unknown = build_request(
            SERVICE_ID,
            METHOD_ECHO_UINT8,
            client_id=_CLIENT_ID,
            session_id=0x0008,
            interface_version=_UNKNOWN_INTERFACE_VERSION,
        )
        error = _request(dut_ip, unknown).msg
        _assert_error(
            error, SERVICE_ID, METHOD_ECHO_UINT8, SOMEIPReturnCode.E_WRONG_INTERFACE_VERSION, _CLIENT_ID, 0x0008
        )
        assert error.interface_version == _UNKNOWN_INTERFACE_VERSION, (
            f"TC8-MSG-007: ERROR Interface Version 0x{error.interface_version:02x}, "
            f"expected 0x{_UNKNOWN_INTERFACE_VERSION:02x}"
        )

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_07_message_type_response(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-008 (SOMEIPSRV_ONWIRE_07): a REQUEST gets a RESPONSE with message type 0x80."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x0009)
        msg = _request(dut_ip, req).msg
        _assert_ok_response(msg)
        _assert_request_id(msg, _CLIENT_ID, 0x0009)

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_error_codes"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_10_unknown_service(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-009 (SOMEIPSRV_ONWIRE_10): a REQUEST to an unknown Service ID gets an ERROR with E_UNKNOWN_SERVICE."""
        assert dut.poll() is None, "DUT is not running"
        _control(dut_ip)
        req = build_request(_UNKNOWN_SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x000A)
        error = _request(dut_ip, req).msg
        _assert_error(
            error, _UNKNOWN_SERVICE_ID, METHOD_ECHO_UINT8, SOMEIPReturnCode.E_UNKNOWN_SERVICE, _CLIENT_ID, 0x000A
        )

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_11_return_code_ok(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-010 (SOMEIPSRV_ONWIRE_11): a normal RESPONSE carries return code E_OK."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x000B)
        _assert_ok_response(_request(dut_ip, req).msg)

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_error_codes"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_12_unknown_method(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-011 (SOMEIPSRV_ONWIRE_12): a REQUEST with an unknown Method ID gets an ERROR with E_UNKNOWN_METHOD."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request(SERVICE_ID, _UNKNOWN_METHOD_ID, client_id=_CLIENT_ID, session_id=0x000C)
        error = _request(dut_ip, req).msg
        _assert_error(error, SERVICE_ID, _UNKNOWN_METHOD_ID, SOMEIPReturnCode.E_UNKNOWN_METHOD, _CLIENT_ID, 0x000C)


class TestProtocolCompliance:
    """SOMEIPSRV_RPC and SOMEIP_ETS tests (Sections 6.1.5.7 and 6.1.6)."""

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_04_fire_and_forget(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-012 (SOMEIPSRV_RPC_04): a REQUEST_NO_RETURN gets no RESPONSE."""
        assert dut.poll() is None, "DUT is not running"
        _control(dut_ip)
        req = build_request_no_return(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x000D)
        _assert_no_message_type(_listen(dut_ip, req), SOMEIPMessageType.RESPONSE, "TC8-MSG-012")

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_05_fire_and_forget_no_error(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-013 (SOMEIPSRV_RPC_05): a REQUEST_NO_RETURN to an unknown Method ID gets no ERROR."""
        assert dut.poll() is None, "DUT is not running"
        _control(dut_ip)
        req = build_request_no_return(SERVICE_ID, _UNKNOWN_METHOD_ID, client_id=_CLIENT_ID, session_id=0x000E)
        _assert_no_message_type(_listen(dut_ip, req), SOMEIPMessageType.ERROR, "TC8-MSG-013")

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_06_return_code_two_msb_zero(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-014 (SOMEIPSRV_RPC_06): the two most significant bits of an ERROR return code are 0."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request(SERVICE_ID, _UNKNOWN_METHOD_ID, client_id=_CLIENT_ID, session_id=0x000F)
        error = _request(dut_ip, req).msg
        assert error.message_type == SOMEIPMessageType.ERROR, (
            f"TC8-MSG-014: message type 0x{error.message_type:02x}, expected ERROR"
        )
        assert int(error.return_code) & 0xC0 == 0, (
            f"TC8-MSG-014: return code 0x{int(error.return_code):02x} has one of the two most significant bits set"
        )

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_07_ignore_return_code_bits(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-015 (SOMEIPSRV_RPC_07): the two most significant bits of the REQUEST return code are ignored."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request_with_return_code(
            SERVICE_ID, METHOD_ECHO_UINT8, return_code=0xC0, client_id=_CLIENT_ID, session_id=0x0010
        )
        msg = _request(dut_ip, req).msg
        _assert_ok_response(msg)
        _assert_request_id(msg, _CLIENT_ID, 0x0010)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason="OA SOMEIPSRV_RPC_08: a REQUEST that already carries an error return code is expected to get no ERROR. "
        "Observed: the control REQUEST gets no RESPONSE.",
    )
    def test_rpc_08_no_reply_to_error(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-016 (SOMEIPSRV_RPC_08): a REQUEST that already carries an error return code gets no ERROR."""
        assert dut.poll() is None, "DUT is not running"
        _control(dut_ip)
        for return_code, session_id in ((0x01, 0x0011), (0x1F, 0x0012)):
            req = build_request_with_return_code(
                SERVICE_ID, METHOD_ECHO_UINT8, return_code=return_code, client_id=_CLIENT_ID, session_id=session_id
            )
            _assert_no_message_type(
                _listen(dut_ip, req), SOMEIPMessageType.ERROR, f"TC8-MSG-016 (return code 0x{return_code:02x})"
            )

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_error_codes"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_09_error_without_payload(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-017 (SOMEIPSRV_RPC_09): an ERROR carries no payload, its Length field is 8."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request(SERVICE_ID, _UNKNOWN_METHOD_ID, client_id=_CLIENT_ID, session_id=0x0013)
        reply = _request(dut_ip, req)
        _assert_error(reply.msg, SERVICE_ID, _UNKNOWN_METHOD_ID, SOMEIPReturnCode.E_UNKNOWN_METHOD, _CLIENT_ID, 0x0013)
        length = int.from_bytes(reply.raw[4:8], "big")
        assert length == 8, f"TC8-MSG-017: ERROR Length field {length}, expected 8"

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_18_copy_message_id(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-018 (SOMEIPSRV_RPC_18): the ERROR copies Service ID and Method ID of the REQUEST."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request(SERVICE_ID, _UNKNOWN_METHOD_ID, client_id=_CLIENT_ID, session_id=0x0014)
        error = _request(dut_ip, req).msg
        _assert_error(error, SERVICE_ID, _UNKNOWN_METHOD_ID, SOMEIPReturnCode.E_UNKNOWN_METHOD, _CLIENT_ID, 0x0014)

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_19_copy_request_id(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-019 (SOMEIPSRV_RPC_19): the ERROR copies client ID and session ID of the REQUEST."""
        assert dut.poll() is None, "DUT is not running"
        client_id, session_id = 0x0021, 0x0016
        req = build_request(SERVICE_ID, _UNKNOWN_METHOD_ID, client_id=client_id, session_id=session_id)
        error = _request(dut_ip, req).msg
        _assert_error(error, SERVICE_ID, _UNKNOWN_METHOD_ID, SOMEIPReturnCode.E_UNKNOWN_METHOD, client_id, session_id)

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_20_copy_interface_version(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-020 (SOMEIPSRV_RPC_20): the ERROR copies the Interface Version of the REQUEST."""
        assert dut.poll() is None, "DUT is not running"
        req = build_request(
            SERVICE_ID,
            _UNKNOWN_METHOD_ID,
            client_id=_CLIENT_ID,
            session_id=0x0017,
            interface_version=MAJOR_VERSION,
        )
        error = _request(dut_ip, req).msg
        _assert_error(error, SERVICE_ID, _UNKNOWN_METHOD_ID, SOMEIPReturnCode.E_UNKNOWN_METHOD, _CLIENT_ID, 0x0017)
        assert error.interface_version == MAJOR_VERSION, (
            f"TC8-MSG-020: ERROR Interface Version 0x{error.interface_version:02x}, expected 0x{MAJOR_VERSION:02x}"
        )

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_004_burst(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-021 (SOMEIP_ETS_004): a burst of REQUESTs gets a RESPONSE to each request."""
        assert dut.poll() is None, "DUT is not running"
        base_session_id = 0x0100
        sock = _udp_socket()
        try:
            for i in range(_BURST):
                req = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=base_session_id + i)
                sock.sendto(req, (dut_ip, DUT_UNRELIABLE_PORT))
            try:
                responses = udp_receive_responses(sock, count=_BURST, timeout_secs=5.0)
            except socket.timeout as exc:
                pytest.fail(f"TC8-MSG-021: {exc}")
        finally:
            sock.close()
        for resp in responses:
            _assert_ok_response(resp)
        sessions = sorted(resp.session_id for resp in responses)
        assert sessions == [base_session_id + i for i in range(_BURST)], (
            f"TC8-MSG-021: session IDs of the RESPONSEs {[hex(s) for s in sessions]}"
        )

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_malformed"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_058_length_way_too_long(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-022 (SOMEIP_ETS_058): a message with an oversized Length is answered with an ERROR or ignored."""
        assert dut.poll() is None, "DUT is not running"
        _control(dut_ip)

        longer = bytearray(build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x0019))
        longer[4:8] = (0xFFFFFFF0).to_bytes(4, "big")
        oversized = (
            build_oversized_message(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x0018),
            bytes(longer),
        )
        for data in oversized:
            _assert_none_or_error(_listen(dut_ip, data), SOMEIPReturnCode.E_MALFORMED_MESSAGE, "TC8-MSG-022")

        assert dut.poll() is None, "TC8-MSG-022: DUT is not running after the oversized messages"
        follow_up = build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x001A)
        _assert_ok_response(_request(dut_ip, follow_up).msg)

    @NO_ECHO_METHODS
    @add_test_properties(
        partially_verifies=["comp_req__tc8_conformance__msg_malformed"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_075_wrong_message_type(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-023 (SOMEIP_ETS_075): a message with a wrong Message Type is answered with an ERROR or ignored."""
        assert dut.poll() is None, "DUT is not running"
        _control(dut_ip)

        reserved = bytearray(build_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x001C))
        reserved[14] = 0x04
        wrong_types = (
            build_notification_as_request(SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x001B),
            bytes(reserved),
        )
        for data in wrong_types:
            _assert_none_or_error(_listen(dut_ip, data), SOMEIPReturnCode.E_WRONG_MESSAGE_TYPE, "TC8-MSG-023")

    @NO_ECHO_METHODS
    @add_test_properties(
        partially_verifies=["comp_req__tc8_conformance__msg_error_codes"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_077_wrong_service_id(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-024 (SOMEIP_ETS_077): a message with a wrong Service ID is answered with an ERROR or ignored."""
        assert dut.poll() is None, "DUT is not running"
        _control(dut_ip)
        req = build_request(_UNKNOWN_SERVICE_ID, METHOD_ECHO_UINT8, client_id=_CLIENT_ID, session_id=0x001D)
        _assert_none_or_error(_listen(dut_ip, req), SOMEIPReturnCode.E_UNKNOWN_SERVICE, "TC8-MSG-024")

    @NO_ECHO_METHODS
    @add_test_properties(
        partially_verifies=["comp_req__tc8_conformance__msg_malformed"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_078_wrong_protocol_version(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-MSG-025 (SOMEIP_ETS_078): a message with a wrong Protocol Version is answered with an ERROR or ignored."""
        assert dut.poll() is None, "DUT is not running"
        _control(dut_ip)
        req = build_wrong_protocol_version_request(SERVICE_ID, METHOD_ECHO_UINT8, _CLIENT_ID, 0x001E)
        _assert_none_or_error(_listen(dut_ip, req), SOMEIPReturnCode.E_WRONG_PROTOCOL_VERSION, "TC8-MSG-025")


class TestUdpTransport:
    """SOME/IP request handling over UDP (Section 6.1.6)."""

    @NO_ECHO_METHODS
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__udp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_069_unaligned_messages_udp(self, dut: subprocess.Popen[bytes], dut_ip: str) -> None:
        """TC8-UDP-001 (SOMEIP_ETS_069): three REQUESTs in one UDP datagram, the third at offset 34, get a RESPONSE each."""
        assert dut.poll() is None, "DUT is not running"
        sessions = (0x0081, 0x0082, 0x0083)
        messages = [
            build_request(SERVICE_ID, METHOD_ECHO_UINT8_ARRAY, client_id=_CLIENT_ID, session_id=s, payload=p)
            for s, p in zip(sessions, (b"", b"\xaa\xbb", b""))
        ]
        sock = _udp_socket()
        try:
            udp_send_concatenated(sock, (dut_ip, DUT_UNRELIABLE_PORT), messages)
            try:
                responses = udp_receive_responses(sock, count=3, timeout_secs=5.0)
            except socket.timeout as exc:
                pytest.fail(f"TC8-UDP-001: {exc}")
        finally:
            sock.close()
        for resp in responses:
            _assert_ok_response(resp, METHOD_ECHO_UINT8_ARRAY)
        assert sorted(r.session_id for r in responses) == list(sessions), (
            f"TC8-UDP-001: session IDs of the RESPONSEs {[hex(r.session_id) for r in responses]}"
        )
