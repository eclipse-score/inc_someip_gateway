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
"""Unit test execution on QNX QEMU image using ITF.

Runs C++ GoogleTest binaries on the QEMU guest and validates results.

Prerequisites:
  Start QEMU first:
    bazel run //deployment/qemu:run_qemu_1 --config=x86_64-qnx

  Then run:
    bazel test //tests/UT:test_ut --test_output=all --cache_test_results=no
"""

import pytest

# ITF imports (score_itf 0.1.0)
from itf.plugins.com.ssh import Ssh, execute_command_output

# ---------------------------------------------------------------------------
# Configuration for connecting to existing QEMU
# ---------------------------------------------------------------------------

QEMU_SSH_HOST = "192.168.87.2"
QEMU_SSH_PORT = 22
QEMU_SSH_USER = "root"
QEMU_SSH_PASSWORD = ""


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def ssh_client():
    """Create ITF SSH connection to existing QEMU instance.

    ITF's Ssh class handles connection retries automatically.
    The QEMU instance must already be running before this test.
    """
    ssh = Ssh(
        target_ip=QEMU_SSH_HOST,
        port=QEMU_SSH_PORT,
        timeout=15,
        n_retries=5,
        retry_interval=2,
        username=QEMU_SSH_USER,
        password=QEMU_SSH_PASSWORD,
    )
    with ssh as connection:
        yield connection


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


def test_cpp_unit_tests_on_qemu(ssh_client):
    """Run C++ GoogleTest binary on QEMU and verify all tests pass."""
    # Execute the C++ unit test binary using ITF's execute_command_output
    exit_code, stdout_lines, stderr_lines = execute_command_output(
        ssh_client,
        "/usr/bin/tests/cpp_test_main",
        timeout=30,
        max_exec_time=120,
        verbose=True,
    )

    output = "".join(stdout_lines)
    error_output = "".join(stderr_lines)

    # GoogleTest returns 0 on success, non-zero on failure
    assert exit_code == 0, f"C++ tests failed with exit code {exit_code}:\n{output}\n{error_output}"
