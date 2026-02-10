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

"""
QEMU integration tests for the SOME/IP Gateway.

Boot QNX in QEMU, start someipd + gatewayd, read the serial log file
from the host filesystem and check for errors.

Prerequisites:
  bazel build //deployment/qemu:someip_gateway_ifs --config=x86_64-qnx

Run:
  bazel test //tests/integration:integration_qemu \
    --test_env=QEMU_IFS_IMAGE=$(pwd)/bazel-bin/deployment/qemu/someip_gateway_x86_64.ifs
"""

from __future__ import annotations

import pytest
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from conftest import QemuGatewaydResult


@pytest.mark.qemu
class TestQemuGatewayd:
    """Verify that gatewayd starts and discovers its configured services."""

    def test_gatewayd_started(self, qemu_gatewayd_result: QemuGatewaydResult):
        """gatewayd should print 'Gateway started' on successful init."""
        assert "Gateway started" in qemu_gatewayd_result.log, (
            "gatewayd did not print 'Gateway started'.\n"
            f"Log tail:\n{qemu_gatewayd_result.log[-3000:]}"
        )
