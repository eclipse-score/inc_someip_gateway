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
"""TC8 Event Notification tests: TC8-EVT-001 through TC8-EVT-006.

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
from helpers.constants import (
    DUT_RELIABLE_PORT,
    EVENT_FIELD_UINT8,
    EVENT_TEST_UINT8,
    EVENT_TEST_UINT8_RELIABLE,
    EVENTGROUP_TCP_RELIABLE,
    EVENTGROUP_UDP_MULTICAST,
    EVENTGROUP_UDP_UNICAST,
    INSTANCE_ID,
    MAJOR_VERSION,
    MULTICAST_ADDR,
    MULTICAST_EVENT_PORT,
    SD_PORT,
    SERVICE_ID,
)
from helpers.tcp_helpers import tcp_receive_response
from someip.header import SOMEIPMessageType

pytestmark = pytest.mark.skip(
    reason="Production stack does not forward events without mw::com ETS app (2026-08-11)"
)

SOMEIP_CONFIG: str = "tc8_someipd_service.json"


class TestEventNotificationFormat:
    """TC8-EVT-001/002, SOMEIPSRV_RPC_15/16: Notification format and delivery strategy."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__evt_subscription"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_evt_001_notification_message_type(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-001: Event notification has message_type = NOTIFICATION (0x02)."""
        assert dut.poll() is None, "DUT is not running"

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
                dut_ip,
                SD_PORT,
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_UDP_UNICAST,
                MAJOR_VERSION,
                notif_port=notif_port,
            )
            # DUT notifies every 500 ms (tc8_someipd_service.json update cycle); wait for at least one
            notifs = capture_notifications(
                notif_sock,
                EVENT_TEST_UINT8,
                SERVICE_ID,
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
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-002: Notification carries the correct event_id in the method_id field."""
        assert dut.poll() is None, "DUT is not running"

        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port = notif_sock.getsockname()[1]

        sd_sock = None
        try:
            sd_sock = subscribe_and_wait_ack(
                tester_ip,
                dut_ip,
                SD_PORT,
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_UDP_UNICAST,
                MAJOR_VERSION,
                notif_port=notif_port,
            )
            notifs = capture_notifications(
                notif_sock,
                EVENT_TEST_UINT8,
                SERVICE_ID,
                count=1,
                timeout_secs=5.0,
            )
            assert notifs, "TC8-EVT-002: No NOTIFICATION received after subscription"
            assert_notification_header(notifs[0], EVENT_TEST_UINT8)
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
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_15: Cyclic event notifications arrive at the configured cycle period."""
        assert dut.poll() is None, "DUT is not running"

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
                dut_ip,
                SD_PORT,
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_UDP_UNICAST,
                MAJOR_VERSION,
                notif_port=notif_port,
                ttl=30,  # 30 s > 10 s observation window; keeps subscription alive.
            )

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
                    if (
                        msg.service_id == SERVICE_ID
                        and msg.method_id == EVENT_TEST_UINT8
                    ):
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
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_16: Field event sends one initial notification then stays silent."""
        assert dut.poll() is None, "DUT is not running"

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
                dut_ip,
                SD_PORT,
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_UDP_UNICAST,
                MAJOR_VERSION,
                notif_port=notif_port,
            )

            initial = capture_notifications(
                notif_sock,
                EVENT_FIELD_UINT8,
                SERVICE_ID,
                count=1,
                timeout_secs=5.0,
            )
            assert initial, (
                "SOMEIPSRV_RPC_16: No initial-value notification received after "
                f"subscribing to eventgroup 0x{EVENTGROUP_UDP_UNICAST:04x}"
            )

            # Wait 3 seconds. No further 0x8006 notifications should arrive because
            # TestFieldUINT8 is not sent periodically (cycle_ms=0) and the field value
            # does not change during the test.  Filter to EVENT_FIELD_UINT8 only: other
            # members of eventgroup 0x0002 (e.g. 0x8001) send cyclic events which are
            # irrelevant to this field-change semantics check.
            subsequent = capture_notifications(
                notif_sock,
                EVENT_FIELD_UINT8,
                SERVICE_ID,
                count=1,
                timeout_secs=3.0,
            )
            assert not subsequent, (
                f"SOMEIPSRV_RPC_16: {len(subsequent)} unexpected notification(s) of "
                f"TestFieldUINT8 (0x{EVENT_FIELD_UINT8:04x}) received within 3 s; "
                "cycle_ms=0 and the field value has not changed — no further "
                "notifications expected"
            )
        finally:
            if sd_sock:
                sd_sock.close()
            notif_sock.close()


