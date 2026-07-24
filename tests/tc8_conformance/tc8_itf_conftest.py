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

Loaded as a pytest plugin (``-p tc8_itf_conftest``) by the ``integration_test()``
Bazel target.  Provides ITF-aware fixtures:

- ``dut_ip``       — QEMU guest IP, used as unicast send-to address for SD/data
- ``host_ip``      — host TAP interface IP for multicast group joins (= tester_ip)
- ``tester_ip``    — same as host_ip under QEMU (DUT and tester on different machines)
- ``someipd_dut``  — launches someipd via target.execute_async() on the QEMU guest
- ``tc8_itf_config_setup`` — renders vsomeip JSON templates on the QEMU guest via sed

Network topology (from qemu_config.json):
  QEMU guest (DUT)  : 169.254.158.190  (overrideable via TC8_DUT_IP)
  Host TAP gateway  : 169.254.21.88    (overrideable via TC8_TESTER_IP)

Ports default to canonical SOME/IP-SD values (overrideable via env vars):
  TC8_SD_PORT = 30490, TC8_SVC_PORT = 30509, TC8_SVC_TCP_PORT = 30510

Key topology split: ``dut_ip`` is the QEMU guest address — pass it to all unicast
send helpers (FindService, SubscribeEventgroup, TCP connect, UDP datagram).
``host_ip`` / ``tester_ip`` is the host TAP interface — pass it to multicast socket
helpers (open_multicast_socket, wait_for_sd_readiness).
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

# ---------------------------------------------------------------------------
# Marker registration (mirrors conftest.py so test metadata is preserved)
# ---------------------------------------------------------------------------


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


# ---------------------------------------------------------------------------
# AsyncProcess adapter — exposes subprocess.Popen-compatible .poll()
# ---------------------------------------------------------------------------


class _AsyncProcessAdapter:
    """Thin wrapper giving AsyncProcess a .poll() compatible with Popen.

    TC8 tests check ``someipd_dut.poll() is None`` to verify the DUT is still
    running.  AsyncProcess uses .is_running() instead; this adapter bridges the
    two interfaces without modifying the test files.
    """

    def __init__(self, proc: AsyncProcess) -> None:
        self._proc = proc

    def poll(self) -> int | None:
        """Return None if the process is running, 0 if it has exited."""
        return None if self._proc.is_running() else 0

    def stop(self) -> None:
        """Stop the underlying AsyncProcess."""
        self._proc.stop()


# ---------------------------------------------------------------------------
# SD readiness gate (copied from conftest.py — no dep on that file at runtime)
# ---------------------------------------------------------------------------


def _wait_for_sd_readiness(
    tester_ip: str,
    timeout_secs: float = 10.0,
) -> bool:
    """Wait until the DUT sends at least one multicast OfferService.

    Opens a short-lived multicast socket on *tester_ip* (host TAP interface),
    listens for SOME/IP-SD OfferService entries, and returns True as soon as
    one arrives.  Returns False on timeout.

    Port and multicast address are read from helpers.constants (derived from
    TC8_SD_PORT env var), so config changes propagate automatically.
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


# ---------------------------------------------------------------------------
# vsomeip socket cleanup on QEMU guest
# ---------------------------------------------------------------------------


def _cleanup_vsomeip_sockets_on_target(target_init: object) -> None:
    """Remove stale vsomeip routing-manager sockets on the QEMU guest.

    vsomeip creates Unix domain sockets for the routing manager.  A stale
    socket from a previous DUT run prevents the next instance from becoming
    routing manager, resulting in no SD messages.  Must be called before each
    DUT restart.

    Base path by OS (set in vsomeip CMakeLists.txt via VSOMEIP_BASE_PATH):
      Linux QEMU : /tmp/vsomeip-*
      QNX QEMU   : /var/run/vsomeip-*   (patch: ``set(VSOMEIP_BASE_PATH "/var/run")``)

    Both paths are cleaned unconditionally so this function remains
    OS-agnostic.  ``rm -f`` on a non-matching glob exits 0 (POSIX + toybox).
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


