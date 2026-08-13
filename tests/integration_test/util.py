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
import logging
import os
import subprocess
from score.itf.plugins.core import Target
from types import TracebackType
from score.itf.core.process.async_process import AsyncProcess

# Re-export capture helpers so existing callers can do:
#   from util import tcpdump_capture, get_output, wait_until_process_exits
from capture import as_text, get_output, tcpdump_capture, wait_until_process_exits


def _completed_process_as_text(process: subprocess.CompletedProcess) -> str:
    return (
        f"Command: {' '.join(process.args)}\n"
        f"Exit code: {process.returncode}\n"
        f"Stdout:\n{as_text(process.stdout)}\n"
        f"Stderr:\n{as_text(process.stderr)}"
    )


class ShellProcess:
    """Similar to WrappedProcess, but allows setting environment variables."""

    def __init__(
        self,
        target: Target,
        application_path: str,
        args: Sequence[str] | None = None,
        env: str = "",
    ):
        self._target = target
        self._application_path = application_path
        self._env = env
        self._args = list(args) if args is not None else []
        self._process: AsyncProcess | None = None

    def __enter__(self) -> AsyncProcess:
        args = " ".join(self._args)
        command = f"LD_LIBRARY_PATH=/ {self._env} exec {self._application_path} {args}"
        self._process = self._target.execute_async(command)

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
        if self._process is not None:
            self._process.stop()


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
