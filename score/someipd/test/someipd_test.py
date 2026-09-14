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
        process = subprocess.Popen(
            [
                self.binary,
                "--configuration",
                self.configuration,
                "--ipc_channel",
                f"someipd-test-{os.getpid()}-{time.monotonic_ns()}",
            ],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        try:
            stdout_pipe = process.stdout
            if stdout_pipe is None:
                self.fail("someipd test failed to capture subprocess stdout")
            stdout_fd = stdout_pipe.fileno()
            deadline = time.monotonic() + 10
            expected_output_bytes = expected_output.encode("utf-8")
            output_bytes = b""
            while time.monotonic() < deadline:
                readable, _, _ = select.select([stdout_fd], [], [], deadline - time.monotonic())
                if not readable:
                    break
                chunk = os.read(stdout_fd, 4096)
                if not chunk:
                    if process.poll() is not None:
                        break
                    continue
                output_bytes += chunk
                if expected_output_bytes in output_bytes:
                    return
            output = output_bytes.decode("utf-8", errors="replace")
            self.fail(f"someipd did not create a network instance:\n{output}")
        finally:
            if process.stdout is not None:
                process.stdout.close()
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            _ = process.wait(timeout=10)


if __name__ == "__main__":
    _ = unittest.main(argv=[sys.argv[0]])
