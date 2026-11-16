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
import unittest

from quality.pytest.process import assert_process_creates_network_instance
from quality.pytest.process import stop_process
from quality.pytest.process import wait_for_output


class GatewaydOpensomeipTest(unittest.TestCase):
    def setUp(self) -> None:
        self.gatewayd = Path(sys.argv[1])
        self.someipd = Path(sys.argv[2])
        self.configuration = Path(sys.argv[3])
        self.service_instance_manifest = Path(sys.argv[4])
        self.someipd_service_instance_manifest = Path(sys.argv[5])
        self.log_config = Path(sys.argv[6])

    def test_help_returns_success_and_prints_usage(self) -> None:
        result = subprocess.run([self.gatewayd, "--help"], capture_output=True, check=False, text=True)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Syntax: gatewayd -h/--help", result.stdout)

    def test_creates_local_network_instance(self) -> None:
        # window_control is service 1, instance 1. mw::log inserts a space between streamed
        # fragments, so the offer line is "0x 1" and "at <ip>:<port> udp".
        self._assert_creates_network_instance(
            "Creating local service instance: window_control*Gateway started, waiting for shutdown signal...",
            "[someipd] Offering service 0x 1  instance 0x 1 *at*udp",
        )

    def test_creates_remote_network_instance(self) -> None:
        # echo_response is service 0x4321 instance 0x5678. The same session also offers the local
        # services, and that offer line includes the endpoint and "udp".
        self._assert_creates_network_instance(
            "Creating remote service instance: echo_response*Gateway started, waiting for shutdown signal...",
            "[someipd] Creating RemoteNetworkService:*echo_response*0x 4321*0x 5678*[someipd] Offering service*at*udp",
        )

    def _assert_creates_network_instance(self, expected_gatewayd_output: str, expected_someipd_output: str) -> None:
        # The console recorder drops info records at its default kWarn threshold. The offer and
        # remote-service lines are LogInfo, so this process is pointed at a config that prints them.
        log_config = self.log_config if self.log_config.is_absolute() else Path.cwd() / self.log_config
        env = os.environ.copy()
        env["MW_LOG_CONFIG_FILE"] = str(log_config)
        someipd_process = subprocess.Popen(
            [
                self.someipd,
                "--service_instance_manifest",
                self.someipd_service_instance_manifest,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
            env=env,
        )
        try:
            wait_for_output(
                someipd_process, "SomeipdService offered", failure_prefix="someipd did not offer SomeipdService"
            )
            assert_process_creates_network_instance(
                [
                    self.gatewayd,
                    "--configuration",
                    self.configuration,
                    "--service_instance_manifest",
                    self.service_instance_manifest,
                ],
                expected_gatewayd_output,
                failure_prefix="gatewayd did not initialize successfully",
            )
            wait_for_output(
                someipd_process,
                expected_someipd_output,
                failure_prefix="someipd did not create a network instance",
            )
        finally:
            stop_process(someipd_process)


if __name__ == "__main__":
    _ = unittest.main(argv=[sys.argv[0]])
