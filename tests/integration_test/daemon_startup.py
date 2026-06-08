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
import time
import subprocess
from util import ShellProcess, _tcpdump_capture, wait_for_ip_traffic


def test_start_someipd(target):
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
        time.sleep(1)  # check that daemon does not crash immediately and prints output
        assert someipd_process.is_running(), (
            someipd_process.get_output(),
            "exit code: ",
            someipd_process.get_exit_code(),
        )


def test_start_gatewayd(target):
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
        time.sleep(1)  # check that daemon does not crash immediately and prints output
        assert gatewayd_process.is_running(), (
            gatewayd_process.get_output(),
            "exit code: ",
            gatewayd_process.get_exit_code(),
        )


def dtest_start_someipd_and_gatewayd(target):
    with _tcpdump_capture("udp port 30490", packet_count=1) as tcpdump_process:
        exit_code, output = target.execute("ping -c 1 169.254.158.190")
        assert exit_code == 0, output.decode()
        exit_code, output = target.execute("ping -c 1 169.254.21.88")
        assert exit_code == 0, output.decode()
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
            assert False, "someipd: is this reached?"  # TODO remove
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
                # assert False, "gatewayd: is this reached?"  # TODO remove
                sd_traffic_received, sd_capture_output = wait_for_ip_traffic(
                    tcpdump_process, timeout=20.0
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
                assert sd_traffic_received, (
                    "Did not observe SOME/IP-SD traffic on UDP port 30490.",
                    sd_capture_output,
                    "someipd output:",
                    someipd_process.get_output(),
                    "gatewayd output:",
                    gatewayd_process.get_output(),
                )
                assert False, "is this reached?"  # TODO remove
