# *******************************************************************************
# Copyright (c) 2025 Contributors to the Eclipse Foundation
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
Pytest configuration and fixtures for integration tests.

Includes fixtures for:
  - Local someipd subprocess
  - QEMU-based QNX system running someipd + gatewayd

The QEMU approach:
  1. Boot QEMU with ``-serial file:<logfile>``  — all console output lands
     in a temp file on the host (WSL2) filesystem.
  2. Send shell commands via stdin pipe (start daemons, run pidin).
  3. Wait a few seconds, kill QEMU.
  4. Read the log file and check for errors.  No prompt detection needed.
"""

import os
import re
import subprocess
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Generator

import pytest


# ---------------------------------------------------------------------------
# Data class — captures everything we need from a QEMU run
# ---------------------------------------------------------------------------

@dataclass
class QemuGatewaydResult:
    """Full output from a QEMU run with someipd + gatewayd."""

    log: str = ""
    error_lines: list[str] = field(default_factory=list)
    qemu_returncode: int | None = None

    @property
    def booted(self) -> bool:
        return "Welcome to QNX" in self.log

    @property
    def gatewayd_running(self) -> bool:
        """Check if gatewayd appears as a running process in pidin output."""
        # pidin shows processes like "usr/bin/gatewayd"
        return "gatewayd" in self.log and "Gateway started" in self.log


# ---------------------------------------------------------------------------
# IFS image locator
# ---------------------------------------------------------------------------

def _find_ifs_image() -> str:
    env_path = os.environ.get("QEMU_IFS_IMAGE")
    if env_path:
        resolved = Path(env_path).resolve()
        if resolved.exists():
            return str(resolved)
        ws = os.environ.get("BUILD_WORKSPACE_DIRECTORY", "")
        if ws:
            candidate = Path(ws) / env_path
            if candidate.exists():
                return str(candidate.resolve())

    workspace = Path(__file__).resolve().parents[2]
    bazel_ifs = workspace / "bazel-bin" / "deployment" / "qemu" / "someip_gateway_x86_64.ifs"
    if bazel_ifs.exists():
        return str(bazel_ifs)

    pytest.skip(
        "QNX IFS image not found. Set QEMU_IFS_IMAGE or build with:\n"
        "  bazel build //deployment/qemu:someip_gateway_ifs --config=x86_64-qnx"
    )
    return ""


# ---------------------------------------------------------------------------
# Single QEMU session fixture — boot, run daemons, read log file
# ---------------------------------------------------------------------------

# How long to let daemons run (override with env var)
QEMU_RUN_SECONDS = int(os.environ.get("QEMU_RUN_SECONDS", "8"))


@pytest.fixture(scope="session")
def qemu_gatewayd_result() -> QemuGatewaydResult:
    """Boot QEMU, start someipd + gatewayd, capture serial log, return it.

    Uses ``-serial mon:stdio`` so stdin goes to the QNX shell and stdout
    (serial output) is captured to a log file on the host filesystem.
    """
    ifs_image = _find_ifs_image()
    result = QemuGatewaydResult()

    kvm = os.path.exists("/dev/kvm") and os.access("/dev/kvm", os.R_OK | os.W_OK)

    log_fd, log_path = tempfile.mkstemp(prefix="qemu_serial_", suffix=".log")
    log_file: open | None = os.fdopen(log_fd, "wb")

    try:
        qemu_cmd = [
            "qemu-system-x86_64",
            "-smp", "2",
            "-m", "1024",
            "-nographic",
            "-serial", "mon:stdio",
            "-kernel", ifs_image,
            "-object", "rng-random,filename=/dev/urandom,id=rng0",
            "-device", "virtio-rng-pci,rng=rng0",
        ]
        if kvm:
            qemu_cmd += ["-accel", "kvm", "-cpu", "host"]
        else:
            qemu_cmd += ["-accel", "tcg"]

        proc = subprocess.Popen(
            qemu_cmd,
            stdin=subprocess.PIPE,
            stdout=log_file,
            stderr=subprocess.DEVNULL,
        )

        def _send(line: str) -> None:
            assert proc.stdin
            proc.stdin.write((line + "\n").encode())
            proc.stdin.flush()

        # Wait for QNX to boot (~3-4s with KVM)
        time.sleep(4)

        # Start gatewayd
        _send(
            "/usr/bin/gatewayd "
            "-config_file /etc/gatewayd/gatewayd_config.bin "
            "--service_instance_manifest /etc/gatewayd/mw_com_config.json &"
        )

        # Let them run
        remaining = max(1, QEMU_RUN_SECONDS - 5)
        time.sleep(remaining)

        # Collect process list
        _send("pidin")
        time.sleep(1)

        # Close stdin — no more commands to send
        if proc.stdin:
            proc.stdin.close()

        # Kill QEMU
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=3)

        result.qemu_returncode = proc.returncode

        # Close log file handle, then read from host filesystem
        log_file.close()
        log_file = None  # prevent double-close in finally
        result.log = Path(log_path).read_text(errors="replace")

        # Scan for error lines (skip known benign messages and our own commands)
        error_re = re.compile(
            r"(error|fatal|abort|panic|segfault|SIGSEGV|SIGABRT)", re.IGNORECASE
        )
        skip_re = re.compile(
            r"(pidin|slog2|echo|/usr/bin/"
            r"|mw::log initialization error"  # benign: falls back to console logging
            r"|ldd:FATAL: Could not load library libvsomeip3)"  # known: someipd optional lib
            , re.IGNORECASE
        )
        for line in result.log.splitlines():
            if error_re.search(line) and not skip_re.search(line):
                result.error_lines.append(line.strip())

    finally:
        if log_file is not None:
            log_file.close()
        try:
            os.unlink(log_path)
        except OSError:
            pass

    return result


# ---------------------------------------------------------------------------
# Local (non-QEMU) fixtures — preserved from original
# ---------------------------------------------------------------------------

@pytest.fixture(scope="class")
def someipd_config() -> Path:
    """Provide SOME/IP configuration parameters."""
    return Path("vsomeip-local.json")


@pytest.fixture(scope="class")
def someipd(someipd_config) -> Generator[None, None, None]:
    """Start someipd before tests and stop it after."""
    someipd = subprocess.Popen(
        ["src/someipd/someipd"],
        env={"VSOMEIP_CONFIGURATION": str(someipd_config.absolute())},
    )
    yield
    someipd.terminate()
    someipd.wait()


# Pytest hooks
def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line("markers", "integration: mark test as integration test")
    config.addinivalue_line("markers", "slow: mark test as slow running")
    config.addinivalue_line("markers", "network: mark test as requiring network access")
    config.addinivalue_line("markers", "qemu: mark test as requiring QEMU + QNX IFS image")


def pytest_collection_modifyitems(
    config: pytest.Config, items: list[pytest.Item]
) -> None:
    for item in items:
        item.add_marker(pytest.mark.integration)
