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
"""Unit test execution on QNX QEMU image.

Runs C++ GoogleTest binaries on the QEMU guest and validates results.
QEMU must be started separately with: bazel run //deployment/qemu:run_qemu --config=x86_64-qnx
"""


def test_cpp_unit_tests_on_qemu(ssh_client):
    """Run C++ GoogleTest binary on QEMU and verify all tests pass."""
    # Execute the C++ unit test binary
    stdin, stdout, stderr = ssh_client.exec_command("/usr/bin/tests/cpp_test_main")
    exit_code = stdout.channel.recv_exit_status()
    output = stdout.read().decode()
    error_output = stderr.read().decode()

    # GoogleTest returns 0 on success, non-zero on failure
    assert exit_code == 0, f"C++ tests failed with exit code {exit_code}:\n{output}\n{error_output}"
