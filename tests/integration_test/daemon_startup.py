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

from collections.abc import Sequence
import io
import os
from pathlib import Path
import pwd
import re
import logging
import select
import time
import subprocess
from types import TracebackType
from typing import IO, Any


def kill_process_by_name(target, application_path: str, timeout: float = 2.0) -> bool:
    # check if pkill is available on the target
    exit_code, _ = target.execute("command -v pkill")
    if exit_code != 0:
        # pkill is not available in a Docker environment, where normal killing works
        return True

    # get name of process
    process_name = Path(application_path).name
    for signal in ["SIGTERM", "SIGINT", "SIGKILL"]:
        _, _ = target.execute(f"pkill -{signal} {process_name}")

        # Avoid costly repeated SSH roundtrips in teardown; a short settle delay is sufficient.
        time.sleep(min(timeout, 0.2))

        exit_code, _ = target.execute(f"pgrep -x {process_name}")
        if exit_code != 0:
            return True  # Process has been terminated

    return False


def _as_text(output: Any) -> str:
    if isinstance(output, bytes):
        return output.decode(errors="replace")
    return str(output)


def _completed_process_as_text(process: subprocess.CompletedProcess) -> str:
    return (
        f"Command: {' '.join(process.args)}\n"
        f"Exit code: {process.returncode}\n"
        f"Stdout:\n{_as_text(process.stdout)}\n"
        f"Stderr:\n{_as_text(process.stderr)}"
    )


def get_content_of_file_object(file_object: io.BufferedReader | None) -> str:
    if file_object is None:
        return ""

    # enable non blocking io
    os.set_blocking(file_object.fileno(), False)

    # Read and discard any buffered content to get the latest output
    data = file_object.read()
    if data is None:
        return ""
    return data.decode(errors="replace")


class ShellProcess:
    def __init__(
        self,
        target: Any,
        application_path: str,
        args: Sequence[str] | None = None,
        env: str = "",
    ):
        self._target = target
        self._application_path = application_path
        self._env = env
        self._args = list(args) if args is not None else []
        self._process: Any = None

    def __enter__(self) -> Any:
        self._process = self._target.execute_async(
            f"LD_LIBRARY_PATH=/ {self._env} {self._application_path} {' '.join(self._args)}"
        )

        logging.getLogger().info(
            f"Started process {self._application_path} with PID: {self._process.pid()}"
        )

        return self._process

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: TracebackType | None,
    ) -> None:
        if self._process is not None and self._process.is_running():
            # stop() on the shell process does not work in QEMU,
            # but killing the process spawned by the shell command works.
            # Not clear why the shell process itself is not killable
            kill_process_by_name(self._target, self._application_path)
            self._process.stop()


# TODO tcpdump does not capture anything here, the test is broken
def _tcpdump_capture(
    filter_expression: str, packet_count: int | None = None
) -> subprocess.Popen[bytes]:
    tcpdump_user = pwd.getpwuid(os.getuid()).pw_name
    args = [
        "/usr/bin/tcpdump",
        "-n",
        "-l",
        "-i",
        "any",
        "-Z",
        tcpdump_user,
    ]
    # TODO tcpdump cannot be killed, thus at the moment only packet_count can be used to stop it
    #      When testing it using `linux-sandbox -R -N -- /bin/bash -lc 'tcpdump -n -l -Z root -i any'`
    #      and `sudo nsenter -t $(pidof tcpdump) -a killall tcpdump` it is killable in the second shell
    if packet_count is not None:
        args.extend(["-c", str(packet_count)])
    if filter_expression:
        args.append(filter_expression)

    return subprocess.Popen(
        # Keep tcpdump under the current test user so teardown signals are permitted.
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )


def _has_captured_something(capture_text: str) -> bool:
    packet_count_match = re.search(
        r"(\d+) packets? received by filter", capture_text, re.IGNORECASE
    )
    return bool(packet_count_match and int(packet_count_match.group(1)) > 0)


def wait_for_ip_traffic(
    capture_process: Any, timeout: float = 20.0
) -> tuple[bool, str]:
    if capture_process.stdout is None or capture_process.stderr is None:
        return False, "tcpdump pipes are not available"

    capture_text = ""
    deadline = time.monotonic() + timeout
    stdout_fd = capture_process.stdout.fileno()

    def _stop_capture_process() -> None:
        if capture_process.poll() is not None:
            return
        capture_process.terminate()
        try:
            capture_process.wait(timeout=1.0)
        except subprocess.TimeoutExpired:
            capture_process.kill()
            capture_process.wait(timeout=1.0)

    while time.monotonic() < deadline:
        remaining_time = max(0.0, deadline - time.monotonic())
        wait_time = min(0.1, remaining_time)
        readable, _, _ = select.select([stdout_fd], [], [], wait_time)

        if readable:
            chunk = os.read(stdout_fd, 4096)
            if chunk:
                capture_text += chunk.decode(errors="replace")
                if _has_captured_something(capture_text):
                    _stop_capture_process()
                    return True, "line 111 " + capture_text

        if capture_process.poll() is not None:
            stderr_text = _as_text(capture_process.stderr.read())
            return (
                _has_captured_something(capture_text),
                "line 114 "
                + capture_text
                + "\ntcpdump process has exited unexpectedly with code "
                + str(capture_process.returncode)
                + "\n"
                + stderr_text,
            )

    _stop_capture_process()

    # Drain any remaining buffered stdout after termination without blocking.
    while True:
        readable, _, _ = select.select([stdout_fd], [], [], 0.0)
        if not readable:
            break
        chunk = os.read(stdout_fd, 4096)
        if not chunk:
            break
        capture_text += chunk.decode(errors="replace")

    if _has_captured_something(capture_text):
        return True, "line 122 " + capture_text

    stderr_text = _as_text(capture_process.stderr.read())
    return (
        False,
        "tcpdump did not capture IP traffic within timeout.\n"
        + capture_text
        + ("\n" + stderr_text if stderr_text else ""),
    )


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


