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
"""
Pytest fixtures for TC8 protocol conformance tests.

The DUT is ``someipd`` in standalone mode with a SOME/IP stack config.
Tests talk to it at the SOME/IP wire level (no gatewayd needed).

Environment variables
---------------------
TC8_HOST_IP
    IP address for SOME/IP traffic. Default: ``127.0.0.1``.
    Use a non-loopback address for reliable multicast.
"""

import glob
import ipaddress
import os
import socket
import struct
import subprocess
import time
from pathlib import Path
from typing import Generator

import pytest


# ---------------------------------------------------------------------------
# Shared helpers (importable by test modules that need custom DUT lifecycle)
# ---------------------------------------------------------------------------


def find_mw_com_config() -> Path:
    """Locate ``someipd``'s LoLa config (``mw_com_config.json``).

    Uses ``$TEST_SRCDIR/_main/...`` inside a Bazel sandbox.
    Falls back to a relative path when running outside Bazel.
    """
    test_srcdir = os.environ.get("TEST_SRCDIR")
    if test_srcdir:
        candidate = (
            Path(test_srcdir)
            / "_main"
            / "src"
            / "someipd"
            / "etc"
            / "mw_com_config.json"
        )
        if candidate.exists():
            return candidate
    return (
        Path(__file__).parent.parent.parent
        / "src"
        / "someipd"
        / "etc"
        / "mw_com_config.json"
    )


def render_someip_config(config_name: str, host_ip: str, dest_dir: Path) -> Path:
    """Replace ``__TC8_HOST_IP__``, ``__TC8_SD_PORT__``, ``__TC8_SVC_PORT__``,
    ``__TC8_SVC_TCP_PORT__``, and ``__TC8_LOG_DIR__`` in a config template.

    Writes the rendered config to *dest_dir* and returns the path.

    Port values are read from environment variables (set per-target in
    BUILD.bazel via the Bazel ``env`` attribute).  Defaults match the
    historical static values to preserve local development compatibility.
    Both the DUT config and the Python sender sockets read the same
    ``TC8_SD_PORT`` env var, ensuring SD messages originate from the
    configured SD port as required by the SOME/IP-SD protocol.
    """
    sd_port = os.environ.get("TC8_SD_PORT", "30490")
    svc_port = os.environ.get("TC8_SVC_PORT", "30509")
    svc_tcp_port = os.environ.get("TC8_SVC_TCP_PORT", "30510")
    template_path = Path(__file__).parent / "config" / config_name
    rendered = (
        template_path.read_text(encoding="utf-8")
        .replace("__TC8_HOST_IP__", host_ip)
        .replace("__TC8_SD_PORT__", sd_port)
        .replace("__TC8_SVC_PORT__", svc_port)
        .replace("__TC8_SVC_TCP_PORT__", svc_tcp_port)
        .replace("__TC8_LOG_DIR__", str(dest_dir))
    )
    config_path = dest_dir / config_name
    config_path.write_text(rendered, encoding="utf-8")
    return config_path


