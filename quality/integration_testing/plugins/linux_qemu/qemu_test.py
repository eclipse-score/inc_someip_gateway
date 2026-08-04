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

import unittest
import importlib.util
import os
from pathlib import Path
from subprocess import TimeoutExpired
from unittest.mock import patch


def _load_qemu_module():
    module_path = Path(__file__).with_name("qemu.py")
    spec = importlib.util.spec_from_file_location("linux_qemu_qemu", module_path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


DiskBootQemu = _load_qemu_module().DiskBootQemu


class DiskBootQemuTest(unittest.TestCase):
    def _new_qemu(self, seed_iso=None):
        with (
            patch.object(DiskBootQemu, "_check_qemu_is_installed"),
            patch.object(DiskBootQemu, "_find_available_kvm_support"),
            patch.object(DiskBootQemu, "_check_kvm_readable_when_necessary"),
        ):
            qemu = DiskBootQemu(
                path_to_image="/tmp/image.qcow2",
                seed_iso=seed_iso,
            )
        qemu._accelerator = "tcg"
        return qemu

    def test_build_command_adds_seed_iso_as_drive(self):
        qemu = self._new_qemu(seed_iso="/tmp/seed.iso")

        cmd = qemu._build_command()

        self.assertIn("-drive", cmd)
        self.assertIn("file=/tmp/seed.iso,format=raw,if=virtio,readonly=on", cmd)

    def test_build_command_without_seed_iso_omits_seed_drive(self):
        qemu = self._new_qemu(seed_iso=None)

        cmd = qemu._build_command()

        self.assertNotIn("file=/tmp/seed.iso,format=raw,if=virtio,readonly=on", cmd)

    def test_build_command_normalizes_relative_seed_iso_to_absolute(self):
        qemu = self._new_qemu(
            seed_iso="quality/integration_testing/environments/ubuntu24_04_qemu/seed.img"
        )

        cmd = qemu._build_command()

        expected = os.path.abspath(
            "quality/integration_testing/environments/ubuntu24_04_qemu/seed.img"
        )
        self.assertIn(f"file={expected},format=raw,if=virtio,readonly=on", cmd)

    def test_build_command_uses_valid_tcg_acceleration_args(self):
        qemu = self._new_qemu(seed_iso=None)
        qemu._accelerator = "tcg"

        cmd = qemu._build_command()

        self.assertIn("-accel", cmd)
        accel_pos = cmd.index("-accel")
        self.assertEqual(cmd[accel_pos + 1], "tcg")

    def test_build_command_uses_enable_kvm_flag(self):
        qemu = self._new_qemu(seed_iso=None)
        qemu._accelerator = "kvm"

        cmd = qemu._build_command()

        self.assertIn("-enable-kvm", cmd)

    def test_stop_handles_unstarted_process(self):
        qemu = self._new_qemu(seed_iso=None)

        qemu.stop()

    def test_stop_falls_back_to_kill_after_terminate_timeout(self):
        qemu = self._new_qemu(seed_iso=None)

        process = unittest.mock.MagicMock()
        process.poll.side_effect = [None, None, 0]
        process.wait.side_effect = [TimeoutExpired(cmd="qemu", timeout=2), 0]
        process.returncode = 0
        qemu._subprocess = process

        qemu.stop()

        process.terminate.assert_called_once()
        process.kill.assert_called_once()


if __name__ == "__main__":
    unittest.main()
