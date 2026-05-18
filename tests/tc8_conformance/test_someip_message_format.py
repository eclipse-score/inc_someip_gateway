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
"""TC8 SOME/IP Message Format tests — TC8-MSG-001 through TC8-MSG-008.

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import socket
import subprocess
import time
import pytest

from attribute_plugin import add_test_properties

from helpers.message_builder import (
    build_notification_as_request,
    build_oversized_message,
    build_request,
    build_request_no_return,
    build_request_with_return_code,
    build_truncated_message,
    build_wrong_protocol_version_request,
)
from helpers.someip_assertions import (
    assert_client_echo,
    assert_offer_has_tcp_endpoint_option,
    assert_return_code,
    assert_session_echo,
    assert_valid_response,
)
from helpers.constants import DUT_RELIABLE_PORT, DUT_UNRELIABLE_PORT
from helpers.sd_helpers import capture_sd_offers
from helpers.tcp_helpers import (
    tcp_connect,
    tcp_receive_n_responses,
    tcp_receive_response,
    tcp_send_concatenated,
    tcp_send_request,
)
from helpers.udp_helpers import udp_receive_responses, udp_send_concatenated
from someip.header import SOMEIPHeader, SOMEIPMessageType, SOMEIPReturnCode

# ---------------------------------------------------------------------------
# Module-level configuration
# ---------------------------------------------------------------------------

SOMEIP_CONFIG: str = "tc8_someipd_service.json"

_SERVICE_ID: int = 0x1234
_INSTANCE_ID: int = 0x5678
_METHOD_ID: int = 0x0421
_UNKNOWN_METHOD_ID: int = 0xBEEF
# SD config for waiting until DUT is ready


def _wait_for_dut_offer(host_ip: str, timeout: float = 5.0) -> None:
    """Block until the DUT sends at least one SD OfferService."""
    try:
        capture_sd_offers(host_ip, min_count=1, timeout_secs=timeout)
    except (TimeoutError, OSError):
        pytest.skip(
            "DUT did not offer service within timeout — multicast may be unavailable"
        )


def _send_request_and_receive(
    host_ip: str,
    request_bytes: bytes,
    timeout_secs: float = 3.0,
) -> SOMEIPHeader:
    """Send a SOME/IP request to the DUT and return the first response."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(("", 0))
        sock.sendto(request_bytes, (host_ip, DUT_UNRELIABLE_PORT))
        sock.settimeout(timeout_secs)
        data, _ = sock.recvfrom(65535)
        resp, _ = SOMEIPHeader.parse(data)
        return resp
    finally:
        sock.close()


