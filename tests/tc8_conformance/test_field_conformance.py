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
"""TC8 Field Conformance tests — TC8-FLD-001 through TC8-FLD-004.

Fields extend events with three properties:
  1. Initial value: the DUT sends the last known value to a new subscriber immediately.
  2. Getter: a REQUEST to method 0x0001 returns the current field value.
  3. Setter: a REQUEST to method 0x0002 updates the field, notifies subscribers, and
     returns a RESPONSE.

The DUT (``someipd --tc8-standalone``) is configured via ``tc8_someipd_service.json``
which declares event 0x0777 with ``is_field: true`` and ``update-cycle: 500`` ms.
The standalone loop sends periodic ``notify()`` calls so the DUT caches the value.

See ``docs/tc8_conformance/requirements.rst`` for requirement traceability.
"""

import socket
import subprocess
import time
import pytest

from attribute_plugin import add_test_properties

from helpers.event_helpers import (
    capture_notifications,
    subscribe_and_wait_ack,
)
from helpers.field_helpers import (
    send_get_field,
    send_get_field_tcp,
    send_set_field,
    send_set_field_tcp,
)
from helpers.constants import DUT_RELIABLE_PORT, DUT_UNRELIABLE_PORT, SD_PORT
from helpers.sd_helpers import capture_sd_offers
from someip.header import SOMEIPReturnCode

# ---------------------------------------------------------------------------
# Module-level configuration
# ---------------------------------------------------------------------------

#: SOME/IP stack config template — fields config with is_field=true, update-cycle=500ms.
SOMEIP_CONFIG: str = "tc8_someipd_service.json"

_SERVICE_ID: int = 0x1234
_INSTANCE_ID: int = 0x5678
_EVENT_ID: int = 0x0777
_EVENTGROUP_ID: int = 0x4455
_MAJOR_VERSION: int = 0x00

#: GET field method — returns current field value (TC8-FLD-003).
_GET_METHOD_ID: int = 0x0001

#: SET field method — updates field value and notifies (TC8-FLD-004).
_SET_METHOD_ID: int = 0x0002

#: All tests in this module require multicast — checked once per module.
pytestmark = pytest.mark.usefixtures("require_multicast")


# ---------------------------------------------------------------------------
# TC8-FLD-001 / TC8-FLD-002 — Field initial value on subscribe
# ---------------------------------------------------------------------------


