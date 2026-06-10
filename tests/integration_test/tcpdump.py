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
from util import (
    ShellProcess,
    _as_text,
    _tcpdump_capture,
    get_content_of_file_object,
    get_ps_aux_text,
)


def is_tcpdump_running() -> tuple[bool, str]:
    tcpdump_name = "/usr/bin/tcpdump"
    # do not know why on Github runners tcpdump shows up like that
    tcpdump_name_github = "[tcpdump]"
    ps_aux_text = get_ps_aux_text()

    return (
        tcpdump_name in ps_aux_text or tcpdump_name_github in ps_aux_text,
        ps_aux_text,
    )


def test_tcpdump_with_ping_from_host(target) -> None:
    with _tcpdump_capture("icmp", packet_count=2) as tcpdump_process:
        assert tcpdump_process.poll() is None, _as_text(tcpdump_process.stderr.read())

        # sanity check that tcpdump is running
        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert tcpdump_running, ps_aux_text

        exit_code, output = target.execute("ping -c 1 169.254.158.190")
        assert exit_code == 0, output.decode()

        # sanity check that tcpdump is running
        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert tcpdump_running, ps_aux_text

        exit_code, output = target.execute("ping -c 1 169.254.21.88")
        assert exit_code == 0, output.decode()

        # Now tcpdump should terminate with two captured packets
        tcpdump_process.wait(timeout=5.0)
        assert tcpdump_process.returncode == 0, _as_text(tcpdump_process.stderr.read())
        assert tcpdump_process.poll() is not None, (
            "tcpdump process should have exited by now: "
            + _as_text(tcpdump_process.stderr.read())
        )

        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert not tcpdump_running, ps_aux_text


def test_tcpdump_with_ping_from_target(target):
    with _tcpdump_capture("icmp", packet_count=2) as tcpdump_process:
        assert tcpdump_process.poll() is None, _as_text(tcpdump_process.stderr.read())

        # sanity check that tcpdump is running
        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert tcpdump_running, ps_aux_text

        with ShellProcess(
            target, "ping", ["-c", "1", "169.254.158.190"]
        ) as bash_process:
            while bash_process.is_running():
                time.sleep(0.1)
            assert bash_process.get_exit_code() == 0, bash_process.get_output().decode()

            # sanity check that tcpdump is running
            tcpdump_running, ps_aux_text = is_tcpdump_running()
            assert tcpdump_running, ps_aux_text

            with ShellProcess(
                target, "ping", ["-c", "1", "169.254.21.88"]
            ) as bash_process:
                while bash_process.is_running():
                    time.sleep(0.1)
                assert bash_process.get_exit_code() == 0, (
                    bash_process.get_output().decode()
                )

        # Now tcpdump should terminate with two captured packets
        tcpdump_process.wait(timeout=5.0)
        assert tcpdump_process.returncode == 0, _as_text(tcpdump_process.stderr.read())
        assert tcpdump_process.poll() is not None, (
            "tcpdump process should have exited by now: "
            + _as_text(tcpdump_process.stderr.read())
        )

        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert not tcpdump_running, ps_aux_text


def test_tcpdump_with_long_running_ping_from_target(target):
    with _tcpdump_capture("icmp", packet_count=5) as tcpdump_process:
        assert tcpdump_process.poll() is None, _as_text(tcpdump_process.stderr.read())

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
                    "final iteration"
                    + get_content_of_file_object(tcpdump_process.stdout)
                    + ", "
                    + get_content_of_file_object(tcpdump_process.stderr)
                )

                assert tcpdump_process.returncode == 0, get_content_of_file_object(
                    tcpdump_process.stderr
                )
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
        assert tcpdump_process.returncode == 0, get_content_of_file_object(
            tcpdump_process.stderr
        )
        assert tcpdump_process.poll() is not None, (
            "tcpdump process should have exited by now: "
            + get_content_of_file_object(tcpdump_process.stderr)
        )

        tcpdump_running, ps_aux_text = is_tcpdump_running()
        assert not tcpdump_running, ps_aux_text

    logging.getLogger().info(
        "Finished test_tcpdump_with_long_running_ping_from_target2"
    )


def test_killing_tcpdump(target):
    with _tcpdump_capture("icmp", packet_count=500) as tcpdump_process:
        assert tcpdump_process.poll() is None, _as_text(tcpdump_process.stderr.read())
        # killing tcpdump via exiting the with statement seems to work
        # What does not work is killing it via using any of these:
        # tcpdump_process.terminate()
        # tcpdump_process.kill()
        # subprocess.run(["pkill", "tcpdump"], check=True)
        # This raises an permission exception.

    tcpdump_running, ps_aux_text = is_tcpdump_running()
    assert not tcpdump_running, ps_aux_text
