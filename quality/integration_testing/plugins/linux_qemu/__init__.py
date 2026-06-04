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
import subprocess
import tempfile
import time

import pytest

from score.itf.core.utils.bunch import Bunch
from score.itf.plugins.qemu.qemu_target import QemuTarget

from quality.integration_testing.plugins.linux_qemu.qemu_process import LinuxQemuProcess
from quality.integration_testing.plugins.linux_qemu.config import load_configuration

logger = logging.getLogger(__name__)


# Cloud-init first-boot needs significantly more time than a pre-configured VM.
_SSH_BOOT_TIMEOUT = 20
_SSH_BOOT_RETRIES = 18
_TARGET_READY_ATTEMPTS = 8
_TARGET_READY_BACKOFF_SECONDS = 20


def _wait_for_target_ready(target):
    """Wait for SSH and SFTP to become available on the target.

    Uses longer timeouts than the upstream pre_tests_phase to accommodate
    cloud-init first-boot provisioning on a fresh qcow2 overlay.
    """
    last_error = None
    for attempt in range(1, _TARGET_READY_ATTEMPTS + 1):
        try:
            with target.ssh(
                timeout=_SSH_BOOT_TIMEOUT,
                n_retries=_SSH_BOOT_RETRIES,
                retry_interval=5,
            ) as ssh:
                result = ssh.execute_command("echo ready")
            if result != 0:
                raise RuntimeError("SSH command on target failed after boot")
            logger.info("Target SSH is ready")

            with target.sftp() as sftp:
                result = sftp.list_dirs_and_files("/")
            if not result:
                raise RuntimeError("SFTP command on target failed")
            logger.info("Target SFTP is ready")
            return
        except Exception as error:  # noqa: BLE001 - external transport exceptions vary by backend.
            last_error = error
            if attempt >= _TARGET_READY_ATTEMPTS:
                break

            backoff_seconds = attempt * _TARGET_READY_BACKOFF_SECONDS
            logger.warning(
                "Target is not ready yet (attempt %d/%d): %s. Retrying in %ds...",
                attempt,
                _TARGET_READY_ATTEMPTS,
                error,
                backoff_seconds,
            )
            time.sleep(backoff_seconds)

    raise RuntimeError(
        f"Target readiness check failed after {_TARGET_READY_ATTEMPTS} attempts: {last_error}"
    ) from last_error


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
    qemu_image = os.path.abspath(request.config.getoption("qemu_image"))
    qemu_seed_iso = request.config.getoption("qemu_seed_iso")
    if qemu_seed_iso:
        qemu_seed_iso = os.path.abspath(qemu_seed_iso)

    return Bunch(
        qemu_config=load_configuration(
            os.path.abspath(request.config.getoption("qemu_config"))
        ),
        qemu_image=qemu_image,
        qemu_seed_iso=qemu_seed_iso,
    )


@pytest.fixture(scope="session")
def target_init(config, request, dlt):
    logger.info(f"Starting tests on host: {socket.gethostname()}")

    # Create a qcow2 overlay backed by the pristine base image so that each
    # test session starts from an unmodified disk.  All writes go to the
    # ephemeral overlay which is discarded after the session.
    base_image = config.qemu_image
    overlay_fd, overlay_path = tempfile.mkstemp(suffix=".qcow2", prefix="qemu_overlay_")
    os.close(overlay_fd)
    try:
        subprocess.run(
            [
                "qemu-img",
                "create",
                "-f",
                "qcow2",
                "-b",
                base_image,
                "-F",
                "qcow2",
                overlay_path,
            ],
            check=True,
            capture_output=True,
        )
        logger.info(f"Created qcow2 overlay: {overlay_path} (backing: {base_image})")

        process = LinuxQemuProcess(
            path_to_qemu_image=overlay_path,
            available_ram=config.qemu_config.qemu_ram_size,
            available_cores=config.qemu_config.qemu_num_cores,
            network_adapters=[adapter.name for adapter in config.qemu_config.networks],
            port_forwarding=config.qemu_config.port_forwarding,
            seed_iso=config.qemu_seed_iso,
        )

        with process:
            target = QemuTarget(process, config.qemu_config)
            _wait_for_target_ready(target)
            yield target
    finally:
        if os.path.exists(overlay_path):
            os.remove(overlay_path)
            logger.info(f"Removed qcow2 overlay: {overlay_path}")


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