# ---------------------------------------------------------------------------
# Host-side tcpdump capture (session-scoped, autouse)
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session", autouse=True)
def someip_pcap_capture() -> Generator[None, None, None]:
    """Capture SOME/IP traffic on the host TAP interface for the whole session.

    Output: ``TEST_UNDECLARED_OUTPUTS_DIR/someip_capture.pcap`` (preserved by
    Bazel under ``bazel-testlogs/<target>/test.outputs/``).

    Unavailable tcpdump or denied CAP_NET_RAW becomes a warning, not a failure.
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

    # Config-derived BPF:
    #   udp port <sd_port>   — SOME/IP-SD (multicast + unicast)
    #   udp port 40490       — multicast event data (eventgroup 0x4465)
    #   host <dut_ip> udp    — all UDP to/from DUT (static TC8_SVC_PORT + dynamic ucei ports)
    #   host <dut_ip> tcp    — SOME/IP TCP service only; excludes SSH (22)
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
            "someip_pcap_capture: tcpdump unavailable — continuing without pcap. %s",
            exc,
        )

    try:
        yield
    finally:
        if proc is not None:
            clean = stop_capture(proc, timeout=5.0)
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


# ---------------------------------------------------------------------------
# IP fixtures (replace conftest.py host_ip / tester_ip / dut_ip for ITF targets)
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def dut_ip() -> str:
    """QEMU guest IP — unicast destination for all SD and data sends.

    FindService, SubscribeEventgroup, TCP connect, and UDP datagram sends
    MUST target this address so packets reach the QEMU guest, not the host.

    Overrideable via TC8_DUT_IP env var.
    Default: 169.254.158.190 (from qemu_config.json).
    """
    return os.environ.get("TC8_DUT_IP", "169.254.158.190")


@pytest.fixture(scope="session")
def host_ip() -> str:
    """Host TAP interface IP used for multicast join and multicast capture.

    Under QEMU the host TAP gateway (169.254.21.88) is the correct interface
    for IP_ADD_MEMBERSHIP so that SD multicast from the QEMU guest (arriving
    via the TAP bridge) is delivered to the host-side test sockets.

    Overrideable via TC8_TESTER_IP env var.

    Note: use ``dut_ip`` (not ``host_ip``) when sending unicast packets to
    the DUT (FindService, SubscribeEventgroup, TCP connect, UDP datagram).

    Session-scoped: the host TAP IP is fixed for the entire test run.
    """
    return os.environ.get("TC8_TESTER_IP", "169.254.21.88")


@pytest.fixture(scope="session")
def tester_ip(host_ip: str) -> str:
    """Tester-side socket bind IP.

    Under QEMU the DUT and tester run on different machines, so both bind
    SD_PORT on their own IP without conflict — no SO_REUSEADDR loopback trick
    needed.  Tester IP equals host_ip (host TAP interface).

    Overrideable via TC8_TESTER_IP env var (same as host_ip override).

    Session-scoped: must be at least as wide as ``host_ip`` (session).
    """
    return host_ip


# ---------------------------------------------------------------------------
# Config injection (session-scoped — runs once after QEMU boot)
# ---------------------------------------------------------------------------

_CONFIG_MAP: dict[str, str] = {
    "tc8_someipd_sd.json": "tc8_sd.json",
    "tc8_someipd_service.json": "tc8_service.json",
    "tc8_someipd_multi.json": "tc8_multi.json",
}


@pytest.fixture(scope="session")
def tc8_itf_config_setup(target_init: object, dut_ip: str) -> None:
    """Render TC8 vsomeip config templates on the QEMU guest via sed.

    Templates land at /<name>.tmpl (via the tc8-itf-pkg bundle extracted to /).
    Sed renders them to /tmp/<name> (writable on both Linux and QNX QEMU;
    QNX8 IFS root is read-only at runtime).  someipd reads configs from /tmp/
    via the VSOMEIP_CONFIGURATION env var.

    Runs once per QEMU session.  All per-class DUT restarts share the rendered
    configs.  ``dut_ip`` is the QEMU guest IP embedded as the vsomeip unicast
    endpoint.
    """
    sd_port = os.environ.get("TC8_SD_PORT", "30490")
    svc_port = os.environ.get("TC8_SVC_PORT", "30509")
    svc_tcp_port = os.environ.get("TC8_SVC_TCP_PORT", "30510")

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
        # Use | as delimiter for __TC8_LOG_DIR__ substitution to avoid / conflicts.
        # Output is written to /tmp/ (writable on both Linux and QNX QEMU) rather
        # than / (read-only IFS on QNX8 — "cannot create /tc8_sd.json: No such
        # file or directory").  Templates remain at / (IFS, readable on both OSes).
        cmd = (
            f"sed"
            f" -e 's/__TC8_HOST_IP__/{dut_ip}/g'"
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


# ---------------------------------------------------------------------------
# DUT fixture (class-scoped — fresh someipd per test class)
# ---------------------------------------------------------------------------


@pytest.fixture(scope="class")
def someipd_dut(
    target_init: object,
    tc8_itf_config_setup: None,  # noqa: ARG001 — ensures configs are rendered first
    request: pytest.FixtureRequest,
    host_ip: str,
) -> Generator[_AsyncProcessAdapter, None, None]:
    """Launch someipd --tc8-standalone on the QEMU guest; yield an adapter with .poll().

    Scope: class — each test class gets a fresh DUT process.  Uses the
    module-level SOMEIP_CONFIG variable (default tc8_someipd_sd.json) to
    select which rendered vsomeip config to pass via VSOMEIP_CONFIGURATION.

    Readiness gate: waits up to 10 s for the first multicast OfferService on
    the host TAP interface (host_ip).  Skips the test class if readiness is
    not reached (consistent with the score_py_pytest someipd_dut behaviour).

    Teardown: stops the AsyncProcess and removes stale vsomeip sockets.
    """
    config_name: str = getattr(request.module, "SOMEIP_CONFIG", "tc8_someipd_sd.json")
    guest_config = _CONFIG_MAP.get(config_name, "tc8_sd.json")

    _cleanup_vsomeip_sockets_on_target(target_init)

    _logger.info(
        "Launching someipd on QEMU guest: VSOMEIP_CONFIGURATION=/tmp/%s", guest_config
    )
    proc: AsyncProcess = target_init.execute_async(
        f"LD_LIBRARY_PATH=/ "
        f"VSOMEIP_CONFIGURATION=/tmp/{guest_config} "
        f"/someipd "
        f"--tc8-standalone "
        f"--service_instance_manifest /someipd_mw_com_config.json"
    )

    if not _wait_for_sd_readiness(host_ip):
        proc.stop()
        pytest.skip(
            "someipd DUT did not reach SD main phase within 10 s (QEMU/ITF). "
            "Check TAP bridge, multicast route on guest, and vsomeip config."
        )

    _logger.info("someipd DUT reached SD main phase on QEMU guest")
    adapter = _AsyncProcessAdapter(proc)
    try:
        yield adapter
    finally:
        # Force-kill someipd before proc.stop() to avoid the 15 s teardown
        # timeout that occurs when vsomeip does not handle SIGTERM cleanly or
        # when the SSH channel does not forward the signal to the remote process
        # group.  SIGKILL is unconditional; the "|| true" ensures this line
        # never raises even if someipd already exited.
        try:
            target_init.execute("pkill -9 someipd 2>/dev/null || true")
        except Exception:  # noqa: BLE001
            _logger.warning(
                "force-kill of someipd on QEMU guest failed; continuing teardown"
            )
        proc.stop()
        _cleanup_vsomeip_sockets_on_target(target_init)
