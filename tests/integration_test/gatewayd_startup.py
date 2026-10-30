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
from score_pytest.attribute_plugin import add_test_properties
from util import (
    ShellProcess,
    get_running_processes_on_target,
)


@add_test_properties(
    fully_verifies=["comp_req__gatewayd__starts_from_config"],
    test_type="requirements-based",
    derivation_technique="requirements-analysis",
)
def test_start_gatewayd(clean_state):
    """Gatewayd starts with runtime configuration and stays active until shutdown."""
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
        time.sleep(1)  # check that daemon does not crash immediately and prints output
        assert gatewayd_process.is_running(), (
            gatewayd_process.get_output(),
            "exit code: ",
            gatewayd_process.get_exit_code(),
        )
        logging.info("gatewayd output:\n%s", gatewayd_process.get_output())
        ps_aux_text = get_running_processes_on_target(clean_state)
        assert "gatewayd" in ps_aux_text, ps_aux_text

    ps_aux_text = get_running_processes_on_target(clean_state)
    assert "gatewayd" not in ps_aux_text, ps_aux_text
