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

import logging
import os
import pytest
from typing import Generator
from util import ShellProcess, tcpdump_capture, check_environment_and_mark
from score.itf.plugins.core import Target


@pytest.fixture(scope="function")
def clean_state(target: Target) -> Generator[Target, None, None]:
    """This fixture can only be used once per QEMU instance / linux-sandbox instance, to ensure a clean environment.

    Otherwise subsequent tests might fail due to leftover state from previous tests.
    Additionally killing tcpdump is problematic (kill errors with permission denied), but terminating linux-sandbox should get rid of it.
    Lastly the output pcap file name should contain the test name, if we want to support multiple tests per run.
    """

    check_environment_and_mark(target)
    yield target


@pytest.fixture(scope="function")
def gatewayd_with_someipd(clean_state: Target) -> Generator[Target, None, None]:
    """Start someipd and gatewayd before tests and stop them after."""

    # Store test traffic in file for later analysis of failures.
    # tcpdump is kept alive for the entire fixture scope; the sandbox tears it
    # down on exit so we do not call stop_capture here.
    pcap_dir = os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR", ".")
    pcap_file = os.path.join(pcap_dir, "test_traffic.pcap")

    try:
        tcpdump_host = tcpdump_capture("", output_file=pcap_file)
        tcpdump_host.__enter__()
    except RuntimeError:
        logging.warning("tcpdump could not start; pcap capture skipped")

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
