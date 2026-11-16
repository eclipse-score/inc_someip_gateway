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
import unittest

from attribute_plugin import add_test_properties

from quality.pytest.process import assert_process_creates_network_instance
from quality.pytest.process import stop_process
from quality.pytest.process import wait_for_output


class GatewaydTest(unittest.TestCase):
    gatewayd: Path
    someipd: Path
    configuration: Path
    service_instance_manifest: Path
    someipd_service_instance_manifest: Path

    @add_test_properties(
        fully_verifies=["comp_req__gatewayd__prints_usage"],
        test_type="interface-test",
        derivation_technique="design-analysis",
    )
    def test_help_returns_success_and_prints_usage(self) -> None:
        """Gatewayd prints its usage and exits successfully when called with --help."""
        result = subprocess.run([self.gatewayd, "--help"], capture_output=True, check=False, text=True)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Syntax: gatewayd -h/--help", result.stdout)

    @add_test_properties(
        partially_verifies=["comp_req__gatewayd__starts_from_config"],
        test_type="interface-test",
        derivation_technique="design-analysis",
    )
    def test_creates_local_network_instance(self) -> None:
        """Gatewayd creates a local bridge for a configured local service instance."""
        self._assert_creates_network_instance(
            "Creating local service instance: window_control*Gateway started, waiting for shutdown signal...",
            "OFFER(0100): [0001.0001",
        )

    @add_test_properties(
        partially_verifies=["comp_req__gatewayd__starts_from_config"],
        test_type="interface-test",
        derivation_technique="design-analysis",
    )
    def test_creates_remote_network_instance(self) -> None:
        """Gatewayd creates a remote bridge for a configured remote service instance."""
        self._assert_creates_network_instance(
            "Creating remote service instance: echo_response*Gateway started, waiting for shutdown signal...",
            "REQUEST(0100): [4321.5678",
        )

    def _assert_creates_network_instance(self, expected_gatewayd_output: str, expected_someipd_output: str) -> None:
        someipd_process = subprocess.Popen(
            [
                self.someipd,
                "--service_instance_manifest",
                self.someipd_service_instance_manifest,
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
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
    _ = unittest.main()
