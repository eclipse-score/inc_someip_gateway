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
"""TC8 SD restart tests: reboot flag and session ID after DUT restarts (FORMAT_07, FORMAT_02), and skipped ETS_097.

This module manages its own someipd lifecycle (launch/terminate per test)
and must NOT share the module-scoped ``dut`` fixture from conftest.py, to
avoid routing-manager conflicts with other TC8 targets that bind the same
SD port.
"""

import socket
import time
from pathlib import Path
from typing import List, Tuple

import pytest

from attribute_plugin import add_test_properties

from helpers.dut_lifecycle import (
    cleanup_vsomeip_sockets,
    launch_dut,
    render_someip_config,
    terminate_dut,
)
from helpers.sd_helpers import open_multicast_socket
from helpers.someip_types import SOMEIPHeader, SOMEIPSDHeader

#: Uses the SD config.
SOMEIP_CONFIG: str = "tc8_someipd_sd.json"

#: Maximum wait time for SD messages, derived from TC8 spec Sec. 4 IUT parameters:
#: Listen Time (10 s) + Tolerance Time (1 s) + Process Time (2 s) = 13 s,
#: plus 2 s buffer for container/CI overhead.
_SD_CAPTURE_TIMEOUT_SECS: float = 15.0


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


@pytest.fixture(scope="module")
def sd_client_config(
    tmp_path_factory: pytest.TempPathFactory,
    host_ip: str,
    request: pytest.FixtureRequest,
) -> Path:
    """Render the DUT config template and return the path.

    Triggers ``tc8_itf_config_setup`` (session-scoped) first, since without
    pre-rendered guest templates the DUT binds loopback and SD multicast
    never reaches the TAP interface. ``launch_dut`` uses the path's filename
    to select the matching pre-rendered guest config.
    """
    request.getfixturevalue("tc8_itf_config_setup")
    tmp_dir = tmp_path_factory.mktemp("tc8_sd_client_config")
    return render_someip_config(SOMEIP_CONFIG, host_ip, tmp_dir)


def test_ets_097_tcp_reconnect() -> None:
    """ETS_097: not applicable, it needs the ETS client mode (clientServiceActivate).

    Follow-up: implement when the DUT supports the ETS client mode.
    """
    pytest.skip("The DUT has no ETS client mode (clientServiceActivate).")


class TestSDClientReboot:
    """FORMAT_07 and FORMAT_02: DUT reboot flag and session ID after restarts."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_reboot"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_07_reboot_flag_set_after_first_restart(
        self,
        sd_client_config: Path,
        host_ip: str,
        request: pytest.FixtureRequest,
    ) -> None:
        """FORMAT_07/02: After restart the first SD message has the reboot flag (bit 7) set and session_id 1."""
        target_init = request.getfixturevalue("target_init")

        try:
            pre_sock = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip(f"Multicast socket unavailable on {host_ip}")

        try:
            proc1 = launch_dut(sd_client_config, target_init=target_init)
        except Exception:
            pre_sock.close()
            raise

        try:
            _collect_sd_messages(pre_sock, count=3, timeout_secs=_SD_CAPTURE_TIMEOUT_SECS)
        finally:
            pre_sock.close()
            terminate_dut(proc1)

        cleanup_vsomeip_sockets(target_init=target_init)

        try:
            post_sock = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip("Multicast socket unavailable for post-reboot capture")

        try:
            proc2 = launch_dut(sd_client_config, target_init=target_init)
        except Exception:
            post_sock.close()
            raise

        try:
            post_messages = _collect_sd_messages(post_sock, count=2, timeout_secs=_SD_CAPTURE_TIMEOUT_SECS)
        finally:
            post_sock.close()
            terminate_dut(proc2)

        assert post_messages, "FORMAT_07: No SD messages captured after restart"

        outer, sd_hdr = post_messages[0]

        reboot_flag = getattr(sd_hdr, "flag_reboot", None)
        if reboot_flag is None:
            raw_flags = getattr(sd_hdr, "flags", 0)
            reboot_flag = bool(raw_flags & 0x80)

        assert reboot_flag, (
            "FORMAT_07: Reboot flag not set in first SD message after restart. "
            "The DUT must set the reboot flag when restarted (PRS_SOMEIPSD_00385)."
        )
        assert outer.session_id == 1, (
            f"FORMAT_02: session_id of the first message after restart = {outer.session_id}, expected 1"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_reboot"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_07_reboot_flag_set_after_second_restart(
        self,
        sd_client_config: Path,
        host_ip: str,
        request: pytest.FixtureRequest,
    ) -> None:
        """FORMAT_07/02: Reboot flag and session_id 1 hold across a second consecutive restart."""
        target_init = request.getfixturevalue("target_init")

        def _drain_and_stop(pre_sock: socket.socket) -> None:
            try:
                proc = launch_dut(sd_client_config, target_init=target_init)
            except Exception:
                pre_sock.close()
                raise
            try:
                _collect_sd_messages(pre_sock, count=3, timeout_secs=_SD_CAPTURE_TIMEOUT_SECS)
            finally:
                pre_sock.close()
                terminate_dut(proc)
            cleanup_vsomeip_sockets(target_init=target_init)

        try:
            sock1 = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip(f"Multicast socket unavailable on {host_ip}")
        _drain_and_stop(sock1)

        try:
            sock2 = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip("Multicast socket unavailable for second run")
        _drain_and_stop(sock2)

        try:
            post_sock = open_multicast_socket(host_ip)
        except OSError:
            pytest.skip("Multicast socket unavailable for post-reboot capture")

        try:
            proc3 = launch_dut(sd_client_config, target_init=target_init)
        except Exception:
            post_sock.close()
            raise

        try:
            post_messages = _collect_sd_messages(post_sock, count=2, timeout_secs=_SD_CAPTURE_TIMEOUT_SECS)
        finally:
            post_sock.close()
            terminate_dut(proc3)

        assert post_messages, "FORMAT_07: No SD messages captured after second restart"

        outer, sd_hdr = post_messages[0]

        reboot_flag = getattr(sd_hdr, "flag_reboot", None)
        if reboot_flag is None:
            raw_flags = getattr(sd_hdr, "flags", 0)
            reboot_flag = bool(raw_flags & 0x80)

        assert reboot_flag, (
            "FORMAT_07: Reboot flag not set after second consecutive restart. "
            "The DUT must set the reboot flag on every cold start (PRS_SOMEIPSD_00385)."
        )
        assert outer.session_id == 1, (
            f"FORMAT_02: session_id of the first message after second restart = {outer.session_id}, expected 1"
        )
