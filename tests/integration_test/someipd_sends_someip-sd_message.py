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
from util import (
    tcpdump_capture,
    wait_until_process_exits,
)
from score.itf.plugins.core import Target


def test_start_someipd_and_gatewayd(gatewayd_with_someipd: Target) -> None:
    """Same as above but with more complex test fixture"""
    with tcpdump_capture("udp port 30490", packet_count=1) as tcpdump_process:
        console_output = wait_until_process_exits(tcpdump_process, timeout=10.0)
        logging.info(
            "Final tcpdump to capture SOME/IP-SD traffic...\n" + console_output
        )
