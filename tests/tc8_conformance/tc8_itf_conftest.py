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
"""ITF conftest for TC8 conformance tests running inside QEMU via score_itf.

Loaded as a pytest plugin by the integration_test() Bazel target; provides
session-scoped fixtures for DUT IP addressing, someipd lifecycle, and vsomeip
config rendering on the QEMU guest.
"""

import logging
import os
import socket
import struct
import subprocess
import time
from typing import Generator

import pytest

from capture import stop_capture, tcpdump_capture
from score.itf.core.process.async_process import AsyncProcess

_logger = logging.getLogger(__name__)


def pytest_configure(config: pytest.Config) -> None:
    """Register TC8 markers (mirrors conftest.py)."""
    config.addinivalue_line("markers", "tc8: mark test as a TC8 conformance test")
    config.addinivalue_line(
        "markers", "conformance: mark test as a protocol conformance test"
    )
    config.addinivalue_line(
        "markers", "network: mark test as requiring a non-loopback network interface"
    )


def pytest_collection_modifyitems(
    config: pytest.Config, items: list[pytest.Item]
) -> None:
    """Auto-mark all collected items as tc8 and conformance."""
    for item in items:
        item.add_marker(pytest.mark.tc8)
        item.add_marker(pytest.mark.conformance)


class _AsyncProcessAdapter:
    """Adapts AsyncProcess to a Popen-compatible interface so TC8 tests can use
    .poll() to check liveness.
    """

    def __init__(self, proc: AsyncProcess) -> None:
        self._proc = proc

    def poll(self) -> int | None:
        """Return None if the process is running, 0 if it has exited."""
        return None if self._proc.is_running() else 0

    def stop(self) -> None:
        self._proc.stop()


def _wait_for_sd_readiness(
    tester_ip: str,
    timeout_secs: float = 10.0,
) -> bool:
    """Block until the DUT sends at least one multicast OfferService, or the
    timeout expires. Returns True on success, False on timeout.
    """
    from helpers.constants import SD_MULTICAST_ADDR, SD_PORT  # noqa: PLC0415

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    except AttributeError:
        pass
    sock.bind(("", SD_PORT))
    group_bytes = socket.inet_aton(SD_MULTICAST_ADDR)
    iface_bytes = socket.inet_aton(tester_ip)
    mreq = struct.pack("4s4s", group_bytes, iface_bytes)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

    deadline = time.monotonic() + timeout_secs
    try:
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(min(remaining, 1.0))
            try:
                data, _ = sock.recvfrom(65535)
            except socket.timeout:
                continue
            if len(data) < 20:
                continue
            service_id = int.from_bytes(data[0:2], "big")
            if service_id != 0xFFFF:
                continue
            sd_offset = 16
            if len(data) < sd_offset + 12:
                continue
            entries_len = int.from_bytes(data[sd_offset + 4 : sd_offset + 8], "big")
            entry_start = sd_offset + 8
            pos = entry_start
            while pos + 16 <= entry_start + entries_len and pos + 16 <= len(data):
                entry_type = data[pos]
                if entry_type == 0x01:  # OfferService
                    return True
                pos += 16
        return False
    finally:
        sock.close()


def _cleanup_vsomeip_sockets_on_target(target_init: object) -> None:
    """Remove stale vsomeip routing-manager sockets on the QEMU guest. Must be
    called before each DUT restart, or the new vsomeip instance will fail to
    become routing manager and send no SD messages.
    """
    for vsomeip_socket_glob in ("/tmp/vsomeip-*", "/var/run/vsomeip-*"):
        exit_code, output = target_init.execute(f"rm -f {vsomeip_socket_glob}")
        if exit_code != 0:
            _logger.warning(
                "vsomeip socket cleanup at %s returned %d: %s",
                vsomeip_socket_glob,
                exit_code,
                output.decode(errors="replace"),
            )


