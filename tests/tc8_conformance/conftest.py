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

import os
import socket
import struct
import subprocess
import time
from pathlib import Path
from typing import Generator

import pytest

from helpers.constants import SD_MULTICAST_ADDR, SD_PORT


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
    and ``__TC8_SVC_TCP_PORT__`` in a config template.

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
            "-service_instance_manifest",
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
# Multicast prerequisite check
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def require_multicast(host_ip: str) -> None:
    """Skip the entire module if the host cannot join the SD multicast group.

    SD uses ``SD_PORT`` (read from ``TC8_SD_PORT`` env var, default 30490).
    The source port of SD messages must equal the configured SD port; the
    DUT drops SD packets from other source ports.  Port isolation across
    parallel Bazel targets is achieved by assigning each target a unique
    ``TC8_SD_PORT`` value via the Bazel ``env`` attribute, so targets never
    compete for the same bind address.
    """
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    try:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind(("", SD_PORT))
        group = socket.inet_aton(SD_MULTICAST_ADDR)
        iface = socket.inet_aton(host_ip)
        mreq = struct.pack("4s4s", group, iface)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
    except OSError as exc:
        pytest.skip(
            f"Multicast socket setup failed on {host_ip}: {exc}. "
            "Set TC8_HOST_IP to a non-loopback interface IP or add a multicast "
            "route: sudo ip route add 224.0.0.0/4 dev lo"
        )
    finally:
        sock.close()


# ---------------------------------------------------------------------------
# DUT fixture
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def someipd_dut(
    request: pytest.FixtureRequest,
    tmp_path_factory: pytest.TempPathFactory,
    host_ip: str,
) -> Generator[subprocess.Popen[bytes], None, None]:
    """Start ``someipd`` as DUT and yield the Popen handle.

    Uses the module-level ``SOMEIP_CONFIG`` variable (default ``tc8_someipd_sd.json``).
    """
    config_name: str = getattr(request.module, "SOMEIP_CONFIG", "tc8_someipd_sd.json")
    tmp_dir = tmp_path_factory.mktemp("tc8_config")
    config_path = render_someip_config(config_name, host_ip, tmp_dir)

    proc = launch_someipd(config_path)
    yield proc
    terminate_someipd(proc)
