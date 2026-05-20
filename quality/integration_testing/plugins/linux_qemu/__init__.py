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
"""Custom QEMU plugin for Linux disk-boot integration tests.

This plugin extends the S-CORE ITF QEMU plugin to support booting from a disk
image (qcow2) with an optional cloud-init seed ISO, without requiring patches
to the upstream score_itf package.
"""

import logging
import os
import socket

import pytest

from score.itf.core.utils.bunch import Bunch
from score.itf.plugins.qemu.checks import pre_tests_phase
from score.itf.plugins.qemu.qemu_target import QemuTarget

from quality.integration_testing.plugins.linux_qemu.qemu_process import LinuxQemuProcess
from quality.integration_testing.plugins.linux_qemu.config import load_configuration

logger = logging.getLogger(__name__)


def pytest_addoption(parser):
    parser.addoption(
        "--qemu-config",
        action="store",
        required=True,
        help="Path to JSON file with QEMU target configuration.",
    )
    parser.addoption(
        "--qemu-image",
        action="store",
        required=True,
        help="Path to a QEMU disk image (qcow2).",
    )
    parser.addoption(
        "--qemu-seed-iso",
        action="store",
        default=None,
        help="Path to a cloud-init NoCloud seed ISO.",
    )
    parser.addoption(
        "--qemu-filesystem-tar",
        action="store",
        default=None,
        help="Path to a tar archive containing the test filesystem to deploy onto the QEMU target.",
    )


@pytest.fixture(scope="session")
def dlt():
    """Overrideable fixture for enabling DLT collection."""
    pass


@pytest.fixture(scope="session")
def config(request):
    return Bunch(
        qemu_config=load_configuration(request.config.getoption("qemu_config")),
        qemu_image=request.config.getoption("qemu_image"),
        qemu_seed_iso=request.config.getoption("qemu_seed_iso"),
    )


@pytest.fixture(scope="session")
def target_init(config, request, dlt):
    logger.info(f"Starting tests on host: {socket.gethostname()}")

    process = LinuxQemuProcess(
        path_to_qemu_image=config.qemu_image,
        available_ram=config.qemu_config.qemu_ram_size,
        available_cores=config.qemu_config.qemu_num_cores,
        network_adapters=[adapter.name for adapter in config.qemu_config.networks],
        port_forwarding=config.qemu_config.port_forwarding,
        seed_iso=config.qemu_seed_iso,
    )

    with process:
        target = QemuTarget(process, config.qemu_config)
        pre_tests_phase(target)
        yield target


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
