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


"""
Pytest configuration and fixtures for integration tests.
"""

import pytest
from typing import Generator
from util import ShellProcess, cleanup
from score.itf.plugins.core import Target


@pytest.fixture(scope="function")
def clean_state(target: Target) -> Generator[Target, None, None]:
    cleanup(target)
    yield target


@pytest.fixture(scope="function")
def gatewayd_with_someipd(clean_state: Target) -> Generator[Target, None, None]:
    with ShellProcess(
        clean_state,
        "/someipd",
        args=[
            "--configuration",
            "/mw_someip_config.bin",
            "--service_instance_manifest",
            "/someipd_mw_com_config.json",
        ],
        env="VSOMEIP_CONFIGURATION=/vsomeip.json",
    ) as someipd_process:
        assert someipd_process.is_running(), someipd_process.get_output()
        with ShellProcess(
            clean_state,
            "/gatewayd",
            args=[
                "--configuration",
                "/mw_someip_config.bin",
                "--service_instance_manifest",
                "/gatewayd_mw_com_config.json",
            ],
        ) as gatewayd_process:
            assert gatewayd_process.is_running(), gatewayd_process.get_output()
            assert gatewayd_process.is_running(), (
                gatewayd_process.get_output(),
                "exit code: ",
                gatewayd_process.get_exit_code(),
            )
            assert someipd_process.is_running(), (
                someipd_process.get_output(),
                "exit code: ",
                someipd_process.get_exit_code(),
            )
            yield clean_state
