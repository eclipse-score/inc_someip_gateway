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


GATEWAY_IPC_BINDING_TEST_COMMAND = "gateway_ipc_binding_test --mw-com-config /opt/mw_com_config.json"

TEST_APPLICATIONS = [
    GATEWAY_IPC_BINDING_TEST_COMMAND,
    "null_serializer_test",
    "socom_stress_test",
    "socom_test",
]

# Emulated targets are slow, especially for the bigger test suites.
# A test is expected to finish in well under 10s.
TEST_APPLICATION_TIMEOUT_S = 30


@pytest.mark.parametrize("application", TEST_APPLICATIONS)
def test_application_succeeds_on_target(target: Target, application: str) -> None:
    process = target.execute_async(f"/opt/{application}")
    exit_code = process.wait(timeout_s=TEST_APPLICATION_TIMEOUT_S)
    assert exit_code == 0, process.get_output()
