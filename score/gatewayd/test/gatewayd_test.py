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
from quality.pytest.process import stop_process


class GatewaydTest(unittest.TestCase):
    def setUp(self) -> None:
        self.gatewayd = Path(sys.argv[1])
        self.someipd = Path(sys.argv[2])
        self.configuration = Path(sys.argv[3])
        self.service_instance_manifest = Path(sys.argv[4])

    def test_help_returns_success_and_prints_usage(self) -> None:
        result = subprocess.run([self.gatewayd, "--help"], capture_output=True, check=False, text=True)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Syntax: gatewayd -h/--help", result.stdout)

    def test_creates_local_network_instance(self) -> None:
        self._assert_creates_network_instance(
            "Creating local service instance: window_control*Gateway started, waiting for shutdown signal..."
        )

    def test_creates_remote_network_instance(self) -> None:
        self._assert_creates_network_instance(
            "Creating remote service instance: echo_response*Gateway started, waiting for shutdown signal..."
        )

    def _assert_creates_network_instance(self, expected_output: str) -> None:
        ipc_channel = f"gatewayd-test-{os.getpid()}-{time.monotonic_ns()}"
        someipd_process = subprocess.Popen(
            [
                self.someipd,
                "--configuration",
                self.configuration,
                "--ipc_channel",
                ipc_channel,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            assert_process_creates_network_instance(
                [
                    self.gatewayd,
                    "--configuration",
                    self.configuration,
                    "--service_instance_manifest",
                    self.service_instance_manifest,
                    "--ipc_channel",
                    ipc_channel,
                ],
                expected_output,
            )
        finally:
            stop_process(someipd_process)


if __name__ == "__main__":
    _ = unittest.main(argv=[sys.argv[0]])
