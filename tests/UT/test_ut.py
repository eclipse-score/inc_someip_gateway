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

"""

import pytest

# ITF imports (score_itf main branch e994cb6 + OCI conflict patch)
from score.itf.core.com.ssh import execute_command_output


@pytest.fixture
def ssh_client(target):
    """Create ITF SSH connection to QEMU guest.

    Uses ITF's target fixture (from qemu plugin) which automatically
    starts QEMU with the configured TAP devices.
    """
    with target.ssh() as connection:
        yield connection


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
commands = ["/usr/bin/tests/cpp_test_main1", "/usr/bin/tests/cpp_test_main2"]


def test_cpp_unit_tests_on_qemu(ssh_client):
    """Run C++ GoogleTest binary on QEMU and verify all tests pass."""
    failed_tests = []
    run_logs = []

    for cmd in commands:
        exit_code, stdout_lines, stderr_lines = execute_command_output(
            ssh_client,
            cmd,
            timeout=10,
            max_exec_time=120,
            verbose=True,
        )

        output = "".join(stdout_lines)
        error_output = "".join(stderr_lines)

        # Save the logs so you can see what happened later
        run_logs.append(
            f"=== Logs for {cmd} ===\nSTDOUT:\n{output}\nSTDERR:\n{error_output}\n"
        )

        # Record failures but keep the loop going
        if exit_code != 0:
            failed_tests.append(f"{cmd} (Exit Code: {exit_code})")

    # Now that all tests have run, assert if any of them failed
    full_log_output = "\n".join(run_logs)
    assert len(failed_tests) == 0, (
        f"C++ tests failed!\nFailed binaries: {failed_tests}\n\n{full_log_output}"
    )
