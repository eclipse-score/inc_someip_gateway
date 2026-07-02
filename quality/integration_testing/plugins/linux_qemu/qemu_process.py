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
"""QEMU process management with disk boot support."""

import logging
import subprocess

from score.itf.core.process.console import PipeConsole
from quality.integration_testing.plugins.linux_qemu.qemu import DiskBootQemu

logger = logging.getLogger(__name__)


class LinuxQemuProcess:
    """Manages a QEMU process lifecycle with disk boot support."""

    def __init__(
        self,
        path_to_qemu_image,
        available_ram,
        available_cores,
        network_adapters=None,
        port_forwarding=None,
        seed_iso=None,
    ):
        self._path_to_qemu_image = path_to_qemu_image
        self._available_ram = available_ram
        self._available_cores = available_cores
        self._network_adapters = network_adapters or []
        self._port_forwarding = port_forwarding or []
        self._qemu = DiskBootQemu(
            self._path_to_qemu_image,
            self._available_ram,
            self._available_cores,
            seed_iso=seed_iso,
            network_adapters=self._network_adapters,
            port_forwarding=self._port_forwarding,
        )
        self._console = None

    def __enter__(self):
        return self.start()

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()

    def start(self):
        logger.info("Starting QEMU (disk boot)...")
        logger.info(f"Using QEMU image: {self._path_to_qemu_image}")
        subprocess_params = {
            "stdin": subprocess.PIPE,
            "stdout": subprocess.PIPE,
            "stderr": subprocess.STDOUT,
        }
        qemu_subprocess = self._qemu.start(subprocess_params)
        self._console = PipeConsole("QEMU", qemu_subprocess)
        return self

    def stop(self):
        logger.info("Stopping QEMU...")
        self._qemu.stop()

    def restart(self):
        self.stop()
        self.start()

    @property
    def console(self):
        return self._console
