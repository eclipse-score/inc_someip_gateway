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
import signal
import subprocess
import unittest

from attribute_plugin import add_test_properties

from quality.pytest.process import stop_process, wait_for_output


class SomeipdTest(unittest.TestCase):
    binary: Path
    service_instance_manifest: Path

    @add_test_properties(
        fully_verifies=["comp_req__someipd__prints_usage"],
        test_type="interface-test",
        derivation_technique="design-analysis",
    )
    def test_help_returns_success_and_prints_usage(self) -> None:
        """Someipd prints its usage and exits successfully when called with --help."""
        result = subprocess.run([self.binary, "--help"], capture_output=True, check=False, text=True)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Syntax: someipd -h/--help", result.stdout)

    @add_test_properties(
        fully_verifies=["comp_req__someipd__exits_on_signal"],
        test_type="interface-test",
        derivation_technique="design-analysis",
    )
    def test_sigterm_and_sigint_exit_with_success(self) -> None:
        """Someipd exits with success within 5 s after receiving SIGTERM or SIGINT."""
        for shutdown_signal in (signal.SIGTERM, signal.SIGINT):
            with self.subTest(signal=shutdown_signal.name):
                process = subprocess.Popen(
                    [
                        self.binary,
                        "--service_instance_manifest",
                        self.service_instance_manifest,
                    ],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    start_new_session=True,
                )
                try:
                    wait_for_output(
                        process, "SomeipdService offered", failure_prefix="someipd did not offer SomeipdService"
                    )
                    process.send_signal(shutdown_signal)
                    try:
                        returncode = process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        self.fail(f"someipd still running 5 s after {shutdown_signal.name}")
                    self.assertEqual(returncode, 0)
                finally:
                    stop_process(process)


if __name__ == "__main__":
    _ = unittest.main()