def test_tcpdump_with_ping_from_host(target) -> None:
    with _tcpdump_capture("icmp", packet_count=2) as tcpdump_process:
        # If tcpdump exits early, killing by name will fail because the process is already gone.
        assert tcpdump_process.poll() is None, _as_text(tcpdump_process.stderr.read())

        # sanity check that tcpdump is running
        ps_aux_result = subprocess.run(
            ["ps", "aux"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        ps_aux_text = _completed_process_as_text(ps_aux_result)
        assert ps_aux_result.returncode == 0 and "tcpdump" in ps_aux_text, ps_aux_text

        exit_code, output = target.execute("ping -c 1 169.254.158.190")
        assert exit_code == 0, output.decode()

        # sanity check that tcpdump is running
        ps_aux_result = subprocess.run(
            ["ps", "aux"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        ps_aux_text = _completed_process_as_text(ps_aux_result)
        assert ps_aux_result.returncode == 0 and "tcpdump" in ps_aux_text, ps_aux_text

        exit_code, output = target.execute("ping -c 1 169.254.21.88")
        assert exit_code == 0, output.decode()

        # Now tcpdump should terminate with two captured packets
        tcpdump_process.wait(timeout=5.0)
        assert tcpdump_process.returncode == 0, _as_text(tcpdump_process.stderr.read())
        assert tcpdump_process.poll() is not None, (
            "tcpdump process should have exited by now: "
            + _as_text(tcpdump_process.stderr.read())
        )

        ps_aux_result = subprocess.run(
            ["ps", "aux"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        ps_aux_text = _completed_process_as_text(ps_aux_result)
        assert ps_aux_result.returncode == 0 and "tcpdump" not in ps_aux_text, (
            ps_aux_text
        )


def test_tcpdump_with_ping_from_target(target):
    with _tcpdump_capture("icmp", packet_count=2) as tcpdump_process:
        # If tcpdump exits early, killing by name will fail because the process is already gone.
        assert tcpdump_process.poll() is None, _as_text(tcpdump_process.stderr.read())

        # sanity check that tcpdump is running
        ps_aux_result = subprocess.run(
            ["ps", "aux"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        ps_aux_text = _completed_process_as_text(ps_aux_result)
        assert ps_aux_result.returncode == 0 and "tcpdump" in ps_aux_text, ps_aux_text

        with ShellProcess(
            target, "ping", ["-c", "1", "169.254.158.190"]
        ) as bash_process:
            while bash_process.is_running():
                time.sleep(0.1)
            assert bash_process.get_exit_code() == 0, bash_process.get_output().decode()

            # sanity check that tcpdump is running
            ps_aux_result = subprocess.run(
                ["ps", "aux"],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            ps_aux_text = _completed_process_as_text(ps_aux_result)
            assert ps_aux_result.returncode == 0 and "tcpdump" in ps_aux_text, (
                ps_aux_text
            )

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

        ps_aux_result = subprocess.run(
            ["ps", "aux"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        ps_aux_text = _completed_process_as_text(ps_aux_result)
        assert ps_aux_result.returncode == 0 and "tcpdump" not in ps_aux_text, (
            ps_aux_text
        )


def test_tcpdump_with_long_running_ping_from_target(target):
    with _tcpdump_capture("icmp", packet_count=5) as tcpdump_process:
        # If tcpdump exits early, killing by name will fail because the process is already gone.
        assert tcpdump_process.poll() is None, _as_text(tcpdump_process.stderr.read())

        # sanity check that tcpdump is running
        ps_aux_result = subprocess.run(
            ["ps", "aux"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        ps_aux_text = _completed_process_as_text(ps_aux_result)
        assert ps_aux_result.returncode == 0 and "tcpdump" in ps_aux_text, ps_aux_text

        try:
            with ShellProcess(target, "ping", ["169.254.21.88"]) as bash_process:
                logging.getLogger().info(
                    "Started ping process with PID: " + str(bash_process.pid())
                )
                # sanity check that tcpdump is running
                ps_aux_result = subprocess.run(
                    ["ps", "aux"],
                    check=True,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                )
                ps_aux_text = _completed_process_as_text(ps_aux_result)
                assert ps_aux_result.returncode == 0 and "tcpdump" in ps_aux_text, (
                    ps_aux_text
                )
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

        ps_aux_result = subprocess.run(
            ["ps", "aux"], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE
        )
        ps_aux_text = _completed_process_as_text(ps_aux_result)
        assert ps_aux_result.returncode == 0 and "tcpdump" not in ps_aux_text, (
            ps_aux_text
        )

    logging.getLogger().info(
        "Finished test_tcpdump_with_long_running_ping_from_target2"
    )
