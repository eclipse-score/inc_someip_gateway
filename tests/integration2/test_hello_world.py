# *******************************************************************************
# Copyright (c) 2025 Contributors to the Eclipse Foundation
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
import subprocess
from collections.abc import Sequence
from types import TracebackType
from typing import Any
from someip.header import SOMEIPHeader


class BashProcess:
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
            "/bin/bash",
            args=[
                "-lc",
                f"LD_LIBRARY_PATH=/ {self._env} {self._application_path} {' '.join(self._args)}",
            ],
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


def test_hello_world_via_shell(target):
    exit_code, output = target.execute(
        "ls -lha / /usr /usr/lib /usr/lib/x86_64-linux-gnu"
    )
    assert 0 == exit_code
    assert b"someipd" in output
    # assert False, (
    #     "This test should not be executed, it is only for debugging purposes.\n"
    #     + output.decode()
    # )


def test_start_someipd(target):
    with BashProcess(
        target,
        "/someipd",
        args=[
            "--configuration",
            "/mw_someip_config.bin",
            "--service_instance_manifest",
            "/someipd_mw_com_config.json",
        ],
        env="VSOMEIP_CONFIGURATION=/vsomeip-local.json",
    ) as someipd_process:
        assert someipd_process.is_running(), someipd_process.get_output()


def test_start_gatewayd(target):
    with BashProcess(
        target,
        "/gatewayd",
        args=[
            "--configuration",
            "/mw_someip_config.bin",
            "--service_instance_manifest",
            "/gatewayd_mw_com_config.json",
        ],
    ) as gatewayd_process:
        assert gatewayd_process.is_running(), gatewayd_process.get_output()


# def test_location_of_pytest_environment(target):
#     command_outputs = []
#     for command in (["whoami"], ["pwd"], ["ls", "-la"], ["ps", "aux"]):
#         result = subprocess.run(command, check=True, capture_output=True, text=True)
#         command_outputs.append(f"$ {' '.join(command)}\n{result.stdout}")

#     environment_dump = "\n".join(
#         f"{key}={value}" for key, value in sorted(os.environ.items())
#     )
#     debug_output = (
#         "Pytest environment debug information:\n"
#         + "\n".join(command_outputs)
#         + "\nEnvironment variables:\n"
#         + environment_dump
#     )

#     print(debug_output, flush=True)
#     assert False, (
#         "This test should not be executed, it is only for debugging purposes.\n"
#         + debug_output
#     )