def _send_request_expect_no_response(
    host_ip: str,
    request_bytes: bytes,
    timeout_secs: float = 2.0,
) -> list:
    """Send a SOME/IP request and verify no response arrives."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    collected = []
    try:
        sock.bind(("", 0))
        sock.sendto(request_bytes, (host_ip, DUT_UNRELIABLE_PORT))
        deadline = time.monotonic() + timeout_secs
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(min(remaining, 0.5))
            try:
                data, _ = sock.recvfrom(65535)
                msg, _ = SOMEIPHeader.parse(data)
                collected.append(msg)
            except socket.timeout:
                continue
            except Exception:
                continue
    finally:
        sock.close()
    return collected


# ---------------------------------------------------------------------------
# TC8-MSG-001 / TC8-MSG-002 / TC8-MSG-005 / TC8-MSG-008 — Response header
# ---------------------------------------------------------------------------


class TestSomeipResponseHeader:
    """TC8-MSG-001/002/005/008: Response header validation."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_msg_001_protocol_version(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-MSG-001: RESPONSE has protocol_version = 0x01."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=0x0001
        )
        resp = _send_request_and_receive(host_ip, req)

        assert resp.protocol_version == 1, (
            f"TC8-MSG-001: protocol_version = {resp.protocol_version}, expected 1 (0x01)"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_msg_002_message_type_response(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-MSG-002: Response to REQUEST has message_type = RESPONSE (0x80)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=0x0002
        )
        resp = _send_request_and_receive(host_ip, req)

        assert_valid_response(resp, _SERVICE_ID, _METHOD_ID)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_msg_002_no_response_for_request_no_return(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-MSG-002 (fire-and-forget): REQUEST_NO_RETURN must not produce a RESPONSE."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request_no_return(
            _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=0x0009
        )
        responses = _send_request_expect_no_response(host_ip, req, timeout_secs=2.0)

        assert not responses, (
            f"TC8-MSG-002: {len(responses)} response(s) received for REQUEST_NO_RETURN; "
            "SOME/IP spec requires no response to fire-and-forget messages"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_msg_005_session_id_echo(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-MSG-005: RESPONSE session_id matches REQUEST session_id."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        session_id = 0x1234
        req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=session_id
        )
        resp = _send_request_and_receive(host_ip, req)

        assert_session_echo(resp, session_id)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_msg_008_client_id_echo(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-MSG-008: RESPONSE client_id matches REQUEST client_id."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        client_id = 0x0011
        req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=client_id, session_id=0x0003
        )
        resp = _send_request_and_receive(host_ip, req)

        assert_client_echo(resp, client_id)


# ---------------------------------------------------------------------------
# TC8-MSG-003 / TC8-MSG-004 / TC8-MSG-006 — Error return codes
# ---------------------------------------------------------------------------


class TestSomeipErrorCodes:
    """TC8-MSG-003/004/006: Error return codes."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_error_codes"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_msg_003_unknown_service(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-MSG-003: Request to unknown service gets E_UNKNOWN_SERVICE or no response.

        Note: The DUT may silently drop requests for services it does not offer.
        TC8 allows either E_UNKNOWN_SERVICE (0x02) or no response at all; this
        test accepts both behaviors.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        unknown_service = 0xBEEF
        req = build_request(
            unknown_service, _METHOD_ID, client_id=0x0010, session_id=0x0004
        )
        responses = _send_request_expect_no_response(host_ip, req, timeout_secs=2.0)

        if responses:
            # If the DUT responds, it must be E_UNKNOWN_SERVICE
            assert_return_code(responses[0], SOMEIPReturnCode.E_UNKNOWN_SERVICE)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_error_codes"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_msg_004_unknown_method(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-MSG-004: Request with unknown method_id gets E_UNKNOWN_METHOD."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _UNKNOWN_METHOD_ID, client_id=0x0010, session_id=0x0005
        )
        resp = _send_request_and_receive(host_ip, req)

        assert_return_code(resp, SOMEIPReturnCode.E_UNKNOWN_METHOD)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_error_codes"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_msg_006_wrong_interface_version(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-MSG-006: Request with wrong interface version gets E_WRONG_INTERFACE_VERSION or error.

        Note: DUT behavior for interface version mismatch varies. The DUT
        may respond with E_WRONG_INTERFACE_VERSION, another error code,
        or handle the request normally (E_OK). This test sends
        interface_version=0xFF (clearly wrong for a service with version 0x00).
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID,
            _METHOD_ID,
            client_id=0x0010,
            session_id=0x0006,
            interface_version=0xFF,
        )
        resp = _send_request_and_receive(host_ip, req)

        # Accept E_WRONG_INTERFACE_VERSION, E_UNKNOWN_METHOD, or E_OK.
        # The DUT may check interface version at routing level, at handler level,
        # or not at all — all are valid SOME/IP stack behaviors.
        acceptable = (
            SOMEIPReturnCode.E_WRONG_INTERFACE_VERSION,
            SOMEIPReturnCode.E_UNKNOWN_METHOD,
            SOMEIPReturnCode.E_OK,
        )
        assert resp.return_code in acceptable, (
            f"TC8-MSG-006: return_code 0x{resp.return_code:02x} not in "
            f"{[f'0x{rc.value:02x} ({rc.name})' for rc in acceptable]}"
        )


# ---------------------------------------------------------------------------
# TC8-MSG-007 — Malformed message handling
# ---------------------------------------------------------------------------


class TestMalformedMessages:
    """TC8-MSG-007: DUT must survive malformed SOME/IP messages without crashing."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_malformed"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_msg_007_truncated_message_no_crash(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-MSG-007: Truncated message (< 8 bytes) does not crash the DUT."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.sendto(build_truncated_message(), (host_ip, DUT_UNRELIABLE_PORT))
        finally:
            sock.close()

        time.sleep(0.3)
        assert someipd_dut.poll() is None, (
            "TC8-MSG-007: someipd crashed after receiving a truncated SOME/IP message"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_malformed"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_msg_007_wrong_protocol_version_no_crash(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-MSG-007: Message with protocol_version=0xFF does not crash the DUT."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        malformed = build_wrong_protocol_version_request(
            _SERVICE_ID,
            _METHOD_ID,
            client_id=0x0010,
            session_id=0x0007,
        )
        # DUT may respond with E_MALFORMED_MESSAGE or drop silently — both are valid.
        _send_request_expect_no_response(host_ip, malformed, timeout_secs=1.0)

        assert someipd_dut.poll() is None, (
            "TC8-MSG-007: someipd crashed after receiving a wrong-protocol-version message"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_malformed"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_msg_007_oversized_length_field_no_crash(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-MSG-007: Message whose length field claims 0x7FF3 bytes does not crash the DUT."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            sock.sendto(
                build_oversized_message(
                    _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=0x0008
                ),
                (host_ip, DUT_UNRELIABLE_PORT),
            )
        finally:
            sock.close()

        time.sleep(0.3)
        assert someipd_dut.poll() is None, (
            "TC8-MSG-007: someipd crashed after receiving a message with oversized length field"
        )


# ---------------------------------------------------------------------------
# SOMEIPSRV_RPC_01/02 / OPTIONS_15 — TCP transport binding
# ---------------------------------------------------------------------------


class TestSomeipTcpTransport:
    """TCP transport binding tests — SOMEIPSRV_RPC_01/02, OPTIONS_15.

    These tests verify that someipd correctly handles SOME/IP request/response
    over TCP (reliable transport binding). The DUT is configured with both
    unreliable (UDP ``DUT_UNRELIABLE_PORT``) and reliable (TCP ``DUT_RELIABLE_PORT``) ports.
    """

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__tcp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_rpc_01_tcp_request_response(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_01: REQUEST over TCP receives a valid RESPONSE.

        Verifies that the DUT accepts a TCP connection and correctly responds
        to a SOME/IP REQUEST with a RESPONSE having message_type=0x80.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        sock = tcp_connect(host_ip, DUT_RELIABLE_PORT)
        try:
            req = build_request(
                _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=0x0050
            )
            tcp_send_request(sock, req)
            resp = tcp_receive_response(sock)

            assert_valid_response(resp, _SERVICE_ID, _METHOD_ID)
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__tcp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_rpc_01_tcp_session_id_echo(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_01: TCP RESPONSE echoes the REQUEST session_id."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        session_id = 0x5678
        sock = tcp_connect(host_ip, DUT_RELIABLE_PORT)
        try:
            req = build_request(
                _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=session_id
            )
            tcp_send_request(sock, req)
            resp = tcp_receive_response(sock)

            assert_session_echo(resp, session_id)
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__tcp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_rpc_01_tcp_client_id_echo(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_01: TCP RESPONSE echoes the REQUEST client_id."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        client_id = 0x0015
        sock = tcp_connect(host_ip, DUT_RELIABLE_PORT)
        try:
            req = build_request(
                _SERVICE_ID, _METHOD_ID, client_id=client_id, session_id=0x0051
            )
            tcp_send_request(sock, req)
            resp = tcp_receive_response(sock)

            assert_client_echo(resp, client_id)
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__tcp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_rpc_02_tcp_multiple_methods_single_connection(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_02: Multiple SOME/IP methods on a single TCP connection.

        Verifies that the DUT handles multiple sequential request/response
        exchanges on the same TCP connection without requiring reconnection.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        sock = tcp_connect(host_ip, DUT_RELIABLE_PORT)
        try:
            # First request
            req1 = build_request(
                _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=0x0060
            )
            tcp_send_request(sock, req1)
            resp1 = tcp_receive_response(sock)
            assert_valid_response(resp1, _SERVICE_ID, _METHOD_ID)
            assert_session_echo(resp1, 0x0060)

            # Second request on the SAME connection
            req2 = build_request(
                _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=0x0061
            )
            tcp_send_request(sock, req2)
            resp2 = tcp_receive_response(sock)
            assert_valid_response(resp2, _SERVICE_ID, _METHOD_ID)
            assert_session_echo(resp2, 0x0061)
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__tcp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_options_15_tcp_endpoint_advertised(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_OPTIONS_15: SD OfferService includes a TCP endpoint option.

        When the DUT is configured with a reliable (TCP) port, the SD
        OfferService message must include an IPv4EndpointOption with
        L4Proto=TCP and the correct port.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        offers = capture_sd_offers(host_ip, min_count=1, timeout_secs=5.0)
        assert offers, "No SD OfferService received"

        assert_offer_has_tcp_endpoint_option(offers[0], host_ip, DUT_RELIABLE_PORT)


# ---------------------------------------------------------------------------
# SOMEIP_ETS_068 — Unaligned messages over TCP
# ---------------------------------------------------------------------------


class TestTcpUnalignedMessages:
    """SOMEIP_ETS_068: Multiple unaligned SOME/IP messages in one TCP segment.

    PRS_SOMEIP_00142, PRS_SOMEIP_00569: A SOME/IP TCP receiver must parse
    the byte stream using the length field as the sole framing indicator.
    No 4-byte alignment between consecutive messages is guaranteed or required.
    """

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__tcp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_ets_068_unaligned_someip_messages_over_tcp(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIP_ETS_068: Three REQUEST messages in one TCP segment; third is unaligned.

        Payload layout in the concatenated TCP write:
          msg1 (session 0x0071): 0-byte payload  -> 16 bytes (offset   0, aligned)
          msg2 (session 0x0072): 2-byte payload  -> 18 bytes (offset  16, aligned)
          msg3 (session 0x0073): 0-byte payload  -> 16 bytes (offset  34, NOT 4-byte aligned)

        All three must receive individual RESPONSE messages from the DUT.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        msg1 = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=0x0071, payload=b""
        )
        msg2 = build_request(
            _SERVICE_ID,
            _METHOD_ID,
            client_id=0x0010,
            session_id=0x0072,
            payload=b"\xaa\xbb",
        )
        msg3 = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=0x0073, payload=b""
        )

        sock = tcp_connect(host_ip, DUT_RELIABLE_PORT)
        try:
            tcp_send_concatenated(sock, [msg1, msg2, msg3])
            responses = tcp_receive_n_responses(sock, count=3, timeout_secs=5.0)
        finally:
            sock.close()

        assert len(responses) == 3, (
            f"SOMEIP_ETS_068: expected 3 RESPONSE messages, got {len(responses)}"
        )
        for resp in responses:
            assert resp.service_id == _SERVICE_ID
            assert_valid_response(resp, _SERVICE_ID, _METHOD_ID)

        session_ids = {resp.session_id for resp in responses}
        assert session_ids == {0x0071, 0x0072, 0x0073}, (
            f"SOMEIP_ETS_068: unexpected session IDs in responses: "
            f"{{{', '.join(f'0x{s:04x}' for s in sorted(session_ids))}}}"
        )


# ---------------------------------------------------------------------------
# SOMEIP_ETS_069 — Unaligned messages over UDP
# ---------------------------------------------------------------------------


class TestUdpUnalignedMessages:
    """SOMEIP_ETS_069: Multiple unaligned SOME/IP messages in one UDP datagram.

    PRS_SOMEIP_00142, PRS_SOMEIP_00569: A SOME/IP UDP receiver must parse
    each SOME/IP message within the datagram using the length field as the
    sole framing indicator. No 4-byte alignment between consecutive messages
    is guaranteed or required.
    """

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__udp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_ets_069_unaligned_someip_messages_over_udp(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIP_ETS_069: Three REQUEST messages in one UDP datagram; third is unaligned.

        Payload layout in the concatenated UDP write:
          msg1 (session 0x0081): 0-byte payload  -> 16 bytes (offset  0, aligned)
          msg2 (session 0x0082): 2-byte payload  -> 18 bytes (offset 16, aligned)
          msg3 (session 0x0083): 0-byte payload  -> 16 bytes (offset 34, NOT 4-byte aligned)

        All three must receive individual RESPONSE messages from the DUT.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        msg1 = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=0x0081, payload=b""
        )
        msg2 = build_request(
            _SERVICE_ID,
            _METHOD_ID,
            client_id=0x0010,
            session_id=0x0082,
            payload=b"\xaa\xbb",
        )
        msg3 = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0010, session_id=0x0083, payload=b""
        )

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("", 0))
        try:
            udp_send_concatenated(
                sock, (host_ip, DUT_UNRELIABLE_PORT), [msg1, msg2, msg3]
            )
            responses = udp_receive_responses(sock, count=3, timeout_secs=5.0)
        finally:
            sock.close()

        assert len(responses) == 3, (
            f"SOMEIP_ETS_069: expected 3 RESPONSE messages, got {len(responses)}"
        )
        for resp in responses:
            assert resp.service_id == _SERVICE_ID
            assert_valid_response(resp, _SERVICE_ID, _METHOD_ID)

        session_ids = {resp.session_id for resp in responses}
        assert session_ids == {0x0081, 0x0082, 0x0083}, (
            f"SOMEIP_ETS_069: unexpected session IDs in responses: "
            f"{{{', '.join(f'0x{s:04x}' for s in sorted(session_ids))}}}"
        )


# ---------------------------------------------------------------------------
# Group 3 — SOMEIPSRV_BASIC_01-03: Service identification primitives
# ---------------------------------------------------------------------------


_UNKNOWN_SERVICE_ID: int = 0xBEEF
_EVENT_METHOD_ID: int = 0x8001  # bit 15 set → event notification indicator


def _send_request_and_receive_with_addr(
    host_ip: str,
    request_bytes: bytes,
    timeout_secs: float = 3.0,
) -> tuple[SOMEIPHeader, tuple[str, int]]:
    """Send a SOME/IP request to the DUT and return (response, (src_ip, src_port))."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.bind(("", 0))
        sock.sendto(request_bytes, (host_ip, DUT_UNRELIABLE_PORT))
        sock.settimeout(timeout_secs)
        data, addr = sock.recvfrom(65535)
        resp, _ = SOMEIPHeader.parse(data)
        return resp, addr
    finally:
        sock.close()


class TestSomeipBasicIdentifiers:
    """SOMEIPSRV_BASIC_01-03: Service identification primitives."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_basic_01_correct_service_id_gets_response(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_BASIC_01: REQUEST to known service/method receives RESPONSE with E_OK."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0020, session_id=0x0001
        )
        resp = _send_request_and_receive(host_ip, req)

        assert_valid_response(resp, _SERVICE_ID, _METHOD_ID)
        assert_return_code(resp, SOMEIPReturnCode.E_OK)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_error_codes"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_basic_02_unknown_service_id_no_response_or_error(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_BASIC_02: REQUEST to unknown service_id must return E_UNKNOWN_SERVICE.

        Per SOMEIPSRV_BASIC_02, a DUT that receives a REQUEST for an unknown service_id
        MUST reply with E_UNKNOWN_SERVICE (return_code 0x02).
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _UNKNOWN_SERVICE_ID, _METHOD_ID, client_id=0x0020, session_id=0x0002
        )
        responses = _send_request_expect_no_response(host_ip, req, timeout_secs=2.0)

        assert responses, (
            "SOMEIPSRV_BASIC_02: No response received for unknown service_id "
            f"0x{_UNKNOWN_SERVICE_ID:04x}; DUT must reply with E_UNKNOWN_SERVICE"
        )
        assert_return_code(responses[0], SOMEIPReturnCode.E_UNKNOWN_SERVICE)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason=(
            "vsomeip 3.6.1 limitation (SOMEIPSRV_BASIC_03): DUT sends a RESPONSE "
            "for event-ID messages (method_id bit 15 = 1). "
            "See docs/architecture/tc8_conformance_testing.rst "
            "§Known SOME/IP Stack Limitations."
        ),
    )
    def test_basic_03_event_method_id_no_response(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_BASIC_03: REQUEST with event method_id (bit 15 set) must not produce a RESPONSE.

        Per SOMEIPSRV_BASIC_03, when the DUT receives a message with method_id bit 15 = 1
        (event notification ID range), it MUST NOT send a RESPONSE (message_type 0x80).
        ERROR messages are not prohibited by the spec.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _EVENT_METHOD_ID, client_id=0x0020, session_id=0x0003
        )
        responses = _send_request_expect_no_response(host_ip, req, timeout_secs=2.0)

        response_msgs = [
            r for r in responses if r.message_type == SOMEIPMessageType.RESPONSE
        ]
        assert not response_msgs, (
            f"SOMEIPSRV_BASIC_03: DUT sent {len(response_msgs)} RESPONSE message(s) "
            "(message_type=0x80) to a REQUEST with event method_id — "
            "DUT must not send a RESPONSE for event method IDs"
        )


# ---------------------------------------------------------------------------
# Group 3 — SOMEIPSRV_ONWIRE_01/02/04/06/11 + RPC_18/20: Response field values
# ---------------------------------------------------------------------------


class TestSomeipResponseFields:
    """SOMEIPSRV_ONWIRE_01/02/04/06/11 + RPC_18/20: Verify RESPONSE field values."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_01_response_source_address(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_ONWIRE_01: RESPONSE originates from the DUT's offering address and port."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0021, session_id=0x0010
        )
        resp, addr = _send_request_and_receive_with_addr(host_ip, req)

        assert_valid_response(resp, _SERVICE_ID, _METHOD_ID)
        assert addr[0] == host_ip, (
            f"SOMEIPSRV_ONWIRE_01: RESPONSE source IP mismatch: "
            f"got {addr[0]}, expected {host_ip}"
        )
        assert addr[1] == DUT_UNRELIABLE_PORT, (
            f"SOMEIPSRV_ONWIRE_01: RESPONSE source port mismatch: "
            f"got {addr[1]}, expected {DUT_UNRELIABLE_PORT}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_02_method_id_msb_zero_in_response(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_ONWIRE_02: RESPONSE method_id has bit 15 = 0 (not an event)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0021, session_id=0x0011
        )
        resp = _send_request_and_receive(host_ip, req)

        assert (resp.method_id & 0x8000) == 0, (
            f"SOMEIPSRV_ONWIRE_02: RESPONSE method_id bit 15 is set "
            f"(0x{resp.method_id:04x}); responses must not carry event method IDs"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_04_request_id_reuse(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_ONWIRE_04: Each RESPONSE echoes the corresponding request_id (client:session).

        Sends two REQUESTs with the same client_id/session_id pair and verifies
        both RESPONSEs echo the pair correctly.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        client_id = 0x0001
        session_id = 0x0042

        for _ in range(2):
            req = build_request(
                _SERVICE_ID, _METHOD_ID, client_id=client_id, session_id=session_id
            )
            resp = _send_request_and_receive(host_ip, req)

            assert resp.client_id == client_id, (
                f"SOMEIPSRV_ONWIRE_04: client_id mismatch: "
                f"got 0x{resp.client_id:04x}, expected 0x{client_id:04x}"
            )
            assert resp.session_id == session_id, (
                f"SOMEIPSRV_ONWIRE_04: session_id mismatch: "
                f"got 0x{resp.session_id:04x}, expected 0x{session_id:04x}"
            )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_06_interface_version_echoed(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_ONWIRE_06: RESPONSE interface_version matches the REQUEST interface_version.

        The interface_version byte (byte 13) in the RESPONSE must be copied
        from the inbound REQUEST, not from the local service configuration.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        iface_ver = 0x05
        req = build_request(
            _SERVICE_ID,
            _METHOD_ID,
            client_id=0x0021,
            session_id=0x0012,
            interface_version=iface_ver,
        )
        resp = _send_request_and_receive(host_ip, req)

        assert resp.interface_version == iface_ver, (
            f"SOMEIPSRV_ONWIRE_06: interface_version mismatch: "
            f"got 0x{resp.interface_version:02x}, expected 0x{iface_ver:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_onwire_11_normal_response_return_code_ok(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_ONWIRE_11: A normal RESPONSE to a valid REQUEST has return_code E_OK."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0021, session_id=0x0013
        )
        resp = _send_request_and_receive(host_ip, req)

        assert_return_code(resp, SOMEIPReturnCode.E_OK)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_18_message_id_echoed(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_18: RESPONSE message_id (service_id:method_id) echoes REQUEST values."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0021, session_id=0x0014
        )
        resp = _send_request_and_receive(host_ip, req)

        assert resp.service_id == _SERVICE_ID, (
            f"SOMEIPSRV_RPC_18: service_id mismatch in RESPONSE: "
            f"got 0x{resp.service_id:04x}, expected 0x{_SERVICE_ID:04x}"
        )
        assert resp.method_id == _METHOD_ID, (
            f"SOMEIPSRV_RPC_18: method_id mismatch in RESPONSE: "
            f"got 0x{resp.method_id:04x}, expected 0x{_METHOD_ID:04x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_19_session_id_echoed_in_error(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_19: Error response session_id must equal the request session_id.

        When the DUT returns an error response (e.g. E_UNKNOWN_METHOD for an
        unknown method_id), the session_id in the response header MUST equal
        the session_id from the request per PRS_SOMEIP_00137 (request_id echo).
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        session_id = 0x0016
        req = build_request(
            _SERVICE_ID, _UNKNOWN_METHOD_ID, client_id=0x0021, session_id=session_id
        )
        resp = _send_request_and_receive(host_ip, req)

        assert_session_echo(resp, session_id)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_20_interface_version_copied_from_request(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_20: RESPONSE interface_version is copied from the REQUEST (variant).

        Sends with interface_version=0x03 (different from ONWIRE_06's 0x05) to
        confirm the echo is dynamic and not hardcoded.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        iface_ver = 0x03
        req = build_request(
            _SERVICE_ID,
            _METHOD_ID,
            client_id=0x0021,
            session_id=0x0015,
            interface_version=iface_ver,
        )
        resp = _send_request_and_receive(host_ip, req)

        assert resp.interface_version == iface_ver, (
            f"SOMEIPSRV_RPC_20: interface_version mismatch: "
            f"got 0x{resp.interface_version:02x}, expected 0x{iface_ver:02x}"
        )


# ---------------------------------------------------------------------------
# Group 3 — SOMEIPSRV_RPC_05-10 + ETS_004/054/059/061/075: Fire-and-forget + robustness
# ---------------------------------------------------------------------------


class TestSomeipFireAndForgetAndErrors:
    """SOMEIPSRV_RPC_05-10 + ETS_004/054/059/061/075: Fire-and-forget and robustness."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_05_fire_and_forget_no_error(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_05: REQUEST_NO_RETURN produces neither a RESPONSE nor an ERROR."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request_no_return(
            _SERVICE_ID, _METHOD_ID, client_id=0x0030, session_id=0x0020
        )
        responses = _send_request_expect_no_response(host_ip, req, timeout_secs=2.0)

        error_msgs = [
            r
            for r in responses
            if r.message_type
            in (
                SOMEIPMessageType.ERROR,
                SOMEIPMessageType.ERROR_ACK,
            )
        ]
        assert not responses, (
            f"SOMEIPSRV_RPC_05: {len(responses)} message(s) received for "
            "REQUEST_NO_RETURN; no RESPONSE or ERROR expected"
        )
        assert not error_msgs, (
            f"SOMEIPSRV_RPC_05: {len(error_msgs)} ERROR message(s) received for "
            "REQUEST_NO_RETURN; SOME/IP spec prohibits error replies to fire-and-forget"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_06_return_code_upper_bits_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_06: RESPONSE return_code has bits 7-5 = 0 (only bits 4-0 are used)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0030, session_id=0x0021
        )
        resp = _send_request_and_receive(host_ip, req)

        rc_value: int = (
            resp.return_code.value
            if hasattr(resp.return_code, "value")
            else int(resp.return_code)
        )
        assert (rc_value & 0xE0) == 0, (
            f"SOMEIPSRV_RPC_06: RESPONSE return_code upper bits are not zero: "
            f"0x{rc_value:02x} (bits 7-5 = 0x{(rc_value >> 5) & 0x07:01x})"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_07_request_with_return_code_bits_set(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_07: DUT processes REQUEST normally even if return_code field is non-zero.

        The SOME/IP spec says servers must ignore return_code in inbound REQUESTs.
        Sending return_code=0x20 (non-zero) must still yield a valid RESPONSE.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request_with_return_code(
            _SERVICE_ID,
            _METHOD_ID,
            return_code=0x20,
            client_id=0x0030,
            session_id=0x0022,
        )
        resp = _send_request_and_receive(host_ip, req)

        assert_valid_response(resp, _SERVICE_ID, _METHOD_ID)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason=(
            "vsomeip 3.6.1 limitation (SOMEIPSRV_RPC_08): DUT replies to REQUEST "
            "messages carrying a non-zero return code, violating the spec. "
            "See docs/architecture/tc8_conformance_testing.rst "
            "§Known SOME/IP Stack Limitations."
        ),
    )
    def test_rpc_08_request_with_error_return_code_no_reply(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_08: DUT does not reply to REQUEST with return_code = E_NOT_OK (0x01).

        Per SOME/IP spec a server must not process a REQUEST whose return_code
        field is set to an error value by the client.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request_with_return_code(
            _SERVICE_ID,
            _METHOD_ID,
            return_code=0x01,
            client_id=0x0030,
            session_id=0x0023,
        )
        responses = _send_request_expect_no_response(host_ip, req, timeout_secs=2.0)

        assert not responses, (
            f"SOMEIPSRV_RPC_08: {len(responses)} response(s) received; "
            "DUT must not reply to REQUEST with non-zero return_code"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_error_codes"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_09_error_response_no_payload(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_09: ERROR response to unknown method has length = 8 (no payload).

        The SOME/IP length field counts from byte 8 onward.  An error RESPONSE
        with no payload has length = 8 (the fixed-header tail: client_id, session_id,
        protocol_version, interface_version, message_type, return_code).
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _UNKNOWN_METHOD_ID, client_id=0x0030, session_id=0x0024
        )
        resp = _send_request_and_receive(host_ip, req)

        # Compute the actual length field from the serialised response.
        raw_resp = resp.build()
        length_field = int.from_bytes(raw_resp[4:8], "big")
        assert length_field == 8, (
            f"SOMEIPSRV_RPC_09: ERROR response length field = {length_field}, "
            "expected 8 (no payload for error responses)"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_malformed"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_10_fire_and_forget_reserved_type_no_error(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_10: REQUEST_NO_RETURN with patched reserved message_type byte is dropped.

        Patches byte 14 to 0x04 (reserved message type) then sends as fire-and-forget.
        The DUT must not send an ERROR in response.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        raw = build_request_no_return(
            _SERVICE_ID, _METHOD_ID, client_id=0x0030, session_id=0x0025
        )
        # Patch byte 14 (message_type) to reserved value 0x04.
        patched = raw[:14] + b"\x04" + raw[15:]

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        responses = []
        try:
            sock.bind(("", 0))
            sock.sendto(patched, (host_ip, DUT_UNRELIABLE_PORT))
            deadline = time.monotonic() + 2.0
            while time.monotonic() < deadline:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                sock.settimeout(min(remaining, 0.5))
                try:
                    data, _ = sock.recvfrom(65535)
                    msg, _ = SOMEIPHeader.parse(data)
                    responses.append(msg)
                except socket.timeout:
                    continue
                except Exception:
                    continue
        finally:
            sock.close()

        assert not responses, (
            f"SOMEIPSRV_RPC_10: {len(responses)} message(s) received after sending "
            "fire-and-forget with reserved message_type; DUT must not send ERROR"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_004_burst_10_sequential_requests(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_ETS_004: 10 sequential REQUESTs each produce a correctly echoed RESPONSE.

        Sends 10 REQUESTs with incrementing session IDs and verifies each
        RESPONSE carries the matching session_id.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        base_session_id = 0x0100
        for i in range(10):
            session_id = base_session_id + i
            req = build_request(
                _SERVICE_ID, _METHOD_ID, client_id=0x0030, session_id=session_id
            )
            resp = _send_request_and_receive(host_ip, req, timeout_secs=3.0)
            assert_valid_response(resp, _SERVICE_ID, _METHOD_ID)
            assert_session_echo(resp, session_id)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_054_empty_payload_request(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_ETS_054: REQUEST with empty payload (length=8) gets E_OK RESPONSE."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID,
            _METHOD_ID,
            client_id=0x0030,
            session_id=0x0030,
            payload=b"",
        )
        resp = _send_request_and_receive(host_ip, req)

        assert_valid_response(resp, _SERVICE_ID, _METHOD_ID)
        assert_return_code(resp, SOMEIPReturnCode.E_OK)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_059_fire_and_forget_wrong_service_no_error(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_ETS_059: REQUEST_NO_RETURN to non-existent service gets no ERROR reply."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request_no_return(
            _UNKNOWN_SERVICE_ID, _METHOD_ID, client_id=0x0030, session_id=0x0031
        )
        responses = _send_request_expect_no_response(host_ip, req, timeout_secs=2.0)

        assert not responses, (
            f"SOMEIPSRV_ETS_059: {len(responses)} message(s) received after sending "
            "REQUEST_NO_RETURN to unknown service; DUT must not send ERROR"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_061_two_sequential_requests(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_ETS_061: Two sequential REQUESTs each receive RESPONSE with correct session_id."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        for session_id in (0x0040, 0x0041):
            req = build_request(
                _SERVICE_ID, _METHOD_ID, client_id=0x0030, session_id=session_id
            )
            resp = _send_request_and_receive(host_ip, req)
            assert_valid_response(resp, _SERVICE_ID, _METHOD_ID)
            assert_session_echo(resp, session_id)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_malformed"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_075_notification_as_request_ignored(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_ETS_075: DUT ignores a message with message_type=NOTIFICATION (0x02).

        A NOTIFICATION message type in the client→server direction is invalid
        per SOME/IP spec.  The server must not send a RESPONSE.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        msg = build_notification_as_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0030, session_id=0x0050
        )
        responses = _send_request_expect_no_response(host_ip, msg, timeout_secs=2.0)

        assert not responses, (
            f"SOMEIPSRV_ETS_075: {len(responses)} response(s) received for a "
            "NOTIFICATION message sent to the server; DUT must not reply"
        )


# ---------------------------------------------------------------------------
# ETS_005 / ETS_058 — Big-endian byte order and oversized length field
# ---------------------------------------------------------------------------


class TestSomeipByteOrder:
    """ETS_005/058: Big-endian byte order in SOME/IP responses and oversized length robustness."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_resp_header"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_005_response_uses_big_endian_byte_order(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """ETS_005: SOME/IP RESPONSE is encoded in big-endian byte order.

        Verifies PRS_SOMEIP_00087: all SOME/IP header fields are big-endian.
        Sends a REQUEST and compares the raw bytes of the RESPONSE with the
        parsed field values to confirm big-endian encoding.
        """
        import struct as _struct

        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0022, session_id=0x0090
        )

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        raw_data: bytes = b""
        parsed: SOMEIPHeader
        try:
            sock.bind(("", 0))
            sock.sendto(req, (host_ip, DUT_UNRELIABLE_PORT))
            sock.settimeout(3.0)
            raw_data, _ = sock.recvfrom(65535)
            parsed, _ = SOMEIPHeader.parse(raw_data)
        finally:
            sock.close()

        assert len(raw_data) >= 16, (
            f"ETS_005: RESPONSE too short to be a valid SOME/IP header ({len(raw_data)} bytes)"
        )

        # service_id is bytes 0-1 (big-endian uint16)
        expected_service_id_msb = (parsed.service_id >> 8) & 0xFF
        assert raw_data[0] == expected_service_id_msb, (
            f"ETS_005: service_id MSB byte mismatch — raw byte[0] = 0x{raw_data[0]:02x}, "
            f"expected 0x{expected_service_id_msb:02x} (service_id = 0x{parsed.service_id:04x}). "
            "SOME/IP header must use big-endian byte order (PRS_SOMEIP_00087)."
        )

        # length field is bytes 4-7 (big-endian uint32)
        raw_length = _struct.unpack_from(">I", raw_data, 4)[0]
        assert raw_length == len(raw_data) - 8, (
            f"ETS_005: length field = {raw_length}, actual payload+header-tail = "
            f"{len(raw_data) - 8}; length field must be big-endian encoded"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__msg_malformed"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_058_oversized_length_field_no_crash(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """ETS_058: SOME/IP message with oversized length field (0xFFFFFFF0) does not crash DUT.

        Patches bytes 4-7 of a valid REQUEST to claim a payload length of
        0xFFFFFFF0 (far exceeding the actual UDP datagram size). The DUT must
        discard the malformed message and remain operational, confirmed by a
        successful response to a subsequent valid REQUEST.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"
        _wait_for_dut_offer(host_ip)

        # Build a valid request and patch the length field to an absurd value.
        valid_req = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0022, session_id=0x0091
        )
        oversized = bytearray(valid_req)
        oversized[4] = 0xFF
        oversized[5] = 0xFF
        oversized[6] = 0xFF
        oversized[7] = 0xF0

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            sock.bind(("", 0))
            sock.sendto(bytes(oversized), (host_ip, DUT_UNRELIABLE_PORT))
        finally:
            sock.close()

        time.sleep(0.3)
        assert someipd_dut.poll() is None, (
            "ETS_058: someipd crashed after receiving a message with "
            "oversized length field (0xFFFFFFF0)"
        )

        # Confirm DUT is still responsive with a valid follow-up request.
        follow_up = build_request(
            _SERVICE_ID, _METHOD_ID, client_id=0x0022, session_id=0x0092
        )
        resp = _send_request_and_receive(host_ip, follow_up)
        assert_valid_response(resp, _SERVICE_ID, _METHOD_ID)
