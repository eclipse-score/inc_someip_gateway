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
import logging
import time
import subprocess
from score.itf.plugins.core import Target
from types import TracebackType
from typing import Any


def kill_process_by_name(
    target: Target, application_path: str, timeout: float = 2.0
) -> bool:
    # check if pkill is available on the target
    exit_code, _ = target.execute("command -v pkill")
    if exit_code != 0:
        # pkill is not available in a Docker environment, where normal killing works
        return True

    # get name of process
    process_name = Path(application_path).name

    logging.info(f"Attempting to kill process '{process_name}' on target using pkill")

    for signal in ["", "-SIGTERM", "-SIGINT", "-SIGKILL"]:
        _, _ = target.execute(f"pkill {signal} {process_name}")

        # Avoid costly repeated SSH roundtrips in teardown; a short settle delay is sufficient.
        time.sleep(min(timeout, 0.2))

        exit_code, _ = target.execute(f"pgrep -x {process_name}")
        if exit_code != 0:
            return True  # Process has been terminated

    logging.warning(
        f"Failed to kill process '{process_name}' on target after sending SIGTERM, SIGINT, and SIGKILL"
    )

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


def _get_content_of_file_object(file_object: io.BufferedReader | None) -> str:
    if file_object is None:
        return ""

    # enable non blocking io
    os.set_blocking(file_object.fileno(), False)

    # Read and discard any buffered content to get the latest output
    data = file_object.read()
    if data is None:
        return ""
    return data.decode(errors="replace")


def get_output(process: subprocess.Popen[bytes]) -> str:
    return (
        _get_content_of_file_object(process.stdout)
        + "\n, stderr: "
        + _get_content_of_file_object(process.stderr)
    )


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
            assert not self._process.is_running(), (
                f"Process {self._application_path} should have been stopped, but is still running. Output: {self.get_output()}"
            )


def tcpdump_capture(
    filter_expression: str,
    packet_count: int | None = None,
    output_file: str | None = None,
) -> subprocess.Popen[bytes]:
    tcpdump_user = pwd.getpwuid(os.getuid()).pw_name
    args = [
        "/usr/bin/tcpdump",
        "-n",
        "-i",
        "any",
        "-Z",
        tcpdump_user,
    ]
    if output_file is not None:
        args.extend(["-w", output_file])
    else:
        # -l: line-buffered output, only meaningful for text (non-pcap) mode
        args.append("-l")
    # TODO tcpdump cannot be killed, thus at the moment only packet_count can be used to stop it
    #      When testing it using `linux-sandbox -R -N -- /bin/bash -lc 'tcpdump -n -l -Z root -i any'`
    #      and `sudo nsenter -t $(pidof tcpdump) -a killall tcpdump` it is killable in the second shell
    if packet_count is not None:
        args.extend(["-c", str(packet_count)])
    if filter_expression:
        args.append(filter_expression)

    return subprocess.Popen(
        args,
        stdout=subprocess.PIPE if output_file is None else subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )


def wait_until_process_exits(
    process: subprocess.Popen[bytes], timeout: float = 10.0
) -> str:
    start_time = time.time()
    while time.time() - start_time < timeout:
        if process.poll() is not None:
            return get_output(process)
        time.sleep(0.5)
    raise TimeoutError(
        f"Process did not exit within {timeout} seconds. Last output: {get_output(process)}"
    )


def get_running_processes_on_host() -> str:
    ps_aux_result = subprocess.run(
        ["ps", "aux"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
    )
    assert ps_aux_result.returncode == 0
    return _completed_process_as_text(ps_aux_result)


def get_running_processes_on_target(target) -> str:
    exit_code, output = target.execute("ps aux || pidin")
    assert exit_code == 0, output.decode()
    return output.decode()


def is_tcpdump_running() -> tuple[bool, str]:
    tcpdump_name = "/usr/bin/tcpdump"
    # do not know why on Github runners tcpdump shows up like that
    tcpdump_name_github = "[tcpdump]"
    ps_aux_text = get_running_processes_on_host()

    return (
        tcpdump_name in ps_aux_text or tcpdump_name_github in ps_aux_text,
        ps_aux_text,
    )


def check_environment_and_mark(target: Target) -> bool:
    """Check that environment is clean and place marker to fail if not."""
    marker_name = "/tmp/inc_someip_gateway_test_run"
    message = "Please ensure that there is only one test per file and one file per bazel label."

    # Check for marker files
    host_result = subprocess.run(
        ["ls", marker_name], check=False, stdout=subprocess.PIPE
    )
    assert host_result.returncode != 0, (
        f"Marker file {marker_name} exists on host, environment is not clean. {message}"
    )

    return_code, output = target.execute(f"ls {marker_name}")
    assert return_code != 0, (
        f"Marker file {marker_name} exists on target, environment is not clean. {message}"
    )

    # Check for running processes
    ps_aux_text = get_running_processes_on_target(target)
    assert "someipd" not in ps_aux_text, "Found stale someipd running: " + ps_aux_text
    assert "gatewayd" not in ps_aux_text, "Found stale gatewayd running: " + ps_aux_text

    # Check for lola files Linux + QNX
    for pattern in ["/dev/shm/lola-*", "/tmp/mw_com_lola/*", "/tmp/lola-*"] + [
        "/dev/shmem/lola-*",
        "/tmp_discovery/mw_com_lola/*",
        "/tmp_discovery/lola-*",
    ]:
        return_code, output = target.execute(f"ls {pattern}")
        assert return_code != 0, (
            f"Found stale lola files in {pattern} on target: " + output.decode()
        )

    # Check for tcpdump running on host
    is_running, ps_aux_text = is_tcpdump_running()
    assert not is_running, "Found stale tcpdump running on host: " + ps_aux_text

    # create marker file to mark that the environment is now dirty
    subprocess.run(["touch", marker_name], check=True)
    return_code, output = target.execute(f"touch {marker_name}")
    assert return_code == 0, (
        f"Failed to create marker file {marker_name} on target. Output: {output.decode()}"
    )
