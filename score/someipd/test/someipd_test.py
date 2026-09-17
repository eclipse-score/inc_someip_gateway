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

from pathlib import Path
import os
import subprocess
import sys
import time
import unittest

from quality.pytest.process import assert_process_creates_network_instance


class SomeipdTest(unittest.TestCase):
    def setUp(self) -> None:
        self.binary = Path(sys.argv[1])
        self.configuration = Path(sys.argv[2])

    def test_help_returns_success_and_prints_usage(self) -> None:
        result = subprocess.run([self.binary, "--help"], capture_output=True, check=False, text=True)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Syntax: someipd -h/--help", result.stdout)

    def test_creates_local_network_instance(self) -> None:
        self._assert_creates_network_instance("OFFER(0100): [0001.0001")

    def test_creates_remote_network_instance(self) -> None:
        self._assert_creates_network_instance("REQUEST(0100): [4321.5678")

    def _assert_creates_network_instance(self, expected_output: str) -> None:
        assert_process_creates_network_instance(
            [
                self.binary,
                "--configuration",
                self.configuration,
                "--ipc_channel",
                f"someipd-test-{os.getpid()}-{time.monotonic_ns()}",
            ],
            expected_output,
            failure_prefix="someipd did not create a network instance",
        )


if __name__ == "__main__":
    _ = unittest.main(argv=[sys.argv[0]])
