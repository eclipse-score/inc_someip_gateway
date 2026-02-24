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


import logging
import os
import signal
import socket
import subprocess
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Generator, Optional

import paramiko
import pytest

# Suppress noisy paramiko logs during SSH retry attempts
logging.getLogger("paramiko").setLevel(logging.CRITICAL)

# ---------------------------------------------------------------------------
# QEMU Configuration Constants
# ---------------------------------------------------------------------------

# Bridge networking configuration
# Guests are directly accessible on the bridge network
BRIDGE_SUBNET = "192.168.87"
SSH_USER = "root"
SSH_PORT = 22  # Direct SSH to guest (no port forwarding)
SSH_TIMEOUT = 10  # SSH connection timeout in seconds

# Boot timeout
QEMU_BOOT_TIMEOUT = 15  # seconds


def get_guest_ip(instance_id: int) -> str:
    """Get the bridge IP address for a QEMU instance.

    Args:
        instance_id: Instance ID (1 or 2).

    Returns:
        IP address string (e.g., '192.168.87.2' for instance 1).
    """
    return f"{BRIDGE_SUBNET}.{1 + instance_id}"


# Bazel workspace name in runfiles (bzlmod uses "_main" for the root module)
WORKSPACE_NAME = "_main"


# ---------------------------------------------------------------------------
# Bazel Runfiles Utilities
# ---------------------------------------------------------------------------


def get_runfile_path(relative_path: str) -> Optional[Path]:
    """Get the path to a file in Bazel runfiles.

    This uses standard Bazel environment variables to locate runfiles.
    Bazel sets RUNFILES_DIR when running tests via 'bazel test'.

    Args:
        relative_path: Path relative to workspace root (e.g., "deployment/qemu/run_qemu.sh")

    Returns:
        Absolute path to the file, or None if not found.
    """
    # Primary method: RUNFILES_DIR (set by Bazel test runner)
    runfiles_dir = os.environ.get("RUNFILES_DIR")
    if runfiles_dir:
        candidate = Path(runfiles_dir) / WORKSPACE_NAME / relative_path
        if candidate.exists():
            return candidate

    return None


# ---------------------------------------------------------------------------
# SSH Connection Utilities
# ---------------------------------------------------------------------------


def wait_for_ssh(host: str, port: int, user: str, timeout: int = 60) -> bool:
    """Wait for SSH to be fully ready (not just port open).


    Args:
        host: SSH hostname to connect to.
        port: SSH port number.
        user: SSH username.
        timeout: Maximum time to wait in seconds.

    Returns:
        True if SSH is ready, False if timeout exceeded.
    """
    start_time = time.time()
    last_error = None
    attempt = 0

    while time.time() - start_time < timeout:
        attempt += 1
        try:
            client = paramiko.SSHClient()
            client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
            client.connect(
                hostname=host,
                port=port,
                username=user,
                password="",
                timeout=5,
                allow_agent=False,
                look_for_keys=False,
                banner_timeout=10,
            )
            # Successfully connected - run a quick command to verify
            _, stdout, _ = client.exec_command("echo ready")
            stdout.channel.recv_exit_status()
            client.close()
            return True
        except (
            paramiko.SSHException,
            socket.error,
            EOFError,
            ConnectionResetError,
        ) as e:
            last_error = e
            # Exponential backoff: 1s, 2s, 3s... up to 5s max
            backoff = min(attempt, 5)
            time.sleep(backoff)

    print(f"SSH not ready after {timeout}s. Last error: {last_error}")
    return False


# ---------------------------------------------------------------------------
# QEMU Instance Management
# ---------------------------------------------------------------------------


@dataclass
class QEMUInstance:
    """Represents a running QEMU instance.

    Attributes:
        process: The subprocess.Popen object for the QEMU process.
        instance_id: Unique identifier for this instance (1, 2, etc.).
        ssh_host: SSH hostname/IP for this instance (192.168.87.2 for id=1, etc.).
        log_file: Path to the QEMU log file.
        pid_file: Path to the PID file for cleanup.
    """

    process: subprocess.Popen
    instance_id: int
    ssh_host: str
    log_file: Path
    pid_file: Path

    def get_ssh_client(self, retries: int = 3) -> paramiko.SSHClient:
        """Create SSH connection to this QEMU instance with retry logic.

        Args:
            retries: Number of connection attempts before failing.

        Returns:
            Connected paramiko.SSHClient.

        Raises:
            paramiko.SSHException: If all connection attempts fail.
        """
        last_error = None
        for attempt in range(retries):
            try:
                client = paramiko.SSHClient()
                client.set_missing_host_key_policy(paramiko.AutoAddPolicy())
                client.connect(
                    hostname=self.ssh_host,
                    port=SSH_PORT,
                    username=SSH_USER,
                    password="",
                    timeout=SSH_TIMEOUT,
                    allow_agent=False,
                    look_for_keys=False,
                    banner_timeout=15,
                )
                return client
            except (
                paramiko.SSHException,
                socket.error,
                EOFError,
                ConnectionResetError,
            ) as e:
                last_error = e
                if attempt < retries - 1:
                    time.sleep(2)
        raise paramiko.SSHException(
            f"Failed to connect after {retries} attempts: {last_error}"
        )

    def stop(self):
        """Stop this QEMU instance gracefully."""
        if self.process and self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()


