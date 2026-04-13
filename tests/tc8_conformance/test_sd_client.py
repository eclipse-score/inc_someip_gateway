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
"""TC8 SD Client lifecycle tests — ETS_081/082/084 and skipped ETS_096/097.

This module manages its own someipd lifecycle (launch/terminate per test)
and must NOT share the module-scoped ``someipd_dut`` fixture from conftest.py.
Running a private DUT avoids routing-manager conflicts with other
TC8 targets that bind the same SD port.

Port assignment (from BUILD.bazel env):
  TC8_SD_PORT  = 30498  (SD traffic)
  TC8_SVC_PORT = 30511  (service UDP traffic)

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import socket
import subprocess
import time
from pathlib import Path
from typing import List, Tuple

import pytest

from attribute_plugin import add_test_properties

from conftest import launch_someipd, render_someip_config, terminate_someipd
from helpers.constants import SD_PORT
from helpers.sd_helpers import open_multicast_socket
from helpers.sd_sender import (
    SOMEIPSDEntryType,
    capture_some_ip_messages,
    capture_unicast_sd_entries,
    open_sender_socket,
    send_subscribe_eventgroup,
)
from someip.header import SOMEIPHeader, SOMEIPSDHeader

# ---------------------------------------------------------------------------
# Module-level configuration
# ---------------------------------------------------------------------------

#: Uses the SD config (service 0x1234/0x5678, eventgroup 0x4455 UDP).
SOMEIP_CONFIG: str = "tc8_someipd_sd.json"

#: Service and eventgroup IDs (matches tc8_someipd_sd.json).
_SERVICE_ID: int = 0x1234
_INSTANCE_ID: int = 0x5678
_EVENTGROUP_ID: int = 0x4455
_MAJOR_VERSION: int = 0x00


# ---------------------------------------------------------------------------
# Module-level helper — collect SD messages from multicast socket
# ---------------------------------------------------------------------------


def _collect_sd_messages(
    capture_sock: socket.socket,
    count: int,
    timeout_secs: float,
) -> List[Tuple[SOMEIPHeader, SOMEIPSDHeader]]:
    """Receive up to *count* SOME/IP-SD messages within *timeout_secs*."""
    collected: List[Tuple[SOMEIPHeader, SOMEIPSDHeader]] = []
    deadline = time.monotonic() + timeout_secs
    while time.monotonic() < deadline and len(collected) < count:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        capture_sock.settimeout(min(remaining, 1.0))
        try:
            data, _ = capture_sock.recvfrom(65535)
        except socket.timeout:
            continue
        try:
            outer, _ = SOMEIPHeader.parse(data)
            if outer.service_id != 0xFFFF:
                continue
            sd_hdr, _ = SOMEIPSDHeader.parse(outer.payload)
            collected.append((outer, sd_hdr))
        except Exception:  # noqa: BLE001
            continue
    return collected


def _wait_port_free(port: int, retries: int = 20, delay: float = 0.1) -> None:
    """Spin until *port* can be bound (i.e. previous someipd has released it)."""
    for _ in range(retries):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                probe.bind(("", port))
            return
        except OSError:
            time.sleep(delay)


# ---------------------------------------------------------------------------
# Module fixture — provides a pre-rendered config path without managing DUT
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def sd_client_config(
    tmp_path_factory: pytest.TempPathFactory,
    host_ip: str,
) -> Path:
    """Render the DUT config template and return the path.

    Tests manage DUT lifecycle themselves (launch/terminate per test).
    """
    tmp_dir = tmp_path_factory.mktemp("tc8_sd_client_config")
    return render_someip_config(SOMEIP_CONFIG, host_ip, tmp_dir)


# ---------------------------------------------------------------------------
# ETS_096 / ETS_097 — TCP eventgroup (skipped: no TCP port allocated)
# ---------------------------------------------------------------------------


@add_test_properties(
    partially_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
    test_type="requirements-based",
    derivation_technique="requirements-analysis",
)
def test_ets_096_tcp_connection_before_subscribe() -> None:
    """ETS_096: TCP connection established before SubscribeEventgroup for TCP eventgroup."""
    pytest.skip(
        "ETS_096 requires a TCP eventgroup config (tc8_someipd_service.json) and a "
        "dedicated TC8_SVC_TCP_PORT.  The tc8_sd_client target allocates only "
        "TC8_SD_PORT=30498 and TC8_SVC_PORT=30511 (no TCP port).  Implement once "
        "a dedicated tc8_sd_client_tcp target with TC8_SVC_TCP_PORT is added."
    )


@add_test_properties(
    partially_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
    test_type="requirements-based",
    derivation_technique="requirements-analysis",
)
def test_ets_097_tcp_reconnect() -> None:
    """ETS_097: TCP reconnection after disconnect yields a new SubscribeAck."""
    pytest.skip(
        "ETS_097 requires a TCP eventgroup config (tc8_someipd_service.json) and a "
        "dedicated TC8_SVC_TCP_PORT.  The tc8_sd_client target allocates only "
        "TC8_SD_PORT=30498 and TC8_SVC_PORT=30511 (no TCP port).  Implement once "
        "a dedicated tc8_sd_client_tcp target with TC8_SVC_TCP_PORT is added."
    )


# ---------------------------------------------------------------------------
# ETS_084 — StopSubscribe ceases event delivery
# ---------------------------------------------------------------------------


class TestSDClientStopSubscribe:
    """ETS_084: Client-initiated StopSubscribeEventgroup stops event delivery."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_084_stop_subscribe_ceases_events(
        self,
        sd_client_config: Path,
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_084: After StopSubscribeEventgroup (TTL=0) the DUT stops sending events.

        This test has its own DUT lifecycle so that the StopSubscribe is verified
        on a fresh subscription with a known notification history.
        """
        proc = launch_someipd(sd_client_config)
        try:
            # Allow DUT to enter its main SD phase.
            time.sleep(2.0)

            sd_sock = open_sender_socket(tester_ip)
            notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            notif_sock.bind((tester_ip, 0))
            notif_port: int = notif_sock.getsockname()[1]

            try:

                def _subscribe() -> None:
                    send_subscribe_eventgroup(
                        sd_sock,
                        (host_ip, SD_PORT),
                        _SERVICE_ID,
                        _INSTANCE_ID,
                        _EVENTGROUP_ID,
                        _MAJOR_VERSION,
                        subscriber_ip=tester_ip,
                        subscriber_port=notif_port,
                    )

                _subscribe()
                acks = capture_unicast_sd_entries(
                    sd_sock,
                    filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                    timeout_secs=5.0,
                    resend=_subscribe,
                )
                assert any(
                    e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0 for e in acks
                ), "ETS_084: Prerequisite failed — no SubscribeAck received"

                # Verify at least one notification arrives before StopSubscribe.
                pre_notifs = capture_some_ip_messages(
                    notif_sock, _SERVICE_ID, timeout_secs=4.0
                )
                assert pre_notifs, (
                    "ETS_084: No notifications received after subscribe (prerequisite)"
                )

                # Send StopSubscribe.
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

                # Verify no further notifications within 4 s.
                post_notifs = capture_some_ip_messages(
                    notif_sock, _SERVICE_ID, timeout_secs=4.0
                )
                assert not post_notifs, (
                    f"ETS_084: {len(post_notifs)} notification(s) received after "
                    "StopSubscribeEventgroup (TTL=0). DUT must cease sending events."
                )
            finally:
                sd_sock.close()
                notif_sock.close()
        finally:
            terminate_someipd(proc)


# ---------------------------------------------------------------------------
# ETS_081 / ETS_082 — Server reboot detection (DUT restarts)
# ---------------------------------------------------------------------------


class TestSDClientReboot:
    """ETS_081/082: DUT reboot flag and session reset across restarts."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_reboot"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_081_reboot_flag_set_after_first_restart(
        self,
        sd_client_config: Path,
        host_ip: str,
    ) -> None:
        """ETS_081: After restart the first SD message has the reboot flag (bit 7) set.

        Lifecycle:
          1. Start DUT, drain 3 SD messages (stable state — reboot bit may clear).
          2. Terminate DUT.
          3. Start DUT again; open capture socket before launch.
          4. Assert first captured SD message has flag_reboot=True.
          5. Assert session_id resets to ≤ 2.
        """
        # --- First run: drain to stable state ---
        try:
            pre_sock = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip(f"Multicast socket unavailable on {host_ip}")

        try:
            proc1 = launch_someipd(sd_client_config)
        except Exception:
            pre_sock.close()
            raise

        try:
            _collect_sd_messages(pre_sock, count=3, timeout_secs=10.0)
        finally:
            pre_sock.close()
            terminate_someipd(proc1)

        _wait_port_free(SD_PORT)

        # --- Second run: capture first post-reboot message ---
        try:
            post_sock = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip("Multicast socket unavailable for post-reboot capture")

        try:
            proc2 = launch_someipd(sd_client_config)
        except Exception:
            post_sock.close()
            raise

        try:
            post_messages = _collect_sd_messages(post_sock, count=2, timeout_secs=10.0)
        finally:
            post_sock.close()
            terminate_someipd(proc2)

        assert post_messages, "ETS_081: No SD messages captured after restart"

        outer, sd_hdr = post_messages[0]

        # Reboot flag (SD flags byte bit 7 = 0x80).
        reboot_flag = getattr(sd_hdr, "flag_reboot", None)
        if reboot_flag is None:
            raw_flags = getattr(sd_hdr, "flags", 0)
            reboot_flag = bool(raw_flags & 0x80)

        assert reboot_flag, (
            "ETS_081: Reboot flag not set in first SD message after restart. "
            "The DUT must reset the reboot flag when restarted (PRS_SOMEIPSD_00385)."
        )
        assert outer.session_id <= 2, (
            f"ETS_081: session_id after restart = {outer.session_id}; "
            "expected ≤ 2 (session counter must reset on reboot)"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_reboot"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_082_reboot_flag_set_after_second_restart(
        self,
        sd_client_config: Path,
        host_ip: str,
    ) -> None:
        """ETS_082: Reboot flag and session reset hold across a second consecutive restart.

        Lifecycle: start → drain → stop → start → drain → stop → start → assert.
        This verifies that the DUT correctly resets SD state on every cold start,
        not just the first one.
        """

        def _drain_and_stop(pre_sock: socket.socket) -> None:
            """Launch DUT, drain 3 messages, terminate, wait for port release."""
            try:
                proc = launch_someipd(sd_client_config)
            except Exception:
                pre_sock.close()
                raise
            try:
                _collect_sd_messages(pre_sock, count=3, timeout_secs=10.0)
            finally:
                pre_sock.close()
                terminate_someipd(proc)
            _wait_port_free(SD_PORT)

        # First run.
        try:
            sock1 = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip(f"Multicast socket unavailable on {host_ip}")
        _drain_and_stop(sock1)

        # Second run.
        try:
            sock2 = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip("Multicast socket unavailable for second run")
        _drain_and_stop(sock2)

        # Third run: capture first message.
        try:
            post_sock = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip("Multicast socket unavailable for post-reboot capture")

        try:
            proc3 = launch_someipd(sd_client_config)
        except Exception:
            post_sock.close()
            raise

        try:
            post_messages = _collect_sd_messages(post_sock, count=2, timeout_secs=10.0)
        finally:
            post_sock.close()
            terminate_someipd(proc3)

        assert post_messages, "ETS_082: No SD messages captured after second restart"

        outer, sd_hdr = post_messages[0]

        reboot_flag = getattr(sd_hdr, "flag_reboot", None)
        if reboot_flag is None:
            raw_flags = getattr(sd_hdr, "flags", 0)
            reboot_flag = bool(raw_flags & 0x80)

        assert reboot_flag, (
            "ETS_082: Reboot flag not set after second consecutive restart. "
            "The DUT must reset the reboot flag on every cold start (PRS_SOMEIPSD_00385)."
        )
        assert outer.session_id <= 2, (
            f"ETS_082: session_id after second restart = {outer.session_id}; "
            "expected ≤ 2 (session counter must reset on every reboot)"
        )
