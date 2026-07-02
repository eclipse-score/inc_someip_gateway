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


def test_start_someipd(clean_state):
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
        time.sleep(1)  # check that daemon does not crash immediately and prints output
        assert someipd_process.is_running(), (
            someipd_process.get_output(),
            "exit code: ",
            someipd_process.get_exit_code(),
        )
        logging.info("someipd output:\n%s", someipd_process.get_output())
        ps_aux_text = get_running_processes_on_target(clean_state)
        assert "someipd" in ps_aux_text, ps_aux_text

    ps_aux_text = get_running_processes_on_target(clean_state)
    assert "someipd" not in ps_aux_text, ps_aux_text