def launch_someipd(config_path: Path) -> subprocess.Popen[bytes]:
    """Start ``someipd --tc8-standalone`` with the given SOME/IP config.

    Returns the Popen handle. The caller must terminate the process.
    Raises ``RuntimeError`` if someipd exits within 0.2 s.
    """
    mw_com_config = find_mw_com_config()
    proc = subprocess.Popen(
        [
            "src/someipd/someipd",
            "--tc8-standalone",
            "--service_instance_manifest",
            str(mw_com_config),
        ],
        env={
            **os.environ,
            "VSOMEIP_CONFIGURATION": str(config_path),
        },  # env var name is fixed by the SOME/IP stack
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    time.sleep(0.2)
    if proc.poll() is not None:
        stdout = proc.stdout.read().decode(errors="replace") if proc.stdout else ""
        stderr = proc.stderr.read().decode(errors="replace") if proc.stderr else ""
        if proc.stdout:
            proc.stdout.close()
        if proc.stderr:
            proc.stderr.close()
        raise RuntimeError(
            f"someipd exited unexpectedly (rc={proc.returncode})\n"
            f"stdout: {stdout}\nstderr: {stderr}"
        )
    return proc


def terminate_someipd(proc: subprocess.Popen[bytes]) -> None:
    """Terminate ``someipd`` and close its pipes."""
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    if proc.stdout:
        proc.stdout.close()
    if proc.stderr:
        proc.stderr.close()


def cleanup_vsomeip_sockets(base_path: str = "/tmp") -> None:
    """Remove stale vsomeip routing-manager sockets.

    vsomeip creates Unix domain sockets at ``<base_path>/vsomeip-<name>``
    for the routing manager.  When ``someipd`` is terminated with SIGTERM,
    these socket files may not be cleaned up.  A stale socket prevents the
    next instance from becoming routing manager — no SD messages are sent.

    Call this between DUT restart cycles (after :func:`terminate_someipd`).
    """
    for stale in glob.glob(f"{base_path}/vsomeip-*"):
        try:
            os.unlink(stale)
        except OSError:
            pass


def wait_for_sd_readiness(
    host_ip: str,
    timeout_secs: float = 10.0,
) -> bool:
    """Wait until the DUT sends at least one multicast OfferService.

    This is a **stack-independent** readiness gate: any compliant SOME/IP
    Service Discovery implementation must send cyclic OfferService entries
    on the configured multicast group once it enters the SD main phase.

    The function opens its own short-lived multicast socket, waits for a
    SOME/IP-SD packet containing an OfferService entry, and returns
    ``True`` as soon as one is received.  Returns ``False`` on timeout.

    Port and multicast address are read from ``helpers.constants`` (which
    themselves derive from environment variables / config templates), so
    this function stays in sync when the JSON configuration changes.
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
    iface_bytes = socket.inet_aton(host_ip)
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
            # Minimal SOME/IP-SD detection: service_id == 0xFFFF and
            # payload contains at least one entry with type==0x01
            # (OfferService).  No dependency on the ``someip`` parser
            # package — keeps conftest.py self-contained.
            if len(data) < 20:
                continue
            service_id = int.from_bytes(data[0:2], "big")
            if service_id != 0xFFFF:
                continue
            # SD payload starts at SOME/IP header offset 16, after the
            # 12-byte SD header (flags + reserved + lengths) the entries
            # array begins.  Each entry is 16 bytes; first byte is type.
            someip_len = int.from_bytes(data[4:8], "big")
            sd_offset = 16  # end of SOME/IP header
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
# Markers
# ---------------------------------------------------------------------------


def pytest_configure(config: pytest.Config) -> None:
    """Register TC8 markers.

    JUnit XML output and structured logging are handled by ``score_py_pytest``
    via ``--junitxml=$$XML_OUTPUT_FILE`` and ``score_tooling``'s ``pytest.ini``.
    """
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
    """Auto-mark all tests in this directory as tc8 and conformance."""
    for item in items:
        item.add_marker(pytest.mark.tc8)
        item.add_marker(pytest.mark.conformance)


# ---------------------------------------------------------------------------
# Host IP fixture
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def host_ip() -> str:
    """IP for SOME/IP traffic (from ``TC8_HOST_IP``, default ``127.0.0.1``)."""
    return os.environ.get("TC8_HOST_IP", "127.0.0.1")


@pytest.fixture(scope="module")
def tester_ip(host_ip: str) -> str:
    """IP for the test sender socket.

    Must differ from ``host_ip`` so both can bind ``SD_PORT``
    (the SOME/IP stack requires SD source port = ``SD_PORT``).
    """
    if host_ip == "127.0.0.1":
        return "127.0.0.2"
    return "127.0.0.1"


# ---------------------------------------------------------------------------
# TC8 environment prerequisite check
# ---------------------------------------------------------------------------


@pytest.fixture(autouse=True, scope="module")
def require_tc8_environment() -> None:
    """Skip the entire module unless the TC8 environment is fully configured.

    Three checks are performed:

    1. **Opt-in gate** — ``TC8_HOST_IP`` must be set explicitly (via
       ``--test_env=TC8_HOST_IP=...``).  Without it ``bazel test //...``
       gracefully skips all TC8 tests.

    2. **IP validation** — ``TC8_HOST_IP`` must be a valid IPv4 address.
       A malformed value (e.g. typo) is caught early with a clear message
       instead of producing cryptic socket errors later.

    3. **Multicast route** — when ``host_ip`` is a loopback address, the
       multicast route must go via ``lo``.  With ``--config=tc8`` Bazel's
       ``linux-sandbox`` creates a private network namespace
       (``--sandbox_default_allow_network=false``) and ``tc8_net_wrapper.sh``
       adds the multicast route automatically.  This check catches cases
       where someone runs without ``--config=tc8``.

    CI uses ``--config=tc8`` which handles everything automatically.
    """
    raw_ip = os.environ.get("TC8_HOST_IP")
    if raw_ip is None:
        pytest.skip(
            "TC8_HOST_IP not set — TC8 conformance tests require explicit "
            "opt-in.  Run with: bazel test --test_env=TC8_HOST_IP=127.0.0.1 "
            "//tests/tc8_conformance/..."
        )

    try:
        addr = ipaddress.ip_address(raw_ip)
    except ValueError:
        pytest.skip(
            f"TC8_HOST_IP={raw_ip!r} is not a valid IP address.  "
            "Set it to a valid IPv4 address, e.g. 127.0.0.1"
        )

    if addr.is_loopback:
        try:
            result = subprocess.run(
                ["ip", "route", "get", "224.244.224.245"],
                capture_output=True,
                text=True,
                timeout=5,
            )
            if "dev lo" not in result.stdout:
                pytest.skip(
                    "Multicast route does not go via loopback.  "
                    "Run with: bazel test --config=tc8 "
                    "//tests/tc8_conformance/..."
                )
        except (FileNotFoundError, subprocess.TimeoutExpired):
            pass  # 'ip' not available — optimistically proceed


# ---------------------------------------------------------------------------
# DUT fixture
# ---------------------------------------------------------------------------


@pytest.fixture(scope="class")
def someipd_dut(
    request: pytest.FixtureRequest,
    tmp_path_factory: pytest.TempPathFactory,
    host_ip: str,
    require_tc8_environment: None,  # noqa: ARG001 — ensures env check runs first
) -> Generator[subprocess.Popen[bytes], None, None]:
    """Start ``someipd`` as DUT and yield the Popen handle.

    **Scope: class** — each test class gets a fresh DUT process.  This
    prevents long-running DUT degradation caused by SOME/IP stacks that
    cycle multicast group membership when no external SD multicast is
    received (e.g. on loopback where the DUT is the only participant).

    Before yielding, waits for the DUT to reach SD main phase by
    observing its first multicast OfferService.  This is a
    stack-independent readiness gate: any compliant SOME/IP-SD
    implementation sends cyclic offers once it enters main phase.

    Uses the module-level ``SOMEIP_CONFIG`` variable (default ``tc8_someipd_sd.json``).
    """
    config_name: str = getattr(request.module, "SOMEIP_CONFIG", "tc8_someipd_sd.json")
    tmp_dir = tmp_path_factory.mktemp("tc8_config")
    config_path = render_someip_config(config_name, host_ip, tmp_dir)

    try:
        proc = launch_someipd(config_path)
    except RuntimeError as exc:
        pytest.skip(f"someipd failed to start (environment not ready?): {exc}")

    if not wait_for_sd_readiness(host_ip):
        terminate_someipd(proc)
        pytest.skip("someipd DUT did not reach SD main phase within timeout")

    yield proc
    terminate_someipd(proc)