class TestFieldInitialValue:
    """TC8-FLD-001/002: DUT sends the cached field value to a new subscriber."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__fld_initial_value"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_fld_001_initial_notification_on_subscribe(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-FLD-001: Subscribing to a field eventgroup triggers an immediate NOTIFICATION.

        The DUT caches the last notified value for ``is_field: true`` events and
        re-sends it to each new subscriber without waiting for the next notify cycle.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # Wait until the DUT has sent at least one SD offer.
        try:
            capture_sd_offers(host_ip, min_count=1, timeout_secs=5.0)
        except (TimeoutError, OSError):
            pytest.skip("DUT did not offer service within timeout")

        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port = notif_sock.getsockname()[1]

        sd_sock = None
        try:
            sd_sock = subscribe_and_wait_ack(
                tester_ip,
                host_ip,
                SD_PORT,
                _SERVICE_ID,
                _INSTANCE_ID,
                _EVENTGROUP_ID,
                _MAJOR_VERSION,
                notif_port=notif_port,
            )
            # Wait actively for the DUT to send the first notify() (update-cycle=500 ms).
            # Proceed as soon as the first notification arrives; no fixed sleep needed.
            notifs = capture_notifications(
                notif_sock,
                _EVENT_ID,
                _SERVICE_ID,
                count=1,
                timeout_secs=1.5,
            )
            assert notifs, (
                "TC8-FLD-001: No initial NOTIFICATION received after subscribing to "
                "a field eventgroup. Verify someipd is running with update-cycle=500 ms "
                "and is_field=true."
            )
        finally:
            if sd_sock:
                sd_sock.close()
            notif_sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__fld_initial_value"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_fld_002_is_field_sends_initial_value_within_one_second(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-FLD-002: Initial notification for a field arrives within 1 s of subscribe ACK.

        Contrast with ``is_field: false`` events which only deliver notifications
        on the next regular notify cycle.  A field delivers the cached value
        immediately, so the first notification should arrive well within 1 second.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port = notif_sock.getsockname()[1]

        sd_sock = None
        try:
            sd_sock = subscribe_and_wait_ack(
                tester_ip,
                host_ip,
                SD_PORT,
                _SERVICE_ID,
                _INSTANCE_ID,
                _EVENTGROUP_ID,
                _MAJOR_VERSION,
                notif_port=notif_port,
            )
            subscribe_time = time.monotonic()
            # Active wait: proceeds as soon as the first notification arrives.
            # A field (is_field=true) delivers the cached value immediately after ACK.
            notifs = capture_notifications(
                notif_sock,
                _EVENT_ID,
                _SERVICE_ID,
                count=1,
                timeout_secs=1.5,
            )
            elapsed_ms = (time.monotonic() - subscribe_time) * 1000.0

            assert notifs, (
                "TC8-FLD-002: No initial NOTIFICATION received within 1.5 s of subscribe. "
                f"Elapsed: {elapsed_ms:.0f} ms. "
                "A field (is_field=true) must deliver the cached value immediately."
            )
        finally:
            if sd_sock:
                sd_sock.close()
            notif_sock.close()


# ---------------------------------------------------------------------------
# TC8-FLD-003 / TC8-FLD-004 — Field getter and setter
# ---------------------------------------------------------------------------


class TestFieldGetSet:
    """TC8-FLD-003/004: Field getter returns current value; setter updates and notifies."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__fld_get_set"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_fld_003_getter_returns_current_value(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-FLD-003: GET request (method 0x0001) returns a RESPONSE with the current value."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        resp = send_get_field(
            host_ip,
            _SERVICE_ID,
            _GET_METHOD_ID,
            DUT_UNRELIABLE_PORT,
            client_id=0x0030,
            session_id=0x0001,
        )

        assert resp.return_code == SOMEIPReturnCode.E_OK, (
            f"TC8-FLD-003: GET returned code 0x{resp.return_code.value:02x} "
            f"({resp.return_code.name}), expected E_OK"
        )
        assert resp.payload, (
            "TC8-FLD-003: GET response has empty payload — expected current field value"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__fld_get_set"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_fld_004_setter_updates_value_and_notifies(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-FLD-004: SET request (method 0x0002) updates the field and notifies subscribers."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        new_value = b"\xca\xfe"

        # Subscribe first so we receive the notification triggered by SET.
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port = notif_sock.getsockname()[1]

        sd_sock = None
        try:
            sd_sock = subscribe_and_wait_ack(
                tester_ip,
                host_ip,
                SD_PORT,
                _SERVICE_ID,
                _INSTANCE_ID,
                _EVENTGROUP_ID,
                _MAJOR_VERSION,
                notif_port=notif_port,
            )

            # Drain any initial value notification that arrived due to is_field=true.
            capture_notifications(
                notif_sock, _EVENT_ID, _SERVICE_ID, count=1, timeout_secs=1.5
            )

            # Send the SET request.
            set_resp = send_set_field(
                host_ip,
                _SERVICE_ID,
                _SET_METHOD_ID,
                new_value,
                DUT_UNRELIABLE_PORT,
                client_id=0x0030,
                session_id=0x0002,
            )
            assert set_resp.return_code == SOMEIPReturnCode.E_OK, (
                f"TC8-FLD-004: SET returned code 0x{set_resp.return_code.value:02x} "
                f"({set_resp.return_code.name}), expected E_OK"
            )

            # Verify the DUT notified us with the new value within 3 s.
            notifs = capture_notifications(
                notif_sock,
                _EVENT_ID,
                _SERVICE_ID,
                count=1,
                timeout_secs=3.0,
            )
            assert notifs, (
                "TC8-FLD-004: No NOTIFICATION received after SET — "
                "DUT must notify all subscribers when a field value changes"
            )
            received_payload = bytes(notifs[0].payload) if notifs[0].payload else b""
            assert received_payload == new_value, (
                f"TC8-FLD-004: Notification payload mismatch: "
                f"got {received_payload!r}, expected {new_value!r}"
            )
        finally:
            if sd_sock:
                sd_sock.close()
            notif_sock.close()


# ---------------------------------------------------------------------------
# SOMEIPSRV_RPC_17 — Field GET/SET over TCP (reliable transport)
# ---------------------------------------------------------------------------


class TestFieldTcpTransport:
    """Field GET/SET over TCP — SOMEIPSRV_RPC_17.

    These tests verify that someipd correctly handles field GET and SET
    requests over TCP (reliable transport binding). The DUT is configured
    with both unreliable (UDP ``DUT_UNRELIABLE_PORT``) and reliable (TCP ``DUT_RELIABLE_PORT``) ports.
    """

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__tcp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_rpc_17_tcp_field_getter(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_17: GET field request over TCP returns the current value."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        resp = send_get_field_tcp(
            host_ip,
            _SERVICE_ID,
            _GET_METHOD_ID,
            DUT_RELIABLE_PORT,
            client_id=0x0040,
            session_id=0x0010,
        )

        assert resp.return_code == SOMEIPReturnCode.E_OK, (
            f"SOMEIPSRV_RPC_17: TCP GET returned code 0x{resp.return_code.value:02x} "
            f"({resp.return_code.name}), expected E_OK"
        )
        assert resp.payload, (
            "SOMEIPSRV_RPC_17: TCP GET response has empty payload — expected current field value"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__tcp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_rpc_17_tcp_field_setter(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_17: SET field request over TCP updates the value."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        new_value = b"\xbe\xef"
        set_resp = send_set_field_tcp(
            host_ip,
            _SERVICE_ID,
            _SET_METHOD_ID,
            new_value,
            DUT_RELIABLE_PORT,
            client_id=0x0040,
            session_id=0x0011,
        )

        assert set_resp.return_code == SOMEIPReturnCode.E_OK, (
            f"SOMEIPSRV_RPC_17: TCP SET returned code 0x{set_resp.return_code.value:02x} "
            f"({set_resp.return_code.name}), expected E_OK"
        )

        # Verify the value was updated by reading it back over TCP.
        get_resp = send_get_field_tcp(
            host_ip,
            _SERVICE_ID,
            _GET_METHOD_ID,
            DUT_RELIABLE_PORT,
            client_id=0x0040,
            session_id=0x0012,
        )
        received_payload = bytes(get_resp.payload) if get_resp.payload else b""
        assert received_payload == new_value, (
            f"SOMEIPSRV_RPC_17: TCP GET after SET payload mismatch: "
            f"got {received_payload!r}, expected {new_value!r}"
        )