class TestEventSubscriptionGating:
    """TC8-EVT-003/004: Notifications only to subscribed endpoints."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__evt_subscription"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_evt_003_notification_only_to_subscriber(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-003: Subscribed endpoint receives notifications; unsubscribed does not."""
        assert dut.poll() is None, "DUT is not running"

        sub_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sub_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sub_sock.bind((tester_ip, 0))
        sub_port = sub_sock.getsockname()[1]

        unsub_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        unsub_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        unsub_sock.bind((tester_ip, 0))

        sd_sock = None
        try:
            sd_sock = subscribe_and_wait_ack(
                tester_ip,
                dut_ip,
                SD_PORT,
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_UDP_UNICAST,
                MAJOR_VERSION,
                notif_port=sub_port,
            )

            notifs = capture_notifications(
                sub_sock,
                EVENT_TEST_UINT8,
                SERVICE_ID,
                count=1,
                timeout_secs=5.0,
            )
            assert notifs, "TC8-EVT-003: No notification on subscribed socket"

            stray = capture_any_notifications(unsub_sock, SERVICE_ID, timeout_secs=2.0)
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
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-004: No notifications arrive before subscribing."""
        assert dut.poll() is None, "DUT is not running"

        # Listen without subscribing. DUT notifies every 2000 ms, so 3 s window is sufficient.
        listen_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listen_sock.bind((tester_ip, 0))

        try:
            stray = capture_any_notifications(listen_sock, SERVICE_ID, timeout_secs=3.0)
            assert not stray, (
                f"TC8-EVT-004: {len(stray)} notification(s) received without subscription"
            )
        finally:
            listen_sock.close()


class TestEventStopSubscribe:
    """TC8-EVT-006: StopSubscribeEventgroup ceases notifications."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__evt_subscription"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_evt_006_stop_subscribe_ceases_notifications(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-006: Notifications stop after StopSubscribeEventgroup (TTL=0)."""
        assert dut.poll() is None, "DUT is not running"

        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port = notif_sock.getsockname()[1]

        sd_sock = None
        try:
            sd_sock = subscribe_and_wait_ack(
                tester_ip,
                dut_ip,
                SD_PORT,
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_UDP_UNICAST,
                MAJOR_VERSION,
                notif_port=notif_port,
            )
            notifs = capture_notifications(
                notif_sock,
                EVENT_TEST_UINT8,
                SERVICE_ID,
                count=1,
                timeout_secs=5.0,
            )
            assert notifs, (
                "TC8-EVT-006: pre-condition failed — no notifications before StopSubscribe"
            )

            # StopSubscribe (TTL=0)
            send_subscribe_eventgroup(
                sd_sock,
                (dut_ip, SD_PORT),
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_UDP_UNICAST,
                MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=notif_port,
                ttl=0,
            )

            # DUT sends every 500 ms (tc8_someipd_service.json update cycle), so a 4 s window catches any leaks
            post = capture_any_notifications(notif_sock, SERVICE_ID, timeout_secs=4.0)
            assert not post, (
                f"TC8-EVT-006: {len(post)} notification(s) after StopSubscribeEventgroup"
            )
        finally:
            if sd_sock:
                sd_sock.close()
            notif_sock.close()


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
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-EVT-005: Notifications arrive on the multicast group after subscribing to 0x4465."""
        assert dut.poll() is None, "DUT is not running"

        try:
            capture_sd_offers(host_ip, min_count=1, timeout_secs=5.0)
        except (TimeoutError, OSError):
            pytest.skip("DUT did not offer service within timeout")

        # Open multicast notification socket BEFORE subscribing so we don't miss
        # the first notification the DUT sends to the multicast group.
        try:
            mcast_sock = open_multicast_socket(
                host_ip,
                multicast_group=MULTICAST_ADDR,
                port=MULTICAST_EVENT_PORT,
            )
        except OSError as exc:
            pytest.skip(
                f"Cannot join multicast group {MULTICAST_ADDR}:{MULTICAST_EVENT_PORT} "
                f"on {host_ip}: {exc}"
            )

        sd_sock = None
        try:
            sd_sock = subscribe_and_wait_ack(
                tester_ip,
                dut_ip,
                SD_PORT,
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_UDP_MULTICAST,
                MAJOR_VERSION,
                notif_port=mcast_sock.getsockname()[1],
            )
            # DUT sends notifications every 500 ms (tc8_someipd_service.json update cycle).
            # Allow up to 5 s for the first notification to arrive on the multicast socket.
            notifs = capture_any_notifications(mcast_sock, SERVICE_ID, timeout_secs=5.0)
            assert notifs, (
                f"TC8-EVT-005: No SOME/IP notification received on multicast "
                f"{MULTICAST_ADDR}:{MULTICAST_EVENT_PORT} within 5 s "
                f"after subscribing to eventgroup 0x{EVENTGROUP_UDP_MULTICAST:04x}"
            )
            assert notifs[0].message_type == SOMEIPMessageType.NOTIFICATION, (
                f"TC8-EVT-005: message_type mismatch on multicast socket: "
                f"got 0x{notifs[0].message_type:02x}, expected NOTIFICATION (0x02)"
            )
        finally:
            if sd_sock:
                sd_sock.close()
            mcast_sock.close()


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
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_RPC_17: Notification arrives on TCP after subscribing with TCP endpoint."""
        assert dut.poll() is None, "DUT is not running"

        try:
            capture_sd_offers(host_ip, min_count=1, timeout_secs=5.0)
        except (TimeoutError, OSError):
            pytest.skip("DUT did not offer service within timeout")

        # Connect TCP to DUT's reliable port. PRS_SOMEIPSD_00362 requires a
        # client-initiated TCP connection before the DUT will accept a reliable
        # subscription.  Bind to tester_ip so the source address matches the
        # subscriber_ip in the SubscribeEventgroup entry (DUT validates the exact
        # subscriber endpoint).
        tcp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        tcp_sock.settimeout(5.0)
        tcp_sock.bind((tester_ip, 0))
        tcp_sock.connect((dut_ip, DUT_RELIABLE_PORT))
        local_tcp_port = tcp_sock.getsockname()[1]
        time.sleep(0.5)  # give the DUT time to register the accepted TCP endpoint

        # Dummy UDP socket setup to satisfy vsomeip's mixed-group requirement
        udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_sock.bind((tester_ip, 0))
        local_udp_port = udp_sock.getsockname()[1]

        sd_sock = open_sender_socket(tester_ip)
        try:
            # Subscribe to the TCP-only eventgroup using the TCP connection's
            # source port so the DUT's TCP-connected check passes.
            send_subscribe_eventgroup(
                sd_sock,
                (dut_ip, SD_PORT),
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_TCP_RELIABLE,
                MAJOR_VERSION,
                subscriber_ip=tester_ip,
                tcp_port=local_tcp_port,
                udp_port=local_udp_port,
            )

            entries = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=10.0,
                resend=lambda: send_subscribe_eventgroup(
                    sd_sock,
                    (dut_ip, SD_PORT),
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_TCP_RELIABLE,
                    MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    tcp_port=local_tcp_port,
                    udp_port=local_udp_port,
                ),
                max_results=1,
            )
            acks = [
                e
                for e in entries
                if e.eventgroup_id == EVENTGROUP_TCP_RELIABLE and e.ttl > 0
            ]
            assert acks, (
                f"No SubscribeEventgroupAck for TCP eventgroup 0x{EVENTGROUP_TCP_RELIABLE:04x}"
            )

            # Eventgroup 0x0005 contains TestEventUINT8Reliable (0x8003, TCP-only).
            # Drain until we see the expected event ID.
            deadline = time.monotonic() + 8.0
            matched = None
            while time.monotonic() < deadline:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                msg = tcp_receive_response(tcp_sock, timeout_secs=remaining)
                if (
                    msg.message_type == SOMEIPMessageType.NOTIFICATION
                    and msg.method_id == EVENT_TEST_UINT8_RELIABLE
                ):
                    matched = msg
                    break
            assert matched is not None, (
                f"SOMEIPSRV_RPC_17: No NOTIFICATION with event_id=0x{EVENT_TEST_UINT8_RELIABLE:04x} "
                f"received over TCP within 8 s"
            )
        finally:
            sd_sock.close()
            tcp_sock.close()
