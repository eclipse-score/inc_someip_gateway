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
    tcpdump_capture,
    is_tcpdump_running,
    get_output,
)


def test_tcpdump_with_ping_from_target_execute(target) -> None:
    with tcpdump_capture("icmp", packet_count=2) as tcpdump_process:
        assert tcpdump_process.poll() is None, get_output(tcpdump_process)

        # sanity check that tcpdump is running
        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert tcpdump_running, ps_aux_text

        # It looks like tcpdump is not always ready to capture packets immediately after starting
        for _ in range(5):
            exit_code, output = target.execute("ping -c 1 169.254.21.88")
            assert exit_code == 0, output.decode()

            # Now tcpdump should terminate with two captured packets
            try:
                tcpdump_process.wait(timeout=1.0)
                break
            except Exception as e:
                logging.getLogger().error(
                    "Exception occurred while waiting for tcpdump to terminate: "
                    + str(e)
                )
        assert tcpdump_process.returncode == 0, get_output(tcpdump_process)
        assert tcpdump_process.poll() is not None, (
            "tcpdump process should have exited by now: " + get_output(tcpdump_process)
        )

        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert not tcpdump_running, ps_aux_text


def test_tcpdump_with_ping_from_target(target):
    with tcpdump_capture("icmp", packet_count=2) as tcpdump_process:
        assert tcpdump_process.poll() is None, get_output(tcpdump_process)

        # sanity check that tcpdump is running
        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert tcpdump_running, ps_aux_text

        with ShellProcess(target, "ping", ["-c", "5", "169.254.21.88"]) as bash_process:
            # Now tcpdump should terminate with two captured packets
            tcpdump_process.wait(timeout=5.0)
        assert tcpdump_process.returncode == 0, get_output(tcpdump_process)
        assert tcpdump_process.poll() is not None, (
            "tcpdump process should have exited by now: " + get_output(tcpdump_process)
        )

        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert not tcpdump_running, ps_aux_text


def test_tcpdump_with_long_running_ping_from_target(target):
    with tcpdump_capture("icmp", packet_count=5) as tcpdump_process:
        assert tcpdump_process.poll() is None, get_output(tcpdump_process)

        # sanity check that tcpdump is running
        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert tcpdump_running, ps_aux_text

        try:
            with ShellProcess(target, "ping", ["169.254.21.88"]) as bash_process:
                logging.getLogger().info(
                    "Started ping process with PID: " + str(bash_process.pid())
                )
                # sanity check that tcpdump is running
                tcpdump_running, ps_aux_text = is_tcpdump_running()
                assert tcpdump_running, ps_aux_text
                while tcpdump_process.poll() is None:
                    time.sleep(0.1)

                logging.getLogger().info(
                    "final iteration" + get_output(tcpdump_process)
                )

                assert tcpdump_process.returncode == 0, get_output(tcpdump_process)
                assert bash_process.is_running(), (
                    "ping process should still be running after tcpdump has exited: "
                    + bash_process.get_output().decode()
                )
        except Exception as e:
            logging.getLogger().error(
                "Exception occurred during ping process: " + str(e)
            )
            raise

        logging.getLogger().info(
            "Exited ping process, now waiting for tcpdump to terminate with captured packets"
        )

        # Now tcpdump should terminate with two captured packets
        tcpdump_process.wait(timeout=5.0)
        assert tcpdump_process.returncode == 0, get_output(tcpdump_process)
        assert tcpdump_process.poll() is not None, (
            "tcpdump process should have exited by now: " + get_output(tcpdump_process)
        )

        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert not tcpdump_running, ps_aux_text

    logging.getLogger().info(
        "Finished test_tcpdump_with_long_running_ping_from_target2"
    )
