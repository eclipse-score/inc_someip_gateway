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
"""SSH connectivity tests for QNX QEMU image with SOME/IP Gateway.

These tests connect to a QEMU instance running with user-mode (SLIRP) networking.
QEMU must be started separately with: bazel run //deployment/qemu:run_qemu --config=x86_64-qnx

SSH is forwarded: localhost:2222 -> guest:22
"""


def test_ssh_connection(ssh_client):
    """Test basic SSH connectivity to QNX QEMU."""
    # Use full path since SSH session may not have /proc/boot in PATH
    stdin, stdout, stderr = ssh_client.exec_command("/proc/boot/uname -a")
    exit_code = stdout.channel.recv_exit_status()
    output = stdout.read().decode().strip()

    assert exit_code == 0, f"Command failed with: {stderr.read().decode()}"
    assert "QNX" in output, f"Expected QNX in uname output, got: {output}"


def test_ssh_with_root_user(ssh_client):
    """Test SSH connection reports root user."""
    # Use full path since SSH session may not have /proc/boot in PATH
    stdin, stdout, stderr = ssh_client.exec_command("/proc/boot/whoami")
    exit_code = stdout.channel.recv_exit_status()
    output = stdout.read().decode().strip()

    assert exit_code == 0, "whoami command failed"
    assert output == "root", f"Expected root user, got: {output}"


def test_network_interface_up(ssh_client):
    """Test that network interface is configured in guest."""
    # Use full path since SSH session may not have /proc/boot in PATH
    stdin, stdout, stderr = ssh_client.exec_command("/proc/boot/ifconfig vtnet0 2>/dev/null || /proc/boot/ifconfig -a")
    exit_code = stdout.channel.recv_exit_status()
    output = stdout.read().decode().strip()

    # SLIRP assigns 10.0.2.15 to guest
    assert "10.0.2.15" in output or "inet" in output.lower(), f"Network not configured: {output}"
