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

from collections.abc import Sequence
import io
import os
from pathlib import Path
import pwd
import re
import logging
import select
import time
import subprocess
from types import TracebackType
from typing import IO, Any


def kill_process_by_name(target, application_path: str, timeout: float = 2.0) -> bool:
    # check if pkill is available on the target
    exit_code, _ = target.execute("command -v pkill")
    if exit_code != 0:
        # pkill is not available in a Docker environment, where normal killing works
        return True

    # get name of process
    process_name = Path(application_path).name
    for signal in ["SIGTERM", "SIGINT", "SIGKILL"]:
        _, _ = target.execute(f"pkill -{signal} {process_name}")

        # Avoid costly repeated SSH roundtrips in teardown; a short settle delay is sufficient.
        time.sleep(min(timeout, 0.2))

        exit_code, _ = target.execute(f"pgrep -x {process_name}")
        if exit_code != 0:
            return True  # Process has been terminated

    return False


def _as_text(output: Any) -> str:
    if isinstance(output, bytes):
        return output.decode(errors="replace")
    return str(output)


def _completed_process_as_text(process: subprocess.CompletedProcess) -> str:
    return (
        f"Command: {' '.join(process.args)}\n"
        f"Exit code: {process.returncode}\n"
        f"Stdout:\n{_as_text(process.stdout)}\n"
        f"Stderr:\n{_as_text(process.stderr)}"
    )


def get_content_of_file_object(file_object: io.BufferedReader | None) -> str:
    if file_object is None:
        return ""

    # enable non blocking io
    os.set_blocking(file_object.fileno(), False)

    # Read and discard any buffered content to get the latest output
    data = file_object.read()
    if data is None:
        return ""
    return data.decode(errors="replace")


class ShellProcess:
    def __init__(
        self,
        target: Any,
        application_path: str,
        args: Sequence[str] | None = None,
        env: str = "",
    ):
        self._target = target
        self._application_path = application_path
        self._env = env
        self._args = list(args) if args is not None else []
        self._process: Any = None

    def __enter__(self) -> Any:
        self._process = self._target.execute_async(
            f"LD_LIBRARY_PATH=/ {self._env} {self._application_path} {' '.join(self._args)}"
        )

        logging.getLogger().info(
            f"Started process {self._application_path} with PID: {self._process.pid()}"
        )

        return self._process

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ) -> None:
        if self._process is not None and self._process.is_running():
            # stop() on the shell process does not work in QEMU,
            # but killing the process spawned by the shell command works.
            # Not clear why the shell process itself is not killable
            kill_process_by_name(self._target, self._application_path)
            self._process.stop()


# TODO tcpdump does not capture anything here, the test is broken
def _tcpdump_capture(
    filter_expression: str, packet_count: int | None = None
) -> subprocess.Popen[bytes]:
    tcpdump_user = pwd.getpwuid(os.getuid()).pw_name
    args = [
        "/usr/bin/tcpdump",
        "-n",
        "-l",
        "-i",
        "any",
        "-Z",
        tcpdump_user,
    ]
    # TODO tcpdump cannot be killed, thus at the moment only packet_count can be used to stop it
    #      When testing it using `linux-sandbox -R -N -- /bin/bash -lc 'tcpdump -n -l -Z root -i any'`
    #      and `sudo nsenter -t $(pidof tcpdump) -a killall tcpdump` it is killable in the second shell
    if packet_count is not None:
        args.extend(["-c", str(packet_count)])
    if filter_expression:
        args.append(filter_expression)

    return subprocess.Popen(
        # Keep tcpdump under the current test user so teardown signals are permitted.
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def _has_captured_something(capture_text: str) -> bool:
    packet_count_match = re.search(
        r"(\d+) packets? received by filter", capture_text, re.IGNORECASE
    )
    return bool(packet_count_match and int(packet_count_match.group(1)) > 0)


def wait_for_ip_traffic(
    capture_process: Any, timeout: float = 20.0
) -> tuple[bool, str]:
    if capture_process.stdout is None or capture_process.stderr is None:
        return False, "tcpdump pipes are not available"

    capture_text = ""
    deadline = time.monotonic() + timeout
    stdout_fd = capture_process.stdout.fileno()

    def _stop_capture_process() -> None:
        if capture_process.poll() is not None:
            return
        capture_process.terminate()
        try:
            capture_process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            capture_process.kill()
            capture_process.wait(timeout=1.0)

    while time.monotonic() < deadline:
        remaining_time = max(0.0, deadline - time.monotonic())
        wait_time = min(0.1, remaining_time)
        readable, _, _ = select.select([stdout_fd], [], [], wait_time)

        if readable:
            chunk = os.read(stdout_fd, 4096)
            if chunk:
                capture_text += chunk.decode(errors="replace")
                if _has_captured_something(capture_text):
                    _stop_capture_process()
                    return True, "line 111 " + capture_text

        if capture_process.poll() is not None:
            stderr_text = _as_text(capture_process.stderr.read())
            return (
                _has_captured_something(capture_text),
                "line 114 "
                + capture_text
                + "\ntcpdump process has exited unexpectedly with code "
                + str(capture_process.returncode)
                + "\n"
                + stderr_text,
            )

    _stop_capture_process()

    # Drain any remaining buffered stdout after termination without blocking.
    while True:
        readable, _, _ = select.select([stdout_fd], [], [], 0.0)
        if not readable:
            break
        chunk = os.read(stdout_fd, 4096)
        if not chunk:
            break
        capture_text += chunk.decode(errors="replace")

    if _has_captured_something(capture_text):
        return True, "line 122 " + capture_text

    stderr_text = _as_text(capture_process.stderr.read())
    return (
        False,
        "tcpdump did not capture IP traffic within timeout.\n"
        + capture_text
        + ("\n" + stderr_text if stderr_text else ""),
    )
