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
"""QEMU launcher with disk boot support.

Provides a standalone QEMU class that supports booting from a qcow2 disk image
with an optional cloud-init seed ISO.  This avoids patching the upstream
score_itf Qemu class whose private methods use Python name mangling.
"""

import logging
import os
import platform
import subprocess
import sys
from subprocess import TimeoutExpired

logger = logging.getLogger(__name__)

_SUPPORTED_ARCHITECTURES = {
    "x86_64": {
        "qemu_path": "/usr/bin/qemu-system-x86_64",
        "cpu": "Cascadelake-Server-v5",
        "network_device": "virtio-net-pci",
        # The default pc (i440fx) machine is used.
        # Other machines change the PCI topology, which can break guests whose
        # PCI interrupt config targets i440fx like QNX.
        "machine": "pc",
        "block_device": "virtio-blk-pci",
    },
    "aarch64": {
        "qemu_path": "/usr/bin/qemu-system-aarch64",
        "cpu": "cortex-a53",
        "network_device": "virtio-net-device",
        "machine": "virt,virtualization=true,gic-version=3",
        "block_device": "virtio-blk-device",
    },
}


def get_image_type(path_to_image: str) -> str:
    """Determine the disk image type based on the file extension."""
    image_lower = path_to_image.lower()
    if image_lower.endswith(".wic") or image_lower.endswith(".img"):
        return "raw"
    return "qcow2"


class DiskBootQemu:
    """QEMU instance that boots from a qcow2 disk image.

    Supports an optional cloud-init seed ISO for automated guest provisioning.
    Port forwarding is used for host-to-guest networking (user-mode networking).
    """

    def __init__(
        self,
        path_to_image: str,
        ram="1G",
        cores="2",
        seed_iso=None,
        architecture="x86_64",
        path_to_kernel=None,
        kernel_cmdline=None,
        network_adapters=None,
        port_forwarding=None,
    ):
        if architecture not in _SUPPORTED_ARCHITECTURES:
            raise ValueError(
                "architecture must be one of: "
                + ", ".join(sorted(_SUPPORTED_ARCHITECTURES))
            )

        self._architecture = architecture
        self._host_architecture = self._normalize_architecture(platform.machine())
        self._qemu_path = _SUPPORTED_ARCHITECTURES[architecture]["qemu_path"]
        self._path_to_image = path_to_image
        self._path_to_kernel = path_to_kernel
        self._ram = ram
        self._cores = cores
        self._seed_iso = seed_iso
        self._cpu = _SUPPORTED_ARCHITECTURES[architecture]["cpu"]
        self._kernel_cmdline = kernel_cmdline
        self._network_device = _SUPPORTED_ARCHITECTURES[architecture]["network_device"]
        self._machine = _SUPPORTED_ARCHITECTURES[architecture]["machine"]
        self._block_device = _SUPPORTED_ARCHITECTURES[architecture]["block_device"]
        self._disk_format = get_image_type(path_to_image)
        self._use_kernel_boot = path_to_kernel is not None
        self._network_adapters = network_adapters or []
        self._port_forwarding = port_forwarding or []

        self._check_qemu_is_installed()

        self._subprocess = None

    def start(self):
        cmd = self._build_command()
        logger.debug(cmd)

        self._subprocess = subprocess.Popen(
            args=cmd,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        return self._subprocess

    def stop(self):
        if self._subprocess is None:
            return

        if self._subprocess.poll() is None:
            self._subprocess.terminate()
            try:
                self._subprocess.wait(2)
            except TimeoutExpired:
                logger.warning("QEMU did not terminate in time. Killing process.")
        if self._subprocess.poll() is None:
            self._subprocess.kill()
            try:
                self._subprocess.wait(2)
            except TimeoutExpired:
                logger.error("QEMU did not exit after kill.")
        ret = self._subprocess.returncode
        if ret != 0:
            raise Exception(f"QEMU process returned: {ret}")

    def _check_qemu_is_installed(self):
        if not os.path.isfile(self._qemu_path):
            logger.fatal(f"QEMU is not installed under {self._qemu_path}")
            sys.exit(-1)

    @staticmethod
    def _normalize_architecture(machine):
        normalized = machine.lower()
        if normalized == "amd64":
            return "x86_64"
        if normalized == "arm64":
            return "aarch64"
        return normalized

    def _build_command(self):
        image_path = os.path.abspath(self._path_to_image)
        cmd = (
            [
                self._qemu_path,
                "-smp",
                f"{self._cores},maxcpus={self._cores},cores={self._cores}",
                "-cpu",
                self._cpu,
                "-m",
                self._ram,
                "-accel",
                "kvm",
                "-accel",
                "tcg",
                "-machine",
                self._machine,
            ]
            + self._network_devices_args()
            + self._port_forwarding_args()
        )

        if self._use_kernel_boot:
            kernel_path = os.path.abspath(self._path_to_kernel)
            cmd.extend(["-kernel", kernel_path])
            if self._kernel_cmdline:
                cmd.extend(
                    [
                        "-append",
                        self._kernel_cmdline,
                    ]
                )
            cmd.extend(
                [
                    "-device",
                    f"{self._block_device},drive=vd0",
                    "-drive",
                    f"if=none,format={self._disk_format},file={image_path},id=vd0",
                ]
            )
        else:
            cmd.extend(
                [
                    "-drive",
                    f"file={image_path},format={self._disk_format},if=virtio",
                ]
            )

        if self._seed_iso:
            seed_path = os.path.abspath(self._seed_iso)
            cmd.extend(
                [
                    # Attach NoCloud seed as a second disk. With cloud-localds this is a
                    # vfat image labeled 'cidata', which cloud-init detects reliably.
                    "-drive",
                    # Bazel runfiles are read-only; mounting the seed image as read-only
                    # prevents permission errors when QEMU opens the backing file.
                    f"file={seed_path},format=raw,if=virtio,readonly=on",
                ]
            )

        cmd.extend(
            [
                "-nographic",
                "-serial",
                "mon:stdio",  # Redirect serial output to console
                "-object",
                "rng-random,filename=/dev/urandom,id=rng0",  # Provide hardware random number generation
                "-device",
                "virtio-rng-pci,rng=rng0",  # Provide hardware random number generation
            ]
        )

        return cmd

    def _network_devices_args(self):
        result = []
        for idx, adapter in enumerate(self._network_adapters, start=1):
            if not adapter.startswith("lo"):
                result.extend(
                    [
                        "-netdev",
                        f"tap,id=t{idx},ifname={adapter},script=no,downscript=no",
                        "-device",
                        f"{self._network_device},netdev=t{idx},id=nic{idx},guest_csum=off",
                    ]
                )
        return result

    def _port_forwarding_args(self):
        result = []
        for idx, forwarding in enumerate(self._port_forwarding, start=1):
            result.extend(
                [
                    "-netdev",
                    f"user,id=net{idx},hostfwd=tcp::{forwarding.host_port}-:{forwarding.guest_port}",
                    "-device",
                    f"{self._network_device},netdev=net{idx}",
                ]
            )
        return result
