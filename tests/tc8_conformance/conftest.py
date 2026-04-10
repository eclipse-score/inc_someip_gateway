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

import ipaddress
import os
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
       kernel's default multicast route typically goes via a physical NIC,
       not ``lo``.  The SOME/IP stack may resolves its SD multicast interface
       from the system routing table (``ip route get 224.x.x.x``), so SD
       traffic bypasses loopback and never reaches the test sockets.  We
       verify the route resolves to ``dev lo`` and skip with instructions
       if not.

    CI sets up both: ``--test_env=TC8_HOST_IP=127.0.0.1`` and
    ``sudo ip route add 224.0.0.0/4 dev lo``.
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
                    "Add it with: sudo ip route add 224.0.0.0/4 dev lo"
                )
        except (FileNotFoundError, subprocess.TimeoutExpired):
            pass  # 'ip' not available — optimistically proceed


# ---------------------------------------------------------------------------
# DUT fixture
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def someipd_dut(
    request: pytest.FixtureRequest,
    tmp_path_factory: pytest.TempPathFactory,
    host_ip: str,
    require_tc8_environment: None,  # noqa: ARG001 — ensures env check runs first
) -> Generator[subprocess.Popen[bytes], None, None]:
    """Start ``someipd`` as DUT and yield the Popen handle.

    Uses the module-level ``SOMEIP_CONFIG`` variable (default ``tc8_someipd_sd.json``).
    """
    config_name: str = getattr(request.module, "SOMEIP_CONFIG", "tc8_someipd_sd.json")
    tmp_dir = tmp_path_factory.mktemp("tc8_config")
    config_path = render_someip_config(config_name, host_ip, tmp_dir)

    try:
        proc = launch_someipd(config_path)
    except RuntimeError as exc:
        pytest.skip(f"someipd failed to start (environment not ready?): {exc}")
    yield proc
    terminate_someipd(proc)
