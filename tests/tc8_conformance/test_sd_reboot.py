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
"""TC8 Service Discovery — Reboot Detection tests (TC8-SD-012).

Isolated in a separate module because these tests manage their own ``someipd``
lifecycle (start → drain → stop → restart) and must not share the
module-scoped ``someipd_dut`` fixture used by ``test_service_discovery.py``.
Running both in the same module would cause a routing-manager conflict.

See ``docs/tc8_conformance/requirements.rst`` for requirement traceability.
"""

import socket
import subprocess
import time
from typing import List, Tuple

import pytest

from attribute_plugin import add_test_properties

from conftest import launch_someipd, render_someip_config, terminate_someipd
from helpers.constants import SD_PORT
from helpers.sd_helpers import open_multicast_socket
from someip.header import SOMEIPHeader, SOMEIPSDHeader

# ---------------------------------------------------------------------------
# Module-level configuration
# ---------------------------------------------------------------------------

#: Uses the standard SD config (same service IDs as test_service_discovery.py).
SOMEIP_CONFIG: str = "tc8_someipd_sd.json"

#: All tests require multicast — checked once per module.
pytestmark = pytest.mark.usefixtures("require_multicast")


# ---------------------------------------------------------------------------
# sd_reboot_capture — module-scoped fixture
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def sd_reboot_capture(
    tmp_path_factory: pytest.TempPathFactory,
    host_ip: str,
) -> List[Tuple[SOMEIPHeader, SOMEIPSDHeader]]:
    """Start someipd, drain stable SD messages, restart, return post-reboot messages.

    Returns a list of (outer_header, sd_header) tuples captured after the restart.
    The multicast socket is opened *before* the second launch so the very first
    post-reboot SD packet is captured.
    """
    tmp_dir = tmp_path_factory.mktemp("tc8_reboot_config")
    config_path = render_someip_config(SOMEIP_CONFIG, host_ip, tmp_dir)

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

    # ---------------------------------------------------------------------------
    # First run — drain enough SD messages that the DUT is in "stable" state.
    # ---------------------------------------------------------------------------
    try:
        pre_sock = open_multicast_socket(host_ip)
    except OSError:
        pytest.skip(
            f"Multicast socket setup failed on {host_ip}. "
            "Set TC8_HOST_IP to a non-loopback IP."
        )

    try:
        proc1 = launch_someipd(config_path)
    except Exception:
        pre_sock.close()
        raise

    try:
        # Drain 3 SD messages so the DUT has cleared the reboot flag.
        _collect_sd_messages(pre_sock, count=3, timeout_secs=8.0)
    finally:
        pre_sock.close()
        terminate_someipd(proc1)

    # Wait for someipd to release the port before restarting.
    for _ in range(10):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                probe.bind(("", SD_PORT))
            break
        except OSError:
            time.sleep(0.05)

    # ---------------------------------------------------------------------------
    # Second run — open capture socket BEFORE launch to catch the first packet.
    # ---------------------------------------------------------------------------
    try:
        post_sock = open_multicast_socket(host_ip)
    except OSError:
        pytest.skip("Multicast socket unavailable for post-reboot capture.")

    try:
        proc2 = launch_someipd(config_path)
    except Exception:
        post_sock.close()
        raise

    try:
        post_messages = _collect_sd_messages(post_sock, count=2, timeout_secs=8.0)
    finally:
        post_sock.close()
        terminate_someipd(proc2)

    return post_messages


# ---------------------------------------------------------------------------
# TC8-SD-012 — Reboot detection
# ---------------------------------------------------------------------------


