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
"""QEMU network connectivity tests.

Tests for verifying bridge networking between QEMU instances.

Single instance tests (bridge):
  bazel test //tests/integration:test_qemu_network_single --test_output=all --config=x86_64-qnx

Dual instance tests (bridge inter-QEMU):
  bazel test //tests/integration:test_qemu_network_dual --test_output=all --config=x86_64-qnx
"""

from __future__ import annotations

import os
import signal
import subprocess
import time
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator

import pytest
from score.itf.core.com.ssh import Ssh

from tests.itf_updates.qemu_utils import (
    qemu_ifs_image,
    qemu_run_script,
    qemu_dual_instances,
    start_qemu,
)


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

TCPDUMP_INTERFACE = "virbr0"
QEMU1_IP = "192.168.87.2"
QEMU2_IP = "192.168.87.3"
SERVICE_SETTLE_TIME = 20  # seconds to let SD exchange complete

SSH_OPTS = [
    "-o", "StrictHostKeyChecking=no",
    "-o", "UserKnownHostsFile=/dev/null",
    "-o", "LogLevel=ERROR",
]

# Service commands
GATEWAYD_CMD = (
    "/usr/bin/gatewayd -config_file /etc/gatewayd/gatewayd_config.bin "
    "--service_instance_manifest /etc/gatewayd/mw_com_config.json"
)
SOMEIPD_CMD = (
    "export VSOMEIP_CONFIGURATION=/etc/someipd/vsomeip.json && "
    "/usr/bin/someipd --service_instance_manifest /etc/someipd/mw_com_config.json"
)
SAMPLE_CLIENT_CMD = (
    "export VSOMEIP_CONFIGURATION=/etc/sample_client/vsomeip.json && "
    "/usr/bin/sample_client"
)


# ---------------------------------------------------------------------------
# Data Classes
# ---------------------------------------------------------------------------

@dataclass
class SDExpectation:
    """Expected SOME/IP-SD state for a host."""
    name: str
    offers: set[str]
    finds: set[str]
    subscribed: set[str]


@dataclass
class SDResult:
    """Parsed SOME/IP-SD result for a host."""
    offers: set[str]
    finds: set[str]
    subscribed: set[str]


# Expected SOME/IP-SD state per host
EXPECTED_SD = {
    QEMU1_IP: SDExpectation(
        name="someipd",
        offers={"Service 0x1234.0x5678"},
        finds={"Service 0x4321.0x5678"},
        subscribed={"Service 0x4321.0x5678, eventgroup 0x4465"},
    ),
    QEMU2_IP: SDExpectation(
        name="sample_client",
        offers={"Service 0x4321.0x5678"},
        finds={"Service 0x1234.0x5678"},
        subscribed={"Service 0x1234.0x5678, eventgroup 0x4465"},
    ),
}


# ---------------------------------------------------------------------------
# Helper Functions
# ---------------------------------------------------------------------------

def get_tmp_dir() -> Path:
    """Get temp directory (Bazel sandbox if available)."""
    return Path(os.environ.get("TEST_TMPDIR", "/tmp"))


def get_workspace_root() -> Path:
    """Get workspace root directory."""
    return Path(__file__).parent.parent.parent


def get_analyze_script() -> Path:
    """Get path to analyze_pcap_someip.py script."""
    return get_workspace_root() / "tests/integration/analyze_pcap_someip.py"


