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

import os
import signal
import subprocess
import tempfile
import time


class TestSingleInstanceNetwork:
    """Tests for single QEMU instance with bridge networking."""

    def test_ssh_connection(self, ssh_client):
        """Test basic SSH connectivity to QNX QEMU as root user."""
        # Use full path since SSH session may not have /proc/boot in PATH
        _, stdout, stderr = ssh_client.exec_command("/proc/boot/uname -a && /proc/boot/whoami")
        output = stdout.read().decode().strip()

        assert stdout.channel.recv_exit_status() == 0, f"Command failed: {stderr.read().decode()}"
        assert "QNX" in output and "root" in output, f"Expected QNX and root, got: {output}"

    def test_bridge_interface_configured(self, ssh_client):
        """Verify vtnet0 (bridge) interface is configured with 192.168.87.2."""
        _, stdout, stderr = ssh_client.exec_command("/proc/boot/ifconfig vtnet0")
        exit_code = stdout.channel.recv_exit_status()
        output = stdout.read().decode().strip()

        assert exit_code == 0, f"ifconfig vtnet0 failed: {stderr.read().decode()}"
        assert "192.168.87.2" in output, f"Expected 192.168.87.2, got: {output}"

    def test_bridge_gateway_reachable(self, ssh_client):
        """Verify guest can ping the bridge gateway (host at 192.168.87.1)."""
        _, stdout, stderr = ssh_client.exec_command("/proc/boot/ping -c 3 192.168.87.1")
        exit_code = stdout.channel.recv_exit_status()
        output = stdout.read().decode().strip()

        assert exit_code == 0, f"Cannot ping gateway: {stderr.read().decode()}"
        assert "3 packets transmitted" in output or "3 received" in output.lower() or "0% packet loss" in output

    def test_bridge_default_route(self, ssh_client):
        """Verify default route points to 192.168.87.1."""
        # QNX route uses 'get default' to show default route info
        _, stdout, stderr = ssh_client.exec_command("/proc/boot/route get default 2>&1")
        exit_code = stdout.channel.recv_exit_status()
        output = stdout.read().decode().strip()

        # Default route should go through gateway 192.168.87.1
        assert "192.168.87.1" in output, f"Default route not via 192.168.87.1: {output}"


class TestDualInstanceNetwork:
    """Tests for two QEMU instances communicating via bridge networking."""

    def test_both_instances_have_bridge_interface(self, dual_ssh_clients):
        """Verify both instances have vtnet0 (bridge) interface configured."""
        client1, client2 = dual_ssh_clients

        # Check instance 1
        _, stdout, _ = client1.exec_command("/proc/boot/ifconfig vtnet0 2>/dev/null || echo 'NO_VTNET0'")
        output1 = stdout.read().decode().strip()

        # Check instance 2
        _, stdout, _ = client2.exec_command("/proc/boot/ifconfig vtnet0 2>/dev/null || echo 'NO_VTNET0'")
        output2 = stdout.read().decode().strip()

        assert "NO_VTNET0" not in output1, f"Instance 1 missing vtnet0: {output1}"
        assert "NO_VTNET0" not in output2, f"Instance 2 missing vtnet0: {output2}"
        assert "192.168.87.2" in output1, f"Instance 1 wrong IP: {output1}"
        assert "192.168.87.3" in output2, f"Instance 2 wrong IP: {output2}"

    def test_instance1_can_ping_instance2(self, dual_ssh_clients):
        """Verify instance 1 can ping instance 2 via bridge network."""
        client1, _ = dual_ssh_clients

        _, stdout, stderr = client1.exec_command("/proc/boot/ping -c 3 192.168.87.3")
        exit_code = stdout.channel.recv_exit_status()
        output = stdout.read().decode().strip()

        assert exit_code == 0, f"Instance 1 cannot ping instance 2: {stderr.read().decode()}\n{output}"

    def test_instance2_can_ping_instance1(self, dual_ssh_clients):
        """Verify instance 2 can ping instance 1 via bridge network."""
        _, client2 = dual_ssh_clients

        _, stdout, stderr = client2.exec_command("/proc/boot/ping -c 3 192.168.87.2")
        exit_code = stdout.channel.recv_exit_status()
        output = stdout.read().decode().strip()

        assert exit_code == 0, f"Instance 2 cannot ping instance 1: {stderr.read().decode()}\n{output}"

    def test_both_instances_can_reach_host(self, dual_ssh_clients):
        """Verify both instances can reach host via bridge (192.168.87.1)."""
        client1, client2 = dual_ssh_clients

        for i, client in enumerate([client1, client2], 1):
            _, stdout, stderr = client.exec_command("/proc/boot/ping -c 1 192.168.87.1")
            exit_code = stdout.channel.recv_exit_status()

            assert exit_code == 0, f"Instance {i} cannot reach host via bridge"


