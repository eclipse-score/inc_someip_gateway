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
"""Conftest plugin for QEMU-based Linux integration tests.

Provides a session-scoped autouse fixture that uploads and extracts the test
filesystem tarball onto the QEMU target via SSH before tests run.
"""

import logging
import os
import pytest

logger = logging.getLogger(__name__)


def pytest_addoption(parser):
    parser.addoption(
        "--qemu-filesystem-tar",
        action="store",
        default=None,
        help="Path to a tar archive containing the test filesystem to deploy onto the QEMU target.",
    )


@pytest.fixture(scope="session", autouse=True)
def deploy_filesystem(request, target_init):
    """Upload and extract the test filesystem tar onto the QEMU target."""
    tar_path = request.config.getoption("qemu_filesystem_tar")
    if not tar_path:
        return

    tar_path = os.path.abspath(tar_path)
    if not os.path.isfile(tar_path):
        pytest.skip(f"Filesystem tar not found: {tar_path}")
        return

    remote_tar = "/tmp/_test_filesystem.tar"

    logger.info(f"Uploading test filesystem from {tar_path} to {remote_tar}")
    target_init.upload(tar_path, remote_tar)

    logger.info("Extracting test filesystem on target")
    exit_code, output = target_init.execute(f"tar xf {remote_tar} -C /")
    if exit_code != 0:
        pytest.fail(f"Failed to extract filesystem tar on target: {output}")

    exit_code, _ = target_init.execute(f"rm -f {remote_tar}")
    logger.info("Test filesystem deployed successfully")
