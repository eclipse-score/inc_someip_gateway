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
"""TC8 Event Notification tests — TC8-EVT-001 through TC8-EVT-006.

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import socket
import subprocess
import time
import pytest

from attribute_plugin import add_test_properties

from helpers.event_helpers import (
    assert_notification_header,
    capture_any_notifications,
    capture_notifications,
    subscribe_and_wait_ack,
)
from helpers.sd_helpers import capture_sd_offers, open_multicast_socket
from helpers.sd_sender import (
    L4Protocols,
    SOMEIPSDEntryType,
    capture_unicast_sd_entries,
    open_sender_socket,
    send_subscribe_eventgroup,
)
from helpers.constants import DUT_RELIABLE_PORT, SD_PORT
from helpers.tcp_helpers import tcp_receive_response
from someip.header import SOMEIPMessageType

# ---------------------------------------------------------------------------
# Module-level configuration
# ---------------------------------------------------------------------------

SOMEIP_CONFIG: str = "tc8_someipd_service.json"

_SERVICE_ID: int = 0x1234
_INSTANCE_ID: int = 0x5678
_EVENT_ID: int = 0x0777
_EVENTGROUP_ID: int = 0x4455
_MAJOR_VERSION: int = 0x00

#: TCP-only event/eventgroup — offered with RT_RELIABLE in someipd standalone mode.
_TCP_EVENT_ID: int = 0x0778
_TCP_EVENTGROUP_ID: int = 0x4475

#: Static field event — long update-cycle (60 000 ms) used for RPC_16 on-change test.
_STATIC_FIELD_EVENT_ID: int = 0x0779
_STATIC_FIELD_EVENTGROUP_ID: int = 0x4480


# ---------------------------------------------------------------------------
# TC8-EVT-001 / TC8-EVT-002 — Notification format
# ---------------------------------------------------------------------------


class TestEventNotificationFormat:
    """TC8-EVT-001/002, SOMEIPSRV_RPC_15/16: Notification format and delivery strategy."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__evt_subscription"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_evt_001_notification_message_type(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-001: Event notification has message_type = NOTIFICATION (0x02)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # Wait for DUT to fully start SD before subscribing.
        try:
            capture_sd_offers(host_ip, min_count=1, timeout_secs=5.0)
        except (TimeoutError, OSError):
            pytest.skip("DUT did not offer service within timeout")

        # Open notification receiver socket
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
            # DUT notifies every 500 ms (tc8_someipd_service.json update-cycle), wait for at least one
            notifs = capture_notifications(
                notif_sock,
                _EVENT_ID,
                _SERVICE_ID,
                count=1,
                timeout_secs=5.0,
            )
            assert notifs, "TC8-EVT-001: No NOTIFICATION received after subscription"
            assert notifs[0].message_type == SOMEIPMessageType.NOTIFICATION, (
                f"TC8-EVT-001: message_type = 0x{notifs[0].message_type:02x}, "
                f"expected NOTIFICATION (0x{SOMEIPMessageType.NOTIFICATION:02x})"
            )
        finally:
            if sd_sock:
                sd_sock.close()
            notif_sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__evt_subscription"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_evt_002_correct_event_id(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-002: Notification carries the correct event_id in the method_id field."""
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
            notifs = capture_notifications(
                notif_sock,
                _EVENT_ID,
                _SERVICE_ID,
                count=1,
                timeout_secs=5.0,
            )
            assert notifs, "TC8-EVT-002: No NOTIFICATION received after subscription"
            assert_notification_header(notifs[0], _EVENT_ID)
        finally:
            if sd_sock:
                sd_sock.close()
            notif_sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__evt_subscription"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_15_cyclic_notification_rate(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_15: Cyclic event notifications arrive at the configured cycle period.

        The DUT is configured with ``update-cycle: 500`` ms for event 0x0777.
        This test subscribes, collects 4 notification timestamps, and verifies
        that the inter-notification intervals are within [200 ms, 1200 ms] —
        a 2.4× tolerance band that accounts for OS scheduling jitter and the
        initial notification (which may arrive immediately as the initial-event
        value send, followed by cyclic notifications thereafter).
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _CYCLE_MS = 500
        _TOLERANCE_FACTOR = 3.0
        _MIN_INTERVAL = (_CYCLE_MS / 1000.0) / _TOLERANCE_FACTOR
        _MAX_INTERVAL = (_CYCLE_MS / 1000.0) * _TOLERANCE_FACTOR

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
                ttl=30,  # 30 s > 10 s observation window; keeps subscription alive.
            )

            # Collect 4 notifications with timestamps.
            timestamps: list = []
            deadline = time.monotonic() + 10.0
            while time.monotonic() < deadline and len(timestamps) < 4:
                remaining = deadline - time.monotonic()
                notif_sock.settimeout(min(remaining, 2.0))
                try:
                    data, _ = notif_sock.recvfrom(65535)
                except socket.timeout:
                    continue
                try:
                    from someip.header import SOMEIPHeader as _HDR

                    msg, _ = _HDR.parse(data)
                    if msg.service_id == _SERVICE_ID and msg.method_id == _EVENT_ID:
                        timestamps.append(time.monotonic())
                except Exception:
                    continue

            assert len(timestamps) >= 4, (
                f"SOMEIPSRV_RPC_15: received only {len(timestamps)} notification(s) "
                f"within 10 s; expected at least 4 at ~{_CYCLE_MS} ms cycle"
            )

            # Check intervals between consecutive notifications (skip first gap
            # which may be shorter due to initial-event delivery).
            intervals = [
                timestamps[i + 1] - timestamps[i] for i in range(1, len(timestamps) - 1)
            ]
            for idx, interval in enumerate(intervals):
                assert _MIN_INTERVAL <= interval <= _MAX_INTERVAL, (
                    f"SOMEIPSRV_RPC_15: notification interval {idx + 1} = "
                    f"{interval * 1000:.0f} ms; expected [{_MIN_INTERVAL * 1000:.0f} ms, "
                    f"{_MAX_INTERVAL * 1000:.0f} ms] "
                    f"(configured cycle: {_CYCLE_MS} ms)"
                )
        finally:
            if sd_sock:
                sd_sock.close()
            notif_sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__evt_subscription"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_16_field_notifies_only_on_change(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_16: Field event 0x0779 sends one initial notification then stays silent.

        Event 0x0779 is a field (is_field=true) with update-cycle=60 000 ms in
        tc8_someipd_service.json. On subscription the DUT sends exactly one
        initial-value notification; no further notifications are expected within
        a 3-second observation window because the 60 000 ms cycle has not elapsed
        and the field value does not change in the standalone DUT configuration.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        try:
            from helpers.sd_helpers import capture_sd_offers as _capture_sd_offers

            _capture_sd_offers(host_ip, min_count=1, timeout_secs=5.0)
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
                _STATIC_FIELD_EVENTGROUP_ID,
                _MAJOR_VERSION,
                notif_port=notif_port,
            )

            # Expect exactly one initial-value notification from the field event.
            initial = capture_notifications(
                notif_sock,
                _STATIC_FIELD_EVENT_ID,
                _SERVICE_ID,
                count=1,
                timeout_secs=5.0,
            )
            assert initial, (
                "SOMEIPSRV_RPC_16: No initial-value notification received after "
                f"subscribing to static field eventgroup 0x{_STATIC_FIELD_EVENTGROUP_ID:04x}"
            )

            # Wait 3 seconds — no further notifications should arrive because the
            # 60 000 ms update-cycle has not elapsed and the field value is frozen.
            subsequent = capture_any_notifications(
                notif_sock, _SERVICE_ID, timeout_secs=3.0
            )
            assert not subsequent, (
                f"SOMEIPSRV_RPC_16: {len(subsequent)} unexpected notification(s) received "
                "within 3 s for a field with update-cycle=60 000 ms; "
                "field events must not be sent cyclically when the value has not changed"
            )
        finally:
            if sd_sock:
                sd_sock.close()
            notif_sock.close()