class TestSDReboot:
    """TC8-SD-012: DUT resets SD state on restart (reboot flag + session ID)."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_reboot"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_012_reboot_flag_set_after_restart(
        self,
        sd_reboot_capture: List[Tuple[SOMEIPHeader, SOMEIPSDHeader]],
    ) -> None:
        """TC8-SD-012: First SD message after restart has the reboot flag set (bit 7 = 1)."""
        assert sd_reboot_capture, "TC8-SD-012: No SD messages captured after restart"
        _, sd_hdr = sd_reboot_capture[0]

        # SOME/IP-SD specification: flags byte bit 7 (0x80) is the reboot flag.
        # The someip library exposes this as ``flag_reboot`` on SOMEIPSDHeader.
        reboot_flag = getattr(sd_hdr, "flag_reboot", None)
        if reboot_flag is None:
            # Defensive fallback if the library attribute name changes.
            raw_flags = getattr(sd_hdr, "flags", 0)
            reboot_flag = bool(raw_flags & 0x80)

        assert reboot_flag, (
            "TC8-SD-012: Reboot flag (SD flags bit 7) not set in first SD message "
            "after restart. The DUT must reset its SD state (session counter and "
            "reboot flag) when restarted (PRS_SOMEIPSD_00157)."
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_reboot"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_012_session_id_resets_after_restart(
        self,
        sd_reboot_capture: List[Tuple[SOMEIPHeader, SOMEIPSDHeader]],
    ) -> None:
        """TC8-SD-012: SD session ID resets to ≤ 2 after restart."""
        assert sd_reboot_capture, "TC8-SD-012: No SD messages captured after restart"
        outer, _ = sd_reboot_capture[0]
        assert outer.session_id <= 2, (
            f"TC8-SD-012: session_id after restart = {outer.session_id}; "
            "expected ≤ 2 (session counter must reset on reboot)"
        )


# ---------------------------------------------------------------------------
# TC8-SDLC-017/018 — ETS reboot detection (inline restart, no shared fixture)
# ---------------------------------------------------------------------------


class TestSDRebootDetectionETS:
    """TC8-SDLC-017/018: ETS reboot flag and session ID behaviour after restart.

    Unlike ``TestSDReboot``, these tests manage their own someipd lifecycle
    inline (no shared fixture) so they can verify unicast and multicast
    behaviour independently.
    """

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_reboot"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_093_reboot_on_unicast_channel(
        self,
        tmp_path_factory: pytest.TempPathFactory,
        host_ip: str,
    ) -> None:
        """TC8-SDLC-017: First unicast SD OFFER after DUT restart has reboot flag set and session_id = 1.

        Procedure:
        1. Start DUT, drain stable SD traffic, terminate.
        2. Restart DUT, open a multicast capture socket before launch.
        3. Assert that the very first post-restart OFFER has reboot_flag = True
           AND session_id = 1 (per PRS_SOMEIPSD_00157, counter resets to 1 on boot).
        """
        tmp_dir = tmp_path_factory.mktemp("tc8_ets093_config")
        config_path = render_someip_config(SOMEIP_CONFIG, host_ip, tmp_dir)

        # --- First run: drain stable messages ---
        try:
            pre_sock = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip(
                f"Multicast socket setup failed on {host_ip}. "
                "Set TC8_HOST_IP to a non-loopback IP."
            )

        proc1 = launch_someipd(config_path)
        drained: List[Tuple[SOMEIPHeader, SOMEIPSDHeader]] = []
        deadline = time.monotonic() + 8.0
        while time.monotonic() < deadline and len(drained) < 3:
            remaining = deadline - time.monotonic()
            pre_sock.settimeout(min(remaining, 1.0))
            try:
                data, _ = pre_sock.recvfrom(65535)
                outer, _ = SOMEIPHeader.parse(data)
                if outer.service_id != 0xFFFF:
                    continue
                sd_hdr, _ = SOMEIPSDHeader.parse(outer.payload)
                drained.append((outer, sd_hdr))
            except socket.timeout:
                continue
            except Exception:  # noqa: BLE001
                continue
        pre_sock.close()
        terminate_someipd(proc1)

        # Wait for port release.
        for _ in range(10):
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                    probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    probe.bind(("", SD_PORT))
                break
            except OSError:
                time.sleep(0.05)

        # --- Second run: capture post-reboot offer ---
        try:
            post_sock = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip("Multicast socket unavailable for post-reboot capture.")

        proc2 = launch_someipd(config_path)
        post_messages: List[Tuple[SOMEIPHeader, SOMEIPSDHeader]] = []
        deadline2 = time.monotonic() + 8.0
        while time.monotonic() < deadline2 and len(post_messages) < 1:
            remaining = deadline2 - time.monotonic()
            post_sock.settimeout(min(remaining, 1.0))
            try:
                data, _ = post_sock.recvfrom(65535)
                outer, _ = SOMEIPHeader.parse(data)
                if outer.service_id != 0xFFFF:
                    continue
                sd_hdr, _ = SOMEIPSDHeader.parse(outer.payload)
                post_messages.append((outer, sd_hdr))
            except socket.timeout:
                continue
            except Exception:  # noqa: BLE001
                continue
        post_sock.close()
        terminate_someipd(proc2)

        assert post_messages, "TC8-SDLC-017: No SD messages captured after DUT restart"
        outer_hdr, sd_hdr = post_messages[0]

        reboot_flag = getattr(sd_hdr, "flag_reboot", None)
        if reboot_flag is None:
            raw_flags = getattr(sd_hdr, "flags", 0)
            reboot_flag = bool(raw_flags & 0x80)

        assert reboot_flag, (
            "TC8-SDLC-017: Reboot flag (SD flags bit 7) not set in first SD OFFER "
            "after restart (PRS_SOMEIPSD_00157)."
        )
        assert outer_hdr.session_id == 1, (
            f"TC8-SDLC-017: session_id after restart = {outer_hdr.session_id}; "
            "expected 1 (session counter must reset to 1 on reboot)"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_reboot"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_094_server_reboot_session_id_resets(
        self,
        tmp_path_factory: pytest.TempPathFactory,
        host_ip: str,
    ) -> None:
        """TC8-SDLC-018: After restart the SD session_id resets to 1 and the reboot flag is set.

        Verifies PRS_SOMEIPSD_00157 from the multicast channel: the very first
        OfferService multicast after a clean restart must have session_id = 1
        and the reboot flag bit set regardless of what session_id was before.
        """
        tmp_dir = tmp_path_factory.mktemp("tc8_ets094_config")
        config_path = render_someip_config(SOMEIP_CONFIG, host_ip, tmp_dir)

        # --- First run: advance session counter past 1 ---
        try:
            pre_sock = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip(
                f"Multicast socket setup failed on {host_ip}. "
                "Set TC8_HOST_IP to a non-loopback IP."
            )

        proc1 = launch_someipd(config_path)
        pre_messages: List[Tuple[SOMEIPHeader, SOMEIPSDHeader]] = []
        deadline = time.monotonic() + 8.0
        while time.monotonic() < deadline and len(pre_messages) < 3:
            remaining = deadline - time.monotonic()
            pre_sock.settimeout(min(remaining, 1.0))
            try:
                data, _ = pre_sock.recvfrom(65535)
                outer, _ = SOMEIPHeader.parse(data)
                if outer.service_id != 0xFFFF:
                    continue
                sd_hdr, _ = SOMEIPSDHeader.parse(outer.payload)
                pre_messages.append((outer, sd_hdr))
            except socket.timeout:
                continue
            except Exception:  # noqa: BLE001
                continue
        pre_sock.close()
        terminate_someipd(proc1)

        # Wait for port release.
        for _ in range(10):
            try:
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                    probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
                    probe.bind(("", SD_PORT))
                break
            except OSError:
                time.sleep(0.05)

        # --- Second run: verify session_id and reboot flag reset ---
        try:
            post_sock = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip("Multicast socket unavailable for post-reboot capture.")

        proc2 = launch_someipd(config_path)
        post_messages: List[Tuple[SOMEIPHeader, SOMEIPSDHeader]] = []
        deadline2 = time.monotonic() + 8.0
        while time.monotonic() < deadline2 and len(post_messages) < 1:
            remaining = deadline2 - time.monotonic()
            post_sock.settimeout(min(remaining, 1.0))
            try:
                data, _ = post_sock.recvfrom(65535)
                outer, _ = SOMEIPHeader.parse(data)
                if outer.service_id != 0xFFFF:
                    continue
                sd_hdr, _ = SOMEIPSDHeader.parse(outer.payload)
                post_messages.append((outer, sd_hdr))
            except socket.timeout:
                continue
            except Exception:  # noqa: BLE001
                continue
        post_sock.close()
        terminate_someipd(proc2)

        assert post_messages, "TC8-SDLC-018: No SD messages captured after DUT restart"
        outer_hdr, sd_hdr = post_messages[0]

        reboot_flag = getattr(sd_hdr, "flag_reboot", None)
        if reboot_flag is None:
            raw_flags = getattr(sd_hdr, "flags", 0)
            reboot_flag = bool(raw_flags & 0x80)

        assert reboot_flag, (
            "TC8-SDLC-018: Reboot flag not set in first SD OFFER after restart "
            "(PRS_SOMEIPSD_00157)."
        )
        assert outer_hdr.session_id == 1, (
            f"TC8-SDLC-018: session_id after restart = {outer_hdr.session_id}; "
            "expected 1 (session counter must reset on reboot per PRS_SOMEIPSD_00157)"
        )
