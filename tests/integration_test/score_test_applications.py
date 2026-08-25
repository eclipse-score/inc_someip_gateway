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

# TODO: gateway_ipc_binding_test hangs on QNX in its first test case.
APPLICATIONS_UNSUPPORTED_ON_QNX = ["gateway_ipc_binding_test"]

# Emulated targets are slow, especially for the bigger test suites.
TEST_APPLICATION_TIMEOUT_S = 600


def _is_qnx(target: Target) -> bool:
    exit_code, output = target.execute("uname")
    return exit_code == 0 and "QNX" in output.decode(errors="replace")


@pytest.mark.parametrize("application", TEST_APPLICATIONS)
def test_application_succeeds_on_target(target: Target, application: str) -> None:
    if application in APPLICATIONS_UNSUPPORTED_ON_QNX and _is_qnx(target):
        pytest.skip(f"{application} is not supported on QNX yet")

    process = target.execute_async(f"/{application}")
    exit_code = process.wait(timeout_s=TEST_APPLICATION_TIMEOUT_S)
    assert exit_code == 0, process.get_output()
