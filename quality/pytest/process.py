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

import os
import re
import select
import signal
import subprocess
import time
from collections.abc import Sequence
from os import PathLike


def _glob_to_regex(pattern: str) -> re.Pattern[str]:
    """Translate a glob pattern ('*' matches any text, including newlines) to a regex."""
    escaped = re.escape(pattern).replace(re.escape("*"), ".*")
    return re.compile(escaped, re.DOTALL)


def assert_process_creates_network_instance(
    command: Sequence[str | PathLike[str]],
    expected_output: str,
    *,
    timeout_seconds: float = 10,
    failure_prefix: str = "process did not produce expected output",
) -> None:
    process = subprocess.Popen(
        command,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        start_new_session=True,
    )
    try:
        wait_for_output(process, expected_output, timeout_seconds=timeout_seconds, failure_prefix=failure_prefix)
    finally:
        stop_process(process)


def wait_for_output(
    process: subprocess.Popen[bytes],
    expected_output: str,
    *,
    timeout_seconds: float = 10,
    failure_prefix: str = "process did not produce expected output",
) -> None:
    """Wait until the process' stdout matches `expected_output`, a glob pattern where '*' matches any text."""
    stdout_pipe = process.stdout
    if stdout_pipe is None:
        raise AssertionError("test failed to capture subprocess stdout")

    stdout_fd = stdout_pipe.fileno()
    deadline = time.monotonic() + timeout_seconds
    pattern = _glob_to_regex(expected_output)
    output_bytes = b""
    while True:
        remaining_seconds = deadline - time.monotonic()
        if remaining_seconds <= 0:
            break
        readable, _, _ = select.select([stdout_fd], [], [], remaining_seconds)
        if not readable:
            break
        chunk = os.read(stdout_fd, 4096)
        if not chunk:
            if process.poll() is not None:
                break
            continue
        output_bytes += chunk
        if pattern.search(output_bytes.decode("utf-8", errors="replace")):
            return

    output = output_bytes.decode("utf-8", errors="replace")
    raise AssertionError(f"{failure_prefix} '{expected_output}':\n{output}")


def stop_process(process: subprocess.Popen[bytes], *, wait_timeout_seconds: float = 10) -> None:
    if process.stdout is not None:
        process.stdout.close()
    if process.poll() is None:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    _ = process.wait(timeout=wait_timeout_seconds)
