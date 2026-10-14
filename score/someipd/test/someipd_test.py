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
import subprocess
import sys
import unittest

from quality.pytest.process import stop_process
from quality.pytest.process import wait_for_output


class SomeipdTest(unittest.TestCase):
    def setUp(self) -> None:
        self.binary = Path(sys.argv[1])
        self.configuration = Path(sys.argv[2])
        self.service_instance_manifest = Path(sys.argv[3])

    def test_help_returns_success_and_prints_usage(self) -> None:
        result = subprocess.run([self.binary, "--help"], capture_output=True, check=False, text=True)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Syntax: someipd -h/--help", result.stdout)

    def test_creates_local_network_instance(self) -> None:
        someipd_process = subprocess.Popen(
            [
                self.binary,
                "--configuration",
                self.configuration,
                "--service_instance_manifest",
                self.service_instance_manifest,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            wait_for_output(
                someipd_process,
                "OFFER(0100): [0001.0001",
                failure_prefix="someipd did not create a network instance",
            )
        finally:
            stop_process(someipd_process)

    def test_creates_remote_network_instance(self) -> None:
        someipd_process = subprocess.Popen(
            [
                self.binary,
                "--configuration",
                self.configuration,
                "--service_instance_manifest",
                self.service_instance_manifest,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            wait_for_output(
                someipd_process,
                "REQUEST(0100): [4321.5678",
                failure_prefix="someipd did not create a network instance",
            )
        finally:
            stop_process(someipd_process)


if __name__ == "__main__":
    _ = unittest.main(argv=[sys.argv[0]])