@pytest.fixture(scope="session", autouse=True)
def someip_pcap_capture() -> Generator[None, None, None]:
    """Capture SOME/IP traffic on the host TAP interface for the whole session.
    A missing or permission-denied tcpdump becomes a warning, not a failure.
    """
    _tc8_dut_ip_key = "TC8_DUT_IP"
    _tc8_dut_ip_default = "169.254.158.190"
    if _tc8_dut_ip_key not in os.environ:
        _logger.info(
            "someip_pcap_capture: TC8_DUT_IP env var is not set; "
            "defaulting to %s (the QEMU TAP bridge IP). "
            "Override via: export TC8_DUT_IP=<dut-ip>",
            _tc8_dut_ip_default,
        )
    dut_ip = os.environ.get(_tc8_dut_ip_key, _tc8_dut_ip_default)
    sd_port = os.environ.get("TC8_SD_PORT", "30490")
    svc_tcp_port = os.environ.get("TC8_SVC_TCP_PORT", "30510")

    # Multicast event group port (fixed in tc8_someipd_sd.json eventgroup 0x4465).
    _multicast_event_port = "40490"

    # BPF filter breakdown:
    #   udp port <sd_port>:   SOME/IP-SD (multicast and unicast)
    #   udp port 40490:       multicast event data (eventgroup 0x4465)
    #   host <dut_ip> udp:    all UDP to and from DUT (service and dynamic ports)
    #   host <dut_ip> tcp:    SOME/IP TCP only, excludes SSH port 22
    bpf = (
        f"(udp port {sd_port})"
        f" or (udp port {_multicast_event_port})"
        f" or (host {dut_ip} and (udp or (tcp and port {svc_tcp_port})))"
    )

    output_dir = os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR", ".")
    output_file = os.path.join(output_dir, "someip_capture.pcap")

    proc: subprocess.Popen[bytes] | None = None
    try:
        proc = tcpdump_capture(bpf, output_file=output_file)
        _logger.info(
            "someip_pcap_capture: tcpdump started (filter=%r output=%s)",
            bpf,
            output_file,
        )
    except (RuntimeError, OSError) as exc:
        _logger.warning(
            "someip_pcap_capture: tcpdump unavailable - continuing without pcap. %s",
            exc,
        )

    try:
        yield
    finally:
        if proc is not None:
            clean = stop_capture(proc, timeout=5.0)
            proc.__exit__(None, None, None)
            if clean:
                _logger.info(
                    "someip_pcap_capture: tcpdump stopped cleanly, pcap complete. "
                    "output=%s",
                    output_file,
                )
            else:
                _logger.warning(
                    "someip_pcap_capture: tcpdump did not respond to SIGINT within "
                    "5 s; SIGKILL used. All captured packets are present (written "
                    "per-packet via -U) but pcap trailer may be absent. output=%s",
                    output_file,
                )


@pytest.fixture(scope="session")
def dut_ip() -> str:
    """QEMU guest IP used as the unicast destination for all SD and data sends.
    Overrideable via TC8_DUT_IP.
    """
    return os.environ.get("TC8_DUT_IP", "169.254.158.190")


@pytest.fixture(scope="session")
def host_ip() -> str:
    """Host TAP interface IP for multicast group joins; pass to multicast
    socket helpers, not to unicast send helpers. Overrideable via
    TC8_TESTER_IP.
    """
    return os.environ.get("TC8_TESTER_IP", "169.254.21.88")


@pytest.fixture(scope="session")
def tester_ip(host_ip: str) -> str:
    """Tester-side socket bind IP; equals host_ip under QEMU."""
    return host_ip


_CONFIG_MAP: dict[str, str] = {
    "tc8_someipd_sd.json": "tc8_sd.json",
    "tc8_someipd_service.json": "tc8_service.json",
    "tc8_someipd_multi.json": "tc8_multi.json",
}


@pytest.fixture(scope="session")
def tc8_itf_config_setup(target_init: object, dut_ip: str) -> None:
    """Render vsomeip config templates on the QEMU guest using the DUT IP and
    port env vars. Runs once per session; all per-class DUT restarts share the
    rendered configs.
    """
    sd_port = os.environ.get("TC8_SD_PORT", "30490")
    svc_port = os.environ.get("TC8_SVC_PORT", "30509")
    svc_tcp_port = os.environ.get("TC8_SVC_TCP_PORT", "30510")
    service_id = os.environ.get("TC8_SERVICE_ID", "0x1234")
    instance_id = os.environ.get("TC8_INSTANCE_ID", "0x5678")

    _logger.info(
        "Rendering TC8 vsomeip configs on QEMU guest: DUT=%s SD=%s SVC=%s TCP=%s",
        dut_ip,
        sd_port,
        svc_port,
        svc_tcp_port,
    )

    for tmpl_name, out_name in [
        ("tc8_sd.json.tmpl", "tc8_sd.json"),
        ("tc8_service.json.tmpl", "tc8_service.json"),
        ("tc8_multi.json.tmpl", "tc8_multi.json"),
    ]:
        # Use | as sed delimiter to avoid conflicts with / in path substitutions.
        # Output goes to /tmp/ because the QNX8 IFS root is read-only at runtime.
        cmd = (
            f"sed"
            f" -e 's/__TC8_HOST_IP__/{dut_ip}/g'"
            f" -e 's/__TC8_SERVICE_ID__/{service_id}/g'"
            f" -e 's/__TC8_INSTANCE_ID__/{instance_id}/g'"
            f" -e 's/__TC8_SD_PORT__/{sd_port}/g'"
            f" -e 's/__TC8_SVC_PORT__/{svc_port}/g'"
            f" -e 's/__TC8_SVC_TCP_PORT__/{svc_tcp_port}/g'"
            f" -e 's|__TC8_LOG_DIR__|/tmp|g'"
            f" /{tmpl_name} > /tmp/{out_name}"
        )
        exit_code, output = target_init.execute(cmd)
        if exit_code != 0:
            pytest.fail(
                f"Failed to render {tmpl_name} on QEMU guest "
                f"(exit {exit_code}): {output.decode(errors='replace')}"
            )
        _logger.info("Rendered /%s -> /tmp/%s on QEMU guest", tmpl_name, out_name)


