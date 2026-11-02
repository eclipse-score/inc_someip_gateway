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

import logging
import time
from util import ShellProcess, get_running_processes_on_target

# vsomeip log of someipd subscribing to echo_response, a remote service in the gatewayd configuration.
ECHO_RESPONSE_SUBSCRIPTION = "[4321.5678.8778:ffff:1]"


def test_start_someipd(clean_state):
    with ShellProcess(
        clean_state,
        "/opt/someipd",
        args=[
            "--service_instance_manifest",
            "/opt/someipd_mw_com_config.json",
        ],
        env="VSOMEIP_CONFIGURATION=/opt/vsomeip.json",
    ) as someipd_process:
        assert someipd_process.is_running(), someipd_process.get_output()
        time.sleep(1)  # check that daemon does not crash immediately and prints output
        assert someipd_process.is_running(), (
            someipd_process.get_output(),
            "exit code: ",
            someipd_process.get_exit_code(),
        )
        ps_aux_text = get_running_processes_on_target(clean_state)
        assert "someipd" in ps_aux_text, ps_aux_text

        with ShellProcess(
            clean_state,
            "/opt/gatewayd",
            args=[
                "--configuration",
                "/opt/mw_someip_config.bin",
                "--service_instance_manifest",
                "/opt/gatewayd_mw_com_config.json",
            ],
        ) as gatewayd_process:
            assert gatewayd_process.is_running(), gatewayd_process.get_output()
            deadline = time.monotonic() + 10.0
            while ECHO_RESPONSE_SUBSCRIPTION not in someipd_process.get_output() and time.monotonic() < deadline:
                time.sleep(0.1)
            output = someipd_process.get_output()
            logging.info("someipd output:\n%s", output)
            assert ECHO_RESPONSE_SUBSCRIPTION in output, output
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

    ps_aux_text = get_running_processes_on_target(clean_state)
    assert "someipd" not in ps_aux_text, ps_aux_text
