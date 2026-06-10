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
from util import (
    ShellProcess,
    get_running_processes_on_target,
    cleanup,
)


def test_start_someipd(target):
    cleanup(target)
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
        logging.info("someipd output:\n%s", someipd_process.get_output())
        ps_aux_text = get_running_processes_on_target(target)
        assert "someipd" in ps_aux_text, ps_aux_text

    ps_aux_text = get_running_processes_on_target(target)
    assert "someipd" not in ps_aux_text, ps_aux_text


def test_start_gatewayd(target):
    cleanup(target)
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
        logging.info("gatewayd output:\n%s", gatewayd_process.get_output())
        ps_aux_text = get_running_processes_on_target(target)
        assert "gatewayd" in ps_aux_text, ps_aux_text

    ps_aux_text = get_running_processes_on_target(target)
    assert "gatewayd" not in ps_aux_text, ps_aux_text