class TestSomeIPSD:
    """Tests for SOME/IP Service Discovery between two QEMU instances.

    Starts tcpdump to capture traffic, launches both QEMU instances with
    their respective services (gatewayd/someipd on QEMU 1, sample_client
    on QEMU 2), waits for service discovery to complete, then analyzes
    the captured pcap to verify offers, finds, and subscriptions.

     OFFERS:
      What the  service is offered by the host's someipd configuration.

      FINDS:
        What service is looking for.

      SUBSCRIBED SUCCESSFULLY TO::
        A subscription succeeds only if the remote side offers the
        matching service.
    """

    TCPDUMP_INTERFACE = "virbr0"
    QEMU1_IP = "192.168.87.2"
    QEMU2_IP = "192.168.87.3"
    SERVICE_SETTLE_TIME = 20  # seconds to let SD exchange complete

    SSH_OPTS = [
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        "-o", "LogLevel=ERROR",
    ]

    # Expected SOME/IP-SD state per host
    EXPECTED = {
        "192.168.87.2": {
            "name": "someipd",
            "offers": {"Service 0x1234.0x5678"},
            "finds": {"Service 0x4321.0x5678"},
            "subscribed": {"Service 0x4321.0x5678, eventgroup 0x4465"},
        },
        "192.168.87.3": {
            "name": "sample_client",
            "offers": {"Service 0x4321.0x5678"},
            "finds": {"Service 0x1234.0x5678"},
            "subscribed": {"Service 0x1234.0x5678, eventgroup 0x4465"},
        },
    }

    @staticmethod
    def _get_script_path(relative_path: str) -> str:
        """Resolve a path relative to the workspace root."""
        workspace_root = os.path.abspath(
            os.path.join(os.path.dirname(__file__), "..", "..")
        )
        return os.path.join(workspace_root, relative_path)

    @staticmethod
    def _wait_for_ssh(host: str, timeout: int = 60) -> bool:
        """Poll until SSH is accepting connections on *host*."""
        deadline = time.time() + timeout
        while time.time() < deadline:
            try:
                result = subprocess.run(
                    ["ssh"] + TestSomeIPSD.SSH_OPTS + [f"root@{host}", "echo ready"],
                    capture_output=True,
                    timeout=5,
                )
                if result.returncode == 0:
                    return True
            except subprocess.TimeoutExpired:
                pass
            time.sleep(2)
        return False

    @staticmethod
    def _ssh_run_bg(host: str, cmd: str) -> None:
        """Fire-and-forget a command on *host* via SSH."""
        subprocess.Popen(
            ["ssh"] + TestSomeIPSD.SSH_OPTS + [f"root@{host}", cmd],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    @staticmethod
    def _parse_analyze_output(output: str) -> dict:
        """Parse the output of analyze_pcap_someip.py into a per-IP dict.

        Returns a dict keyed by IP with sub-keys 'offers', 'finds',
        'subscribed', each a set of stripped entry strings.
        """
        result = {}
        current_ip = None
        current_section = None

        for line in output.splitlines():
            line_stripped = line.strip()

            # Detect SUMMARY header  →  "SUMMARY: someipd (192.168.87.2)"
            if line_stripped.startswith("SUMMARY:"):
                ip_start = line_stripped.rfind("(")
                ip_end = line_stripped.rfind(")")
                if ip_start != -1 and ip_end != -1:
                    current_ip = line_stripped[ip_start + 1 : ip_end]
                    result[current_ip] = {
                        "offers": set(),
                        "finds": set(),
                        "subscribed": set(),
                    }
                    current_section = None
                continue

            if current_ip is None:
                continue

            # Detect section headers
            if line_stripped == "OFFERS:":
                current_section = "offers"
                continue
            elif line_stripped == "FINDS:":
                current_section = "finds"
                continue
            elif line_stripped.startswith("SUBSCRIBED SUCCESSFULLY TO:"):
                current_section = "subscribed"
                continue

            # Collect entry lines (indented with "Service …")
            if current_section and line_stripped.startswith("Service"):
                result[current_ip][current_section].add(line_stripped)

        return result

    def test_someip_sd_offers_finds_subscriptions(self, qemu_dual_instances):
        """Verify SOME/IP-SD offers, finds, and subscriptions between QEMUs.

        1. Start tcpdump on virbr0 capturing SOME/IP traffic.
        2. Start gatewayd + someipd on QEMU 1 and sample_client on QEMU 2.
        3. Wait for service discovery to settle.
        4. Stop the services and tcpdump.
        5. Run analyze_pcap_someip.py on the capture.
        6. Assert expected offers / finds / subscriptions per host.
        """
        instance1, instance2 = qemu_dual_instances

        # Use TEST_TMPDIR (Bazel sandbox) if available, else /tmp
        tmp_dir = os.environ.get("TEST_TMPDIR", tempfile.gettempdir())
        pcap_file = os.path.join(tmp_dir, "someip_sd_test.pcap")
        analyze_script = self._get_script_path(
            "tests/integration/analyze_pcap_someip.py"
        )

        # --- 1. Start tcpdump ------------------------------------------------
        tcpdump_log = os.path.join(tmp_dir, "tcpdump_stderr.log")
        tcpdump_log_fh = open(tcpdump_log, "w")
        tcpdump_proc = subprocess.Popen(
            [
                "tcpdump",
                "-i", self.TCPDUMP_INTERFACE,
                "-w", pcap_file,
                f"host {self.QEMU1_IP} or host {self.QEMU2_IP}",
            ],
            stdout=subprocess.DEVNULL,
            stderr=tcpdump_log_fh,
        )
        # Give tcpdump a moment to initialise the capture
        time.sleep(2)
        assert tcpdump_proc.poll() is None, (
            f"tcpdump exited early (rc={tcpdump_proc.returncode}). "
            f"stderr: {open(tcpdump_log).read()}"
        )

        try:
            # --- 2. Launch services on both QEMUs -----------------------------

            # QEMU 1 – gatewayd then someipd (mirrors setup_qemu_1.sh)
            self._ssh_run_bg(
                instance1.ssh_host,
                "/usr/bin/gatewayd -config_file /etc/gatewayd/gatewayd_config.bin "
                "--service_instance_manifest /etc/gatewayd/mw_com_config.json",
            )
            time.sleep(3)
            self._ssh_run_bg(
                instance1.ssh_host,
                "export VSOMEIP_CONFIGURATION=/etc/someipd/vsomeip.json && "
                "/usr/bin/someipd --service_instance_manifest /etc/someipd/mw_com_config.json",
            )
            time.sleep(2)

            # QEMU 2 – sample_client (mirrors setup_qemu_2.sh)
            self._ssh_run_bg(
                instance2.ssh_host,
                "export VSOMEIP_CONFIGURATION=/etc/sample_client/vsomeip.json && "
                "/usr/bin/sample_client",
            )

            # --- 3. Wait for service discovery to settle ----------------------
            time.sleep(self.SERVICE_SETTLE_TIME)

        finally:
            # --- 4. Stop tcpdump ---------------------------------------------
            tcpdump_proc.send_signal(signal.SIGINT)
            try:
                tcpdump_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                tcpdump_proc.kill()
                tcpdump_proc.wait()
            tcpdump_log_fh.close()

        # --- 5. Analyse the pcap file -----------------------------------------
        result = subprocess.run(
            ["python3", analyze_script, pcap_file],
            capture_output=True,
            text=True,
            timeout=30,
        )
        output = result.stdout
        assert os.path.exists(pcap_file), (
            f"pcap file was not created at {pcap_file}. "
            f"tcpdump log: {open(tcpdump_log).read()}"
        )
        assert result.returncode == 0, (
            f"analyze_pcap_someip.py failed (rc={result.returncode}):\n"
            f"{result.stderr}\n{output}"
        )

        parsed = self._parse_analyze_output(output)

        # --- 6. Verify per host -----------------------------------------------
        for ip, expected in self.EXPECTED.items():
            host_name = expected["name"]

            print("-" * 80)
            print(f"CHECKING: {host_name} ({ip})")
            print("-" * 80)

            assert ip in parsed, (
                f"No SOME/IP-SD data found for {host_name} ({ip}).\n"
                f"Full output:\n{output}"
            )

            actual = parsed[ip]

            print(f"  OFFERS:")
            print(f"    expected: {sorted(expected['offers'])}")
            print(f"    actual:   {sorted(actual['offers'])}")
            assert expected["offers"] == actual["offers"], (
                f"[{host_name}] OFFERS mismatch.\n"
                f"  expected: {expected['offers']}\n"
                f"  actual:   {actual['offers']}"
            )
            print(f"    -> OK")

            print(f"  FINDS:")
            print(f"    expected: {sorted(expected['finds'])}")
            print(f"    actual:   {sorted(actual['finds'])}")
            assert expected["finds"] == actual["finds"], (
                f"[{host_name}] FINDS mismatch.\n"
                f"  expected: {expected['finds']}\n"
                f"  actual:   {actual['finds']}"
            )
            print(f"    -> OK")

            print(f"  SUBSCRIBED SUCCESSFULLY TO:")
            print(f"    expected: {sorted(expected['subscribed'])}")
            print(f"    actual:   {sorted(actual['subscribed'])}")
            assert expected["subscribed"] == actual["subscribed"], (
                f"[{host_name}] SUBSCRIBED SUCCESSFULLY TO mismatch.\n"
                f"  expected: {expected['subscribed']}\n"
                f"  actual:   {actual['subscribed']}"
            )
            print(f"    -> OK")
