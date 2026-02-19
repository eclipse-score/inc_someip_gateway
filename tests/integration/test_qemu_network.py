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