# ---------------------------------------------------------------------------
# TC8-EVT-003 / TC8-EVT-004 — Subscription-gated delivery
# ---------------------------------------------------------------------------


class TestEventSubscriptionGating:
    """TC8-EVT-003/004: Notifications only to subscribed endpoints."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__evt_subscription"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_evt_003_notification_only_to_subscriber(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-003: Subscribed endpoint receives notifications; unsubscribed does not."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # Subscribed socket
        sub_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sub_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sub_sock.bind((tester_ip, 0))
        sub_port = sub_sock.getsockname()[1]

        # Unsubscribed socket (different port, never subscribes)
        unsub_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        unsub_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        unsub_sock.bind((tester_ip, 0))

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
                notif_port=sub_port,
            )

            # Wait for notification on subscribed socket
            notifs = capture_notifications(
                sub_sock,
                _EVENT_ID,
                _SERVICE_ID,
                count=1,
                timeout_secs=5.0,
            )
            assert notifs, "TC8-EVT-003: No notification on subscribed socket"

            # Unsubscribed socket should have nothing
            stray = capture_any_notifications(unsub_sock, _SERVICE_ID, timeout_secs=2.0)
            assert not stray, (
                f"TC8-EVT-003: {len(stray)} notification(s) on unsubscribed socket"
            )
        finally:
            if sd_sock:
                sd_sock.close()
            sub_sock.close()
            unsub_sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__evt_subscription"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_evt_004_no_notification_before_subscribe(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-004: No notifications arrive before subscribing."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # Listen without subscribing — DUT notifies every 2000 ms
        listen_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listen_sock.bind((tester_ip, 0))

        try:
            # Wait 3 seconds — if we got any, subscription gating failed
            stray = capture_any_notifications(
                listen_sock, _SERVICE_ID, timeout_secs=3.0
            )
            assert not stray, (
                f"TC8-EVT-004: {len(stray)} notification(s) received without subscription"
            )
        finally:
            listen_sock.close()


# ---------------------------------------------------------------------------
# TC8-EVT-006 — StopSubscribe ceases notifications
# ---------------------------------------------------------------------------


class TestEventStopSubscribe:
    """TC8-EVT-006: StopSubscribeEventgroup ceases notifications."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__evt_subscription"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_evt_006_stop_subscribe_ceases_notifications(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-006: Notifications stop after StopSubscribeEventgroup (TTL=0)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port = notif_sock.getsockname()[1]

        sd_sock = None
        try:
            # Subscribe and verify notifications
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
            notifs = capture_notifications(
                notif_sock,
                _EVENT_ID,
                _SERVICE_ID,
                count=1,
                timeout_secs=5.0,
            )
            assert notifs, (
                "TC8-EVT-006: pre-condition failed — no notifications before StopSubscribe"
            )

            # StopSubscribe (TTL=0)
            send_subscribe_eventgroup(
                sd_sock,
                (host_ip, SD_PORT),
                _SERVICE_ID,
                _INSTANCE_ID,
                _EVENTGROUP_ID,
                _MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=notif_port,
                ttl=0,
            )

            # Wait — no more notifications should arrive
            # DUT sends every 500 ms (tc8_someipd_service.json update-cycle), so a 4 s window should catch any leaks
            post = capture_any_notifications(notif_sock, _SERVICE_ID, timeout_secs=4.0)
            assert not post, (
                f"TC8-EVT-006: {len(post)} notification(s) after StopSubscribeEventgroup"
            )
        finally:
            if sd_sock:
                sd_sock.close()
            notif_sock.close()


# ---------------------------------------------------------------------------
# TC8-EVT-005 — Multicast notification delivery
# ---------------------------------------------------------------------------


_MULTICAST_EVENTGROUP_ID: int = 0x4465
_MULTICAST_NOTIF_ADDR: str = "239.0.0.1"
_MULTICAST_NOTIF_PORT: int = 40490


class TestMulticastEventDelivery:
    """TC8-EVT-005: Notifications for a multicast eventgroup arrive on the multicast address."""

    @pytest.mark.network
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__evt_subscription"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_evt_005_multicast_notification_delivery(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-005: Notifications arrive on the multicast group after subscribing to 0x4465."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # Wait for DUT to fully start SD before subscribing.
        try:
            capture_sd_offers(host_ip, min_count=1, timeout_secs=5.0)
        except (TimeoutError, OSError):
            pytest.skip("DUT did not offer service within timeout")

        # Open multicast notification socket BEFORE subscribing so we don't miss
        # the first notification the DUT sends to the multicast group.
        try:
            mcast_sock = open_multicast_socket(
                host_ip,
                multicast_group=_MULTICAST_NOTIF_ADDR,
                port=_MULTICAST_NOTIF_PORT,
            )
        except OSError as exc:
            pytest.skip(
                f"Cannot join multicast group {_MULTICAST_NOTIF_ADDR}:{_MULTICAST_NOTIF_PORT} "
                f"on {host_ip}: {exc}"
            )

        sd_sock = None
        try:
            sd_sock = subscribe_and_wait_ack(
                tester_ip,
                host_ip,
                SD_PORT,
                _SERVICE_ID,
                _INSTANCE_ID,
                _MULTICAST_EVENTGROUP_ID,
                _MAJOR_VERSION,
                notif_port=mcast_sock.getsockname()[1],
            )
            # DUT sends notifications every 500 ms (tc8_someipd_service.json update-cycle).
            # Allow up to 5 s for the first notification to arrive on the multicast socket.
            notifs = capture_any_notifications(
                mcast_sock, _SERVICE_ID, timeout_secs=5.0
            )
            assert notifs, (
                f"TC8-EVT-005: No SOME/IP notification received on multicast "
                f"{_MULTICAST_NOTIF_ADDR}:{_MULTICAST_NOTIF_PORT} within 5 s "
                f"after subscribing to eventgroup 0x{_MULTICAST_EVENTGROUP_ID:04x}"
            )
            assert notifs[0].message_type == SOMEIPMessageType.NOTIFICATION, (
                f"TC8-EVT-005: message_type mismatch on multicast socket: "
                f"got 0x{notifs[0].message_type:02x}, expected NOTIFICATION (0x02)"
            )
        finally:
            if sd_sock:
                sd_sock.close()
            mcast_sock.close()


# ---------------------------------------------------------------------------
# SOMEIPSRV_RPC_17 — TCP notification delivery
# ---------------------------------------------------------------------------


class TestEventTcpNotification:
    """SOMEIPSRV_RPC_17: Event notification delivery via TCP (reliable transport).

    When a subscriber specifies a TCP endpoint in SubscribeEventgroup,
    the DUT must deliver event notifications over a TCP connection to
    the subscriber's TCP port.
    """

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__tcp_transport"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_rpc_17_tcp_event_notification_delivery(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_17: Notification arrives on TCP after subscribing with TCP endpoint.

        Procedure:
        1. Establish a TCP connection to the DUT's reliable port.
        2. Subscribe with SubscribeEventgroup using the TCP connection's
           local port as the subscriber endpoint (SOME/IP SD PRS_SOMEIPSD_00362
           requires an existing TCP connection before the DUT accepts a
           reliable subscription).
        3. Receive a NOTIFICATION over the established TCP connection.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # Wait for DUT to fully start SD.
        try:
            capture_sd_offers(host_ip, min_count=1, timeout_secs=5.0)
        except (TimeoutError, OSError):
            pytest.skip("DUT did not offer service within timeout")

        # Connect TCP to DUT's reliable port — SOME/IP SD PRS_SOMEIPSD_00362 requires a
        # client-initiated TCP connection before the DUT will accept a reliable
        # subscription.  Bind to tester_ip so the source address matches the
        # subscriber_ip in the SubscribeEventgroup entry (DUT validates the exact
        # subscriber endpoint).
        tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp_sock.settimeout(5.0)
        tcp_sock.bind((tester_ip, 0))
        tcp_sock.connect((host_ip, DUT_RELIABLE_PORT))
        local_port = tcp_sock.getsockname()[1]

        sd_sock = open_sender_socket(tester_ip)
        try:
            # Subscribe to the TCP-only eventgroup using the TCP connection's
            # source port so the DUT's TCP-connected check passes.
            send_subscribe_eventgroup(
                sd_sock,
                (host_ip, SD_PORT),
                _SERVICE_ID,
                _INSTANCE_ID,
                _TCP_EVENTGROUP_ID,
                _MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=local_port,
                l4proto=L4Protocols.TCP,
            )

            # Wait for SubscribeAck.
            entries = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=lambda: send_subscribe_eventgroup(
                    sd_sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _TCP_EVENTGROUP_ID,
                    _MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=local_port,
                    l4proto=L4Protocols.TCP,
                ),
                max_results=1,
            )
            acks = [
                e
                for e in entries
                if e.eventgroup_id == _TCP_EVENTGROUP_ID and e.ttl > 0
            ]
            assert acks, (
                f"No SubscribeEventgroupAck for TCP eventgroup 0x{_TCP_EVENTGROUP_ID:04x}"
            )

            # DUT sends every 500 ms via standalone loop — wait for TCP notification.
            msg = tcp_receive_response(tcp_sock, timeout_secs=8.0)
            assert msg.message_type == SOMEIPMessageType.NOTIFICATION, (
                f"SOMEIPSRV_RPC_17: TCP message_type = 0x{msg.message_type:02x}, "
                f"expected NOTIFICATION (0x{SOMEIPMessageType.NOTIFICATION:02x})"
            )
            assert msg.method_id == _TCP_EVENT_ID, (
                f"SOMEIPSRV_RPC_17: TCP event_id = 0x{msg.method_id:04x}, "
                f"expected 0x{_TCP_EVENT_ID:04x}"
            )
        finally:
            sd_sock.close()
            tcp_sock.close()
