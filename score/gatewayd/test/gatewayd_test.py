# *******************************************************************************
# Copyright (c) 2026 Contributors to the Eclipse Foundation
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License Version 2.0 which is available at
#
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************

from pathlib import Path
import os
import select
import signal
import subprocess
import sys
import time
import unittest


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
        self._assert_creates_network_instance("Creating local service instance: window_control")

    def test_creates_remote_network_instance(self) -> None:
        self._assert_creates_network_instance("Creating remote service instance: echo_response")

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
            text=True,
            start_new_session=True,
        )
        gatewayd_process: subprocess.Popen[str] | None = None
        try:
            gatewayd_process = subprocess.Popen(
                [
                    self.gatewayd,
                    "--configuration",
                    self.configuration,
                    "--service_instance_manifest",
                    self.service_instance_manifest,
                    "--ipc_channel",
                    ipc_channel,
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                start_new_session=True,
            )
            self._wait_for_output(gatewayd_process, expected_output)
        finally:
            if gatewayd_process is not None:
                self._stop_process(gatewayd_process)
            self._stop_process(someipd_process)

    def _wait_for_output(self, process: subprocess.Popen[str], expected_output: str) -> None:
        deadline = time.monotonic() + 10
        output = ""
        while time.monotonic() < deadline:
            readable, _, _ = select.select([process.stdout], [], [], deadline - time.monotonic())
            if not readable:
                break
            line = process.stdout.readline()
            if not line:
                if process.poll() is not None:
                    break
                continue
            output += line
            if expected_output in output:
                return
        self.fail(f"process did not produce expected output '{expected_output}':\n{output}")

    def _stop_process(self, process: subprocess.Popen[str]) -> None:
        if process.poll() is None:
            os.killpg(process.pid, signal.SIGKILL)
        process.wait(timeout=10)
        if process.stdout is not None:
            process.stdout.close()


if __name__ == "__main__":
    _ = unittest.main(argv=[sys.argv[0]])