def ssh_run_bg(host: str, cmd: str) -> None:
    """Fire-and-forget a command on host via SSH."""
    subprocess.Popen(
        ["ssh", *SSH_OPTS, f"root@{host}", cmd],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def kill_stale_qemu_instances() -> None:
    """Kill any stale QEMU instances from previous runs."""
    for instance_id in (1, 2):
        pid_file = Path(f"/tmp/qemu-someip-gateway-{instance_id}.pid")
        if pid_file.exists():
            try:
                old_pid = int(pid_file.read_text().strip())
                os.kill(old_pid, signal.SIGTERM)
                print(f"Killed stale QEMU instance {instance_id} (PID {old_pid})")
                time.sleep(1)
                pid_file.unlink(missing_ok=True)
            except (ProcessLookupError, ValueError):
                pass
    subprocess.run(["pkill", "-f", "qemu-system"], capture_output=True)
    time.sleep(2)


def parse_analyze_output(output: str) -> dict[str, SDResult]:
    """Parse analyze_pcap_someip.py output into per-IP SDResult dict."""
    result: dict[str, SDResult] = {}
    current_ip: str | None = None
    current_section: str | None = None

    for line in output.splitlines():
        stripped = line.strip()

        # Detect SUMMARY header: "SUMMARY: someipd (192.168.87.2)"
        if stripped.startswith("SUMMARY:"):
            if (start := stripped.rfind("(")) != -1 and (end := stripped.rfind(")")) != -1:
                current_ip = stripped[start + 1:end]
                result[current_ip] = SDResult(offers=set(), finds=set(), subscribed=set())
                current_section = None
            continue

        if current_ip is None:
            continue

        # Detect section headers
        if stripped == "OFFERS:":
            current_section = "offers"
        elif stripped == "FINDS:":
            current_section = "finds"
        elif stripped.startswith("SUBSCRIBED SUCCESSFULLY TO:"):
            current_section = "subscribed"
        elif current_section and stripped.startswith("Service"):
            getattr(result[current_ip], current_section).add(stripped)

    return result


def analyze_pcap(pcap_file: Path) -> dict[str, SDResult]:
    """Run analyze_pcap_someip.py and return parsed results."""
    proc = subprocess.run(
        ["python3", str(get_analyze_script()), str(pcap_file)],
        capture_output=True,
        text=True,
        timeout=30,
    )
    assert proc.returncode == 0, f"analyze_pcap_someip.py failed:\n{proc.stderr}"
    return parse_analyze_output(proc.stdout)


@contextmanager
def tcpdump_capture(pcap_file: Path, log_file: Path) -> Iterator[subprocess.Popen]:
    """Context manager for tcpdump packet capture."""
    with open(log_file, "w") as log_fh:
        proc = subprocess.Popen(
            [
                "tcpdump", "-i", TCPDUMP_INTERFACE,
                "-w", str(pcap_file),
                f"host {QEMU1_IP} or host {QEMU2_IP}",
            ],
            stdout=subprocess.DEVNULL,
            stderr=log_fh,
        )
        time.sleep(2)  # Give tcpdump time to initialize
        assert proc.poll() is None, f"tcpdump exited early: {log_file.read_text()}"

        try:
            yield proc
        finally:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


def start_qemu1_services(host: str) -> None:
    """Start gatewayd and someipd on QEMU 1."""
    ssh_run_bg(host, GATEWAYD_CMD)
    time.sleep(3)
    ssh_run_bg(host, SOMEIPD_CMD)
    time.sleep(2)


def start_qemu2_services(host: str) -> None:
    """Start sample_client on QEMU 2."""
    ssh_run_bg(host, SAMPLE_CLIENT_CMD)


def verify_sd_match(ip: str, expected: SDExpectation, actual: SDResult) -> None:
    """Verify SD offers/finds/subscriptions match expected values."""
    print("-" * 80)
    print(f"CHECKING: {expected.name} ({ip})")
    print("-" * 80)

    for field in ("offers", "finds", "subscribed"):
        exp_val = getattr(expected, field)
        act_val = getattr(actual, field)
        label = "SUBSCRIBED SUCCESSFULLY TO" if field == "subscribed" else field.upper()
        print(f"  {label}:")
        print(f"    expected: {sorted(exp_val)}")
        print(f"    actual:   {sorted(act_val)}")
        assert exp_val == act_val, f"[{expected.name}] {label} mismatch"
        print("    -> OK")


def verify_partial_sd(ip: str, parsed: dict[str, SDResult], output: str) -> None:
    """Verify partial SD behavior (offers/finds present, no subscriptions)."""
    assert ip in parsed, f"No SOME/IP-SD data for {ip}.\nOutput:\n{output}"
    actual = parsed[ip]

    print(f"  OFFERS: {sorted(actual.offers)}")
    assert actual.offers, f"Should have offers, got: {actual.offers}"
    print("    -> OK (has offers)")

    print(f"  FINDS: {sorted(actual.finds)}")
    assert actual.finds, f"Should have finds, got: {actual.finds}"
    print("    -> OK (has finds)")

    print(f"  SUBSCRIBED: {sorted(actual.subscribed)}")
    assert not actual.subscribed, f"Should have no subscriptions, got: {actual.subscribed}"
    print("    -> OK (no subscriptions as expected)")


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------

@pytest.fixture
def ssh_client(target):
    """Create ITF SSH connection to QEMU guest."""
    with target.ssh() as connection:
        yield connection


@pytest.fixture
def dual_ssh_clients(qemu_dual_instances):
    """Create ITF SSH connections to both QEMU instances."""
    instance1, instance2 = qemu_dual_instances

    def make_ssh(host: str) -> Ssh:
        return Ssh(
            target_ip=host, port=22, timeout=15,
            n_retries=5, retry_interval=2,
            username="root", password="",
        )

    with make_ssh(instance1.ssh_host) as c1, make_ssh(instance2.ssh_host) as c2:
        yield c1, c2


# ---------------------------------------------------------------------------
# Test Classes
# ---------------------------------------------------------------------------

class TestSingleInstanceNetwork:
    """Tests for single QEMU instance with bridge networking."""

    def test_ssh_connection(self, ssh_client):
        """Test basic SSH connectivity to QNX QEMU as root user."""
        _, stdout, stderr = ssh_client.exec_command("/proc/boot/uname -a && /proc/boot/whoami")
        output = stdout.read().decode().strip()

        assert stdout.channel.recv_exit_status() == 0, f"Failed: {stderr.read().decode()}"
        assert "QNX" in output and "root" in output, f"Expected QNX/root, got: {output}"

    def test_bridge_interface_configured(self, ssh_client):
        """Verify vtnet0 interface is configured with 192.168.87.2."""
        _, stdout, stderr = ssh_client.exec_command("/proc/boot/ifconfig vtnet0")
        assert stdout.channel.recv_exit_status() == 0, f"Failed: {stderr.read().decode()}"
        assert "192.168.87.2" in stdout.read().decode()

    def test_bridge_gateway_reachable(self, ssh_client):
        """Verify guest can ping the bridge gateway (192.168.87.1)."""
        _, stdout, stderr = ssh_client.exec_command("/proc/boot/ping -c 3 192.168.87.1")
        output = stdout.read().decode()
        assert stdout.channel.recv_exit_status() == 0, f"Cannot ping: {stderr.read().decode()}"
        assert any(x in output for x in ["3 packets transmitted", "0% packet loss"])

    def test_bridge_default_route(self, ssh_client):
        """Verify default route points to 192.168.87.1."""
        _, stdout, _ = ssh_client.exec_command("/proc/boot/route get default 2>&1")
        assert "192.168.87.1" in stdout.read().decode()


class TestDualInstanceNetwork:
    """Tests for two QEMU instances communicating via bridge networking."""

    def test_both_instances_have_bridge_interface(self, dual_ssh_clients):
        """Verify both instances have vtnet0 interface configured."""
        client1, client2 = dual_ssh_clients

        _, out1, _ = client1.exec_command("/proc/boot/ifconfig vtnet0 2>/dev/null || echo NO_VTNET0")
        _, out2, _ = client2.exec_command("/proc/boot/ifconfig vtnet0 2>/dev/null || echo NO_VTNET0")
        output1, output2 = out1.read().decode(), out2.read().decode()

        assert "NO_VTNET0" not in output1 and "192.168.87.2" in output1
        assert "NO_VTNET0" not in output2 and "192.168.87.3" in output2

    def test_instance1_can_ping_instance2(self, dual_ssh_clients):
        """Verify instance 1 can ping instance 2."""
        client1, _ = dual_ssh_clients
        _, stdout, stderr = client1.exec_command("/proc/boot/ping -c 3 192.168.87.3")
        assert stdout.channel.recv_exit_status() == 0, f"Ping failed: {stderr.read().decode()}"

    def test_instance2_can_ping_instance1(self, dual_ssh_clients):
        """Verify instance 2 can ping instance 1."""
        _, client2 = dual_ssh_clients
        _, stdout, stderr = client2.exec_command("/proc/boot/ping -c 3 192.168.87.2")
        assert stdout.channel.recv_exit_status() == 0, f"Ping failed: {stderr.read().decode()}"

    def test_both_instances_can_reach_host(self, dual_ssh_clients):
        """Verify both instances can reach host (192.168.87.1)."""
        for i, client in enumerate(dual_ssh_clients, 1):
            _, stdout, _ = client.exec_command("/proc/boot/ping -c 1 192.168.87.1")
            assert stdout.channel.recv_exit_status() == 0, f"Instance {i} cannot reach host"


class TestSomeIPSD:
    """Tests for SOME/IP Service Discovery between two QEMU instances.

    OFFERS: Services offered by the host's someipd configuration.
    FINDS: Services the host is looking for.
    SUBSCRIBED: Successful subscriptions (requires matching offer from remote).
    """

    def test_someip_sd_offers_finds_subscriptions(self, qemu_dual_instances):
        """Verify SOME/IP-SD offers, finds, and subscriptions between QEMUs."""
        instance1, instance2 = qemu_dual_instances
        tmp = get_tmp_dir()
        pcap = tmp / "someip_sd_test.pcap"

        with tcpdump_capture(pcap, tmp / "tcpdump.log"):
            start_qemu1_services(instance1.ssh_host)
            start_qemu2_services(instance2.ssh_host)
            time.sleep(SERVICE_SETTLE_TIME)

        assert pcap.exists(), f"pcap not created at {pcap}"
        parsed = analyze_pcap(pcap)

        for ip, expected in EXPECTED_SD.items():
            assert ip in parsed, f"No SD data for {expected.name} ({ip})"
            verify_sd_match(ip, expected, parsed[ip])

    def test_negative_no_qemu_only_tcpdump(self):
        """Negative: No QEMUs running - no traffic expected."""
        kill_stale_qemu_instances()

        tmp = get_tmp_dir()
        pcap = tmp / "negative_no_qemu.pcap"

        with tcpdump_capture(pcap, tmp / "tcpdump_no_qemu.log"):
            time.sleep(5)

        parsed = analyze_pcap(pcap)
        assert not parsed, f"Expected no SD data, found: {list(parsed.keys())}"
        print("PASS: No SOME/IP-SD traffic when no QEMU instances running")

    def test_negative_both_qemus_no_services(self, qemu_dual_instances):
        """Negative: Both QEMUs running but no services - no traffic expected."""
        instance1, instance2 = qemu_dual_instances
        tmp = get_tmp_dir()
        pcap = tmp / "negative_no_services.pcap"

        print(f"QEMU 1: {instance1.ssh_host}, QEMU 2: {instance2.ssh_host}")
        print("Waiting 10 seconds without starting services...")

        with tcpdump_capture(pcap, tmp / "tcpdump_no_services.log"):
            time.sleep(10)

        parsed = analyze_pcap(pcap)
        assert not parsed, f"Expected no SD data, found: {list(parsed.keys())}"
        print("PASS: No SOME/IP-SD traffic when QEMUs running without services")

    def test_negative_only_qemu1_with_services(self, qemu_ifs_image, qemu_run_script):
        """Negative: Only QEMU 1 with services - offers/finds but no subscriptions."""
        kill_stale_qemu_instances()
        tmp = get_tmp_dir()
        pcap = tmp / "negative_only_qemu1.pcap"

        instance = start_qemu(qemu_ifs_image, qemu_run_script, instance_id=1)
        try:
            with tcpdump_capture(pcap, tmp / "tcpdump_qemu1.log"):
                print(f"Starting services on QEMU 1 ({instance.ssh_host})...")
                start_qemu1_services(instance.ssh_host)
                print(f"Waiting {SERVICE_SETTLE_TIME}s for SD exchanges...")
                time.sleep(SERVICE_SETTLE_TIME)
        finally:
            instance.stop()

        parsed = analyze_pcap(pcap)

        print("-" * 80)
        print("CHECKING: Only QEMU 1 running with services")
        print("-" * 80)

        verify_partial_sd(QEMU1_IP, parsed, str(pcap))
        assert QEMU2_IP not in parsed, f"QEMU 2 should not be present"
        print("  QEMU 2: Not present (as expected)")
        print("PASS: Only QEMU 1 - offers/finds present, no subscriptions")

    def test_negative_only_qemu2_with_services(self, qemu_ifs_image, qemu_run_script):
        """Negative: Only QEMU 2 with services - offers/finds but no subscriptions."""
        kill_stale_qemu_instances()
        tmp = get_tmp_dir()
        pcap = tmp / "negative_only_qemu2.pcap"

        instance = start_qemu(qemu_ifs_image, qemu_run_script, instance_id=2)
        try:
            with tcpdump_capture(pcap, tmp / "tcpdump_qemu2.log"):
                print(f"Starting sample_client on QEMU 2 ({instance.ssh_host})...")
                start_qemu2_services(instance.ssh_host)
                print(f"Waiting {SERVICE_SETTLE_TIME}s for SD exchanges...")
                time.sleep(SERVICE_SETTLE_TIME)
        finally:
            instance.stop()

        parsed = analyze_pcap(pcap)

        print("-" * 80)
        print("CHECKING: Only QEMU 2 running with services")
        print("-" * 80)

        verify_partial_sd(QEMU2_IP, parsed, str(pcap))
        assert QEMU1_IP not in parsed, f"QEMU 1 should not be present"
        print("  QEMU 1: Not present (as expected)")
        print("PASS: Only QEMU 2 - offers/finds present, no subscriptions")