@pytest.fixture(scope="class")
def someipd_dut(
    target_init: object,
    tc8_itf_config_setup: None,  # noqa: ARG001 (ensures configs are rendered first)
    request: pytest.FixtureRequest,
    host_ip: str,
) -> Generator[_AsyncProcessAdapter, None, None]:
    """Launch someipd then gatewayd on the QEMU guest and yield a .poll() adapter.

    someipd is started first so it becomes the vsomeip routing manager.  gatewayd
    starts second and blocks internally until the IPC handshake with someipd
    completes.  The fixture skips the test class if someipd does not send an
    OfferService within 10 s.
    """
    config_name: str = getattr(request.module, "SOMEIP_CONFIG", "tc8_someipd_sd.json")
    guest_config = _CONFIG_MAP.get(config_name, "tc8_sd.json")

    _cleanup_vsomeip_sockets_on_target(target_init)

    # 1. Start someipd first — it becomes the vsomeip routing manager.
    _logger.info(
        "Launching someipd on QEMU guest: VSOMEIP_CONFIGURATION=/tmp/%s", guest_config
    )
    someipd_proc: AsyncProcess = target_init.execute_async(
        f"LD_LIBRARY_PATH=/ "
        f"VSOMEIP_CONFIGURATION=/tmp/{guest_config} "
        f"MW_LOG_CONFIG_FILE=/tc8_logging.json "
        f"/someipd -c /tc8_someipd_config.bin"
    )

    # 2. Start the ETS stub — provides the mw::com skeleton so gatewayd's
    #    StartFindService callback fires and gatewayd calls offer_event() in vsomeip.
    _logger.info("Launching tc8_ets_stub on QEMU guest")
    stub_proc: AsyncProcess = target_init.execute_async(
        f"MW_LOG_CONFIG_FILE=/tc8_logging.json "
        f"/tc8_ets_stub -s /tc8_ets_stub_mw_com_config.json"
    )

    # 3. Start gatewayd — connects to someipd IPC, discovers the stub via
    #    StartFindService, and calls offer_event() to make vsomeip advertise the service.
    _logger.info("Launching gatewayd on QEMU guest")
    gatewayd_proc: AsyncProcess = target_init.execute_async(
        f"MW_LOG_CONFIG_FILE=/tc8_logging.json "
        f"/gatewayd -c /tc8_someipd_config.bin -s /tc8_gatewayd_mw_com_config.json"
    )

    # 4. Wait for the OfferService multicast that vsomeip sends after gatewayd's
    #    offer_event() call completes.  Only then are TC8 test classes safe to run.
    if not _wait_for_sd_readiness(host_ip):
        for proc_name in ("gatewayd", "tc8_ets_stub", "someipd"):
            try:
                target_init.execute(f"pkill -9 {proc_name} 2>/dev/null || true")
            except Exception:  # noqa: BLE001
                pass
        gatewayd_proc.stop()
        stub_proc.stop()
        someipd_proc.stop()
        pytest.skip(
            "DUT did not reach SD main phase within 10 s (QEMU/ITF). "
            "Check TAP bridge, multicast route on guest, and vsomeip config."
        )

    _logger.info("DUT reached SD main phase on QEMU guest")

    adapter = _AsyncProcessAdapter(someipd_proc)
    try:
        yield adapter
    finally:
        # Force-kill all three binaries before .stop() to avoid the 15 s teardown
        # timeout that occurs when vsomeip does not handle SIGTERM cleanly or
        # when the SSH channel does not forward the signal to the remote process
        # group.  SIGKILL is unconditional.  The "|| true" ensures these lines
        # never raise even if the processes already exited.
        for proc_name in ("gatewayd", "tc8_ets_stub", "someipd"):
            try:
                target_init.execute(f"pkill -9 {proc_name} 2>/dev/null || true")
            except Exception:  # noqa: BLE001
                _logger.warning(
                    "force-kill of %s on QEMU guest failed; continuing teardown",
                    proc_name,
                )
        someipd_proc.stop()
        stub_proc.stop()
        gatewayd_proc.stop()
        _cleanup_vsomeip_sockets_on_target(target_init)