def start_qemu(ifs_image: Path, run_script: Path, instance_id: int = 1) -> QEMUInstance:
    """Start a QEMU instance and wait for it to boot.

    Args:
        ifs_image: Path to the QNX IFS image.
        run_script: Path to the run_qemu.sh script.
        instance_id: Unique identifier for this instance (default: 1).

    Returns:
        QEMUInstance representing the running QEMU.

    Raises:
        pytest.fail: If QEMU fails to boot or SSH is not ready.
    """
    ssh_host = get_guest_ip(instance_id)

    # Create log file
    log_dir = Path(tempfile.gettempdir()) / "qemu-test"
    log_dir.mkdir(exist_ok=True)
    log_file = log_dir / f"qemu-instance-{instance_id}.log"
    pid_file = Path(f"/tmp/qemu-someip-gateway-{instance_id}.pid")

    # Clean up any existing instance on same ports
    if pid_file.exists():
        try:
            old_pid = int(pid_file.read_text().strip())
            os.kill(old_pid, signal.SIGTERM)
            time.sleep(1)
        except (ProcessLookupError, ValueError):
            pass
        pid_file.unlink(missing_ok=True)

    # Start QEMU
    env = os.environ.copy()
    env["QEMU_INSTANCE_ID"] = str(instance_id)

    with open(log_file, "w") as log:
        process = subprocess.Popen(
            [str(run_script), str(ifs_image), str(instance_id)],
            stdout=log,
            stderr=subprocess.STDOUT,
            env=env,
            start_new_session=True,  # Prevent signals from propagating
        )

    instance = QEMUInstance(
        process=process,
        instance_id=instance_id,
        ssh_host=ssh_host,
        log_file=log_file,
        pid_file=pid_file,
    )

    # Wait for SSH to be fully ready (not just port open)
    if not wait_for_ssh(ssh_host, SSH_PORT, SSH_USER, timeout=QEMU_BOOT_TIMEOUT):
        # Read log for debugging
        log_content = log_file.read_text() if log_file.exists() else "No log"
        instance.stop()
        pytest.fail(
            f"QEMU instance {instance_id} failed to boot within {QEMU_BOOT_TIMEOUT}s. "
            f"SSH not ready at {ssh_host}:{SSH_PORT}.\nLog:\n{log_content[-2000:]}"
        )

    return instance


# ---------------------------------------------------------------------------
# Common Pytest Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture(scope="session")
def qemu_ifs_image() -> Path:
    """Locate the QNX IFS image from Bazel runfiles."""
    path = "deployment/qemu/someip_gateway_x86_64.ifs"
    found = get_runfile_path(path)
    if found:
        return found
    pytest.fail(
        f"Cannot find QNX IFS image at '{WORKSPACE_NAME}/{path}'. "
        "Build it first: bazel build //deployment/qemu:someip_gateway_ifs --config=x86_64-qnx"
    )


@pytest.fixture(scope="session")
def qemu_run_script() -> Path:
    """Locate the QEMU run script from Bazel runfiles."""
    path = "deployment/qemu/run_qemu.sh"
    found = get_runfile_path(path)
    if found:
        return found
    pytest.fail(f"Cannot find run_qemu.sh at '{WORKSPACE_NAME}/{path}'.")


@pytest.fixture(scope="module")
def qemu_dual_instances(
    qemu_ifs_image: Path, qemu_run_script: Path
) -> Generator[tuple[QEMUInstance, QEMUInstance], None, None]:
    """Start two QEMU instances for inter-QEMU communication tests.

    Instance 1: 192.168.87.2:22 (direct bridge access)
    Instance 2: 192.168.87.3:22 (direct bridge access)
    """
    instance1 = start_qemu(
        ifs_image=qemu_ifs_image, run_script=qemu_run_script, instance_id=1
    )

    instance2 = start_qemu(
        ifs_image=qemu_ifs_image, run_script=qemu_run_script, instance_id=2
    )

    yield instance1, instance2

    instance1.stop()
    instance2.stop()
