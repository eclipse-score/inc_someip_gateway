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
"""Pytest fixtures for integration tests using ITF.

Single instance tests:
  Uses ITF's native QemuTarget with QEMU plugin for full QEMU lifecycle management.
  ITF starts QEMU using pre-created TAP devices (tap-qemu1) attached to virbr0.

Dual instance tests:
  Uses qemu_utils.py to automatically start two QEMU instances via run_qemu.sh.
  Both instances are started before tests and stopped after.

Setup:
  sudo ./deployment/qemu/setup_bridge.sh setup  # Creates tap-qemu1 on virbr0
"""

import pytest

# ITF imports for SSH
from score.itf.core.com.ssh import Ssh

# Import qemu_utils fixtures for dual instance tests
# These are re-exported so pytest can discover them
from tests.itf_updates.qemu_utils import (
    qemu_ifs_image,
    qemu_run_script,
    qemu_dual_instances,
)


# ---------------------------------------------------------------------------
# ITF SSH Fixtures - wrap ITF's target fixture for test compatibility
# ---------------------------------------------------------------------------


@pytest.fixture
def ssh_client(target):
    """Create ITF SSH connection to QEMU guest.

    Uses ITF's target fixture (from qemu plugin) which automatically
    starts QEMU with the configured TAP devices.
    """
    with target.ssh() as connection:
        yield connection


@pytest.fixture
def dual_ssh_clients(qemu_dual_instances):
    """Create ITF SSH connections to both QEMU instances.

    Uses qemu_utils.py to automatically start two QEMUs via run_qemu.sh.
    Depends on qemu_dual_instances which handles QEMU lifecycle.
    """
    instance1, instance2 = qemu_dual_instances

    ssh1 = Ssh(
        target_ip=instance1.ssh_host,
        port=22,
        timeout=15,
        n_retries=5,
        retry_interval=2,
        username="root",
        password="",
    )
    ssh2 = Ssh(
        target_ip=instance2.ssh_host,
        port=22,
        timeout=15,
        n_retries=5,
        retry_interval=2,
        username="root",
        password="",
    )

    with ssh1 as client1, ssh2 as client2:
        yield client1, client2
