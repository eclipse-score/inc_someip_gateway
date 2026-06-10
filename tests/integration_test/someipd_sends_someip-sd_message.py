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

import logging
import subprocess
from util import (
    ShellProcess,
    tcpdump_capture,
    wait_until_process_exits,
    cleanup,
)


def test_start_someipd_and_gatewayd(target):
    cleanup(target)
    subprocess.run(["ip", "route", "add", "224.0.0.0/4", "dev", "tap0"], check=True)

    with tcpdump_capture("udp port 30490", packet_count=1) as tcpdump_process:
        with ShellProcess(
            target,
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
                console_output = wait_until_process_exits(tcpdump_process, timeout=10.0)
                logging.info(
                    "Final tcpdump to capture SOME/IP-SD traffic...\n" + console_output
                )

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
