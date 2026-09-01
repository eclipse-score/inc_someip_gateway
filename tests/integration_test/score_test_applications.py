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

"""Run the test applications from `score/` on the target (Linux QEMU / QNX QEMU)."""

import pytest
from score.itf.plugins.core import Target

TEST_APPLICATIONS = [
    "gateway_ipc_binding_test",
    "null_serializer_test",
    "socom_stress_test",
    "socom_test",
]


def _is_qnx(target: Target) -> bool:
    exit_code, output = target.execute("uname")
    return exit_code == 0 and "QNX" in output.decode(errors="replace")


GATEWAY_IPC_BINDING_TEST = "gateway_ipc_binding_test"

# Emulated targets are slow, especially for the bigger test suites.
# A test is expected to finish in well under 10s.
TEST_APPLICATION_TIMEOUT_S = 30


def _list_gtest_suites(target: Target, application: str) -> list[str]:
    exit_code, output = target.execute(f"/{application} --gtest_list_tests")
    text = output.decode(errors="replace")
    assert exit_code == 0, text

    # gtest prints suites unindented and terminated by '.', their test cases indented below.
    suites = [
        line.strip()
        for line in text.splitlines()
        if line == line.lstrip() and line.rstrip().endswith(".")
    ]
    assert suites, text
    return suites


@pytest.mark.parametrize("application", TEST_APPLICATIONS)
def test_application_succeeds_on_target(target: Target, application: str) -> None:
    if _is_qnx(target) and application == GATEWAY_IPC_BINDING_TEST:
        pytest.skip(
            "gateway_ipc_binding_test is split into multiple processes on QNX, see test_gateway_ipc_binding_succeeds_on_target()"
        )

    process = target.execute_async(f"/{application}")
    exit_code = process.wait(timeout_s=TEST_APPLICATION_TIMEOUT_S)
    assert exit_code == 0, process.get_output()


def test_gateway_ipc_binding_succeeds_on_target(target: Target) -> None:
    """Run every gateway_ipc_binding_test suite in its own process.

    The suites cannot share a single process: on QNX the qnx_dispatch backend of
    score::message_passing leaks or is slow to release an OS resource across repeated
    connect/disconnect cycles (visible as MsgRegisterEvent starting to fail), and the exhaustion
    is cumulative over the whole binary run rather than tied to any single suite. Giving each
    suite a fresh process keeps the per-process connection count low enough to pass. This is a
    stopgap - the underlying resource exhaustion is in the message_passing dependency and needs
    its own investigation.
    """

    if not _is_qnx(target):
        pytest.skip(
            "gateway_ipc_binding_test is only split into multiple processes on QNX"
        )

    failures = []
    for suite in _list_gtest_suites(target, GATEWAY_IPC_BINDING_TEST):
        process = target.execute_async(
            f"/{GATEWAY_IPC_BINDING_TEST} --gtest_filter={suite}*"
        )
        exit_code = process.wait(timeout_s=TEST_APPLICATION_TIMEOUT_S)
        if exit_code != 0:
            failures.append(f"{suite} failed with {exit_code}:\n{process.get_output()}")

    assert not failures, "\n".join(failures)
