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

"""Unit tests for tests/common/capture.py.

Host-only: no QEMU, network, or CAP_NET_RAW required.
"""

import os
import subprocess

import pytest

import time

from capture import (
    as_text,
    _close_pipes_and_wait,
    _get_content_of_file_object,
    get_output,
    is_process_alive,
    stop_capture,
    tcpdump_capture,
    wait_until_process_exits,
)


# ---------------------------------------------------------------------------
# as_text
# ---------------------------------------------------------------------------


def test_as_text_decodes_bytes() -> None:
    """Bytes are decoded to str with UTF-8 (replacement on error)."""
    assert as_text(b"hello") == "hello"


def test_as_text_passes_str_through() -> None:
    """Plain str is returned unchanged."""
    assert as_text("world") == "world"


def test_as_text_converts_other_types() -> None:
    """Non-str, non-bytes values are converted via str()."""
    assert as_text(42) == "42"


def test_as_text_handles_invalid_utf8() -> None:
    """Invalid UTF-8 bytes are replaced rather than raising."""
    result = as_text(b"\xff\xfe")
    assert isinstance(result, str)


# ---------------------------------------------------------------------------
# _get_content_of_file_object
# ---------------------------------------------------------------------------


def test_get_content_of_file_object_none_returns_empty() -> None:
    """None input returns an empty string without raising."""
    assert _get_content_of_file_object(None) == ""


def test_get_content_of_file_object_reads_pipe() -> None:
    """Content written to a pipe is read back as a str."""
    proc = subprocess.Popen(
        ["echo", "-n", "capture_test"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    proc.wait()
    result = _get_content_of_file_object(proc.stdout)
    assert "capture_test" in result


# ---------------------------------------------------------------------------
# get_output
# ---------------------------------------------------------------------------


def test_get_output_includes_stdout_and_stderr() -> None:
    """get_output returns a string containing both stdout and stderr content."""
    proc = subprocess.Popen(
        ["sh", "-c", "echo out; echo err >&2"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    proc.wait()
    output = get_output(proc)
    assert "out" in output
    assert "err" in output


def test_get_output_empty_process() -> None:
    """A process that writes nothing returns a string (may be empty or whitespace)."""
    proc = subprocess.Popen(
        ["true"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    proc.wait()
    result = get_output(proc)
    assert isinstance(result, str)


# ---------------------------------------------------------------------------
# is_process_alive
# ---------------------------------------------------------------------------


def test_is_process_alive_true_for_running_process() -> None:
    """A running process is reported as alive."""
    proc = subprocess.Popen(["sleep", "5"])
    try:
        assert is_process_alive(proc.pid) is True
    finally:
        proc.kill()
        proc.wait()


def test_is_process_alive_false_for_missing_pid() -> None:
    """A pid with no /proc entry is reported as not alive."""
    missing_pid = 2**22  # far beyond any pid this test process could have
    assert is_process_alive(missing_pid) is False


def test_is_process_alive_false_for_zombie() -> None:
    """A zombie (exited, not yet reaped) process is reported as not alive."""
    proc = subprocess.Popen(["true"])

    # Wait for the kernel to transition the process to zombie state, without
    # reaping it ourselves (that would remove it from the process table).
    deadline = time.monotonic() + 5.0
    status_path = f"/proc/{proc.pid}/status"
    is_zombie = False
    while time.monotonic() < deadline:
        try:
            with open(status_path) as status_file:
                if any("zombie" in line.lower() for line in status_file if line.startswith("State:")):
                    is_zombie = True
                    break
        except FileNotFoundError:
            break
        time.sleep(0.01)

    try:
        assert is_zombie, "process did not reach zombie state before timeout"
        assert is_process_alive(proc.pid) is False
    finally:
        proc.wait()  # reap


# ---------------------------------------------------------------------------
# wait_until_process_exits
# ---------------------------------------------------------------------------


def test_wait_until_process_exits_returns_output() -> None:
    """A process that exits immediately returns its output as a str."""
    proc = subprocess.Popen(
        ["echo", "done"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    output = wait_until_process_exits(proc, timeout=5.0)
    assert isinstance(output, str)
    assert proc.poll() is not None


def test_wait_until_process_exits_raises_on_timeout() -> None:
    """TimeoutError is raised when the process does not exit in time."""
    proc = subprocess.Popen(
        ["sleep", "60"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        with pytest.raises(TimeoutError):
            wait_until_process_exits(proc, timeout=0.5)
    finally:
        proc.kill()
        proc.wait()


# ---------------------------------------------------------------------------
# tcpdump_capture: argument validation and flag verification
# ---------------------------------------------------------------------------


def test_tcpdump_capture_output_file_without_packet_count_does_not_raise(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """No error when output_file is set and packet_count is omitted; stop_capture handles teardown."""
    import capture as capture_module  # noqa: PLC0415

    original_popen = capture_module.subprocess.Popen

    def _popen_sleep(args: list, **kwargs):  # type: ignore[override]
        return original_popen(["sleep", "5"], **kwargs)

    monkeypatch.setattr(capture_module.subprocess, "Popen", _popen_sleep)

    proc = tcpdump_capture("icmp", output_file="/tmp/test.pcap")
    proc.kill()
    proc.wait()


def test_tcpdump_capture_pcap_mode_includes_dash_u_flag(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """-U (packet-buffered) is included when output_file is set."""
    import capture as capture_module  # noqa: PLC0415

    original_popen = capture_module.subprocess.Popen
    captured_args: list[list[str]] = []

    def _popen_record(args: list, **kwargs):  # type: ignore[override]
        captured_args.append(list(args))
        return original_popen(["sleep", "5"], **kwargs)

    monkeypatch.setattr(capture_module.subprocess, "Popen", _popen_record)

    proc = tcpdump_capture("icmp", output_file="/tmp/test.pcap")
    proc.kill()
    proc.wait()

    assert captured_args, "Popen was not called"
    assert "-U" in captured_args[0], f"-U flag missing from tcpdump args: {captured_args[0]}"


def test_tcpdump_capture_text_mode_excludes_dash_u_flag(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """-U is not added in text mode (no output_file); it's a pcap-specific flag."""
    import capture as capture_module  # noqa: PLC0415

    original_popen = capture_module.subprocess.Popen
    captured_args: list[list[str]] = []

    def _popen_record(args: list, **kwargs):  # type: ignore[override]
        captured_args.append(list(args))
        return original_popen(["sleep", "5"], **kwargs)

    monkeypatch.setattr(capture_module.subprocess, "Popen", _popen_record)

    proc = tcpdump_capture("icmp")
    proc.kill()
    proc.wait()

    assert captured_args, "Popen was not called"
    assert "-U" not in captured_args[0], f"-U should not be in text-mode args: {captured_args[0]}"


def test_tcpdump_capture_includes_z_flag_with_current_user(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """-Z uses the current username so tcpdump doesn't drop to its default user, which fails without CAP_SETUID in CI."""
    import os
    import pwd
    import capture as capture_module  # noqa: PLC0415

    original_popen = capture_module.subprocess.Popen
    captured_args: list[list[str]] = []

    def _popen_record(args: list, **kwargs):  # type: ignore[override]
        captured_args.append(list(args))
        return original_popen(["sleep", "5"], **kwargs)

    monkeypatch.setattr(capture_module.subprocess, "Popen", _popen_record)

    proc = tcpdump_capture("icmp")
    proc.kill()
    proc.wait()

    assert captured_args, "Popen was not called"
    args = captured_args[0]
    assert "-Z" in args, f"-Z flag missing from tcpdump args: {args}"

    z_index = args.index("-Z")
    assert z_index + 1 < len(args), "-Z flag has no argument"
    z_user = args[z_index + 1]

    try:
        expected_user = pwd.getpwuid(os.getuid()).pw_name
    except KeyError:
        expected_user = str(os.getuid())

    assert z_user == expected_user, f"-Z argument is '{z_user}', expected '{expected_user}'"


def test_tcpdump_capture_pcap_written_under_tmp(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """tcpdump writes pcaps to /tmp, which stays writable after its -Z credential drop (unlike the bind-mounted TEST_UNDECLARED_OUTPUTS_DIR)."""
    import capture as capture_module  # noqa: PLC0415

    original_popen = capture_module.subprocess.Popen
    captured_args: list[list[str]] = []

    def _popen_record(args: list, **kwargs):  # type: ignore[override]
        captured_args.append(list(args))
        return original_popen(["sleep", "5"], **kwargs)

    monkeypatch.setattr(capture_module.subprocess, "Popen", _popen_record)

    proc = tcpdump_capture("icmp", output_file="/some/output/dir/test.pcap")
    proc.kill()
    proc.wait()

    assert captured_args, "Popen was not called"
    args = captured_args[0]
    w_index = args.index("-w")
    pcap_path = args[w_index + 1]
    assert pcap_path.startswith("/tmp/"), f"tcpdump pcap path should be under /tmp, got: {pcap_path}"


# ---------------------------------------------------------------------------
# stop_capture
# ---------------------------------------------------------------------------


def test_stop_capture_returns_true_for_already_exited_process() -> None:
    """stop_capture returns True immediately when the process has already exited."""
    proc = subprocess.Popen(
        ["true"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    proc.wait()
    result = stop_capture(proc, timeout=2.0)
    assert result is True


def test_stop_capture_handles_permission_error() -> None:
    """PermissionError from proc.send_signal (EPERM in sandbox) returns False.

    EPERM means the -Z privilege-drop created a signaling barrier; the process
    is likely still alive (unlike ProcessLookupError, which returns True).
    """
    proc = subprocess.Popen(
        ["sleep", "60"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    original_send_signal = proc.send_signal

    def _raise_eperm(sig: int) -> None:
        raise PermissionError("Operation not permitted (simulated)")

    proc.send_signal = _raise_eperm  # type: ignore[method-assign]

    result = stop_capture(proc, timeout=1.0)
    assert result is False

    # Process is still alive (signal was faked); restore and kill for real.
    proc.send_signal = original_send_signal  # type: ignore[method-assign]
    proc.kill()
    proc.wait()


def test_stop_capture_handles_process_lookup_error() -> None:
    """ProcessLookupError from proc.send_signal is treated as 'process already gone'.

    Covers the race where the process exits between poll() returning None
    and the send_signal() call.  The correct response is True (not an exception).
    """
    proc = subprocess.Popen(
        ["sleep", "60"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    original_send_signal = proc.send_signal

    def _raise_esrch(sig: int) -> None:
        raise ProcessLookupError("No such process (simulated)")

    proc.send_signal = _raise_esrch  # type: ignore[method-assign]

    result = stop_capture(proc, timeout=1.0)
    assert result is True

    # Process is still alive (signal was faked); restore and kill for real.
    proc.send_signal = original_send_signal  # type: ignore[method-assign]
    proc.kill()
    proc.wait()


def test_stop_capture_graceful_sigint_returns_true() -> None:
    """stop_capture sends SIGINT; the process exits cleanly, returns True."""
    proc = subprocess.Popen(
        ["sleep", "60"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    result = stop_capture(proc, timeout=2.0)
    assert result is True
    assert proc.poll() is not None, "process should have exited after stop_capture"


def test_stop_capture_kills_on_timeout_returns_false() -> None:
    """stop_capture falls back to SIGKILL when SIGINT is ignored; returns False.

    A pipe gates SIGINT until SIG_IGN is installed, avoiding a startup race.
    start_new_session=True isolates the child so SIGKILL does not reach the
    test runner's process group.
    """
    r_fd, w_fd = os.pipe()
    proc = subprocess.Popen(
        [
            "python3",
            "-c",
            (
                "import signal, time, os; "
                "signal.signal(signal.SIGINT, signal.SIG_IGN); "
                f"os.write({w_fd}, b'r'); "
                "time.sleep(60)"
            ),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        pass_fds=(w_fd,),
        start_new_session=True,
    )
    os.close(w_fd)
    os.read(r_fd, 1)  # Block until child has installed SIG_IGN.
    os.close(r_fd)

    result = stop_capture(proc, timeout=0.3)
    assert result is False
    assert proc.poll() is not None, "process should be dead after SIGKILL fallback"


def test_stop_capture_sweeps_orphans_with_pkill(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """pkill sweeps orphaned tcpdump children by name after SIGKILL, since process-group targeting raises EPERM in the CI sandbox."""
    import capture as capture_module  # noqa: PLC0415

    pkill_calls: list[list[str]] = []
    original_run = capture_module.subprocess.run

    def _record_run(cmd: list, **kwargs):  # type: ignore[override]
        pkill_calls.append(list(cmd))
        return original_run(cmd, **kwargs)

    monkeypatch.setattr(capture_module.subprocess, "run", _record_run)

    r_fd, w_fd = os.pipe()
    proc = subprocess.Popen(
        [
            "python3",
            "-c",
            (
                "import signal, time, os; "
                "signal.signal(signal.SIGINT, signal.SIG_IGN); "
                f"os.write({w_fd}, b'r'); "
                "time.sleep(60)"
            ),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        pass_fds=(w_fd,),
        start_new_session=True,
    )
    os.close(w_fd)
    os.read(r_fd, 1)
    os.close(r_fd)

    result = stop_capture(proc, timeout=0.3)
    assert result is False

    pkill_invoked = any(
        len(cmd) >= 4 and cmd[0] == "pkill" and "-9" in cmd and "-x" in cmd and "tcpdump" in cmd for cmd in pkill_calls
    )
    assert pkill_invoked, f"pkill -9 -x tcpdump not called after SIGKILL; subprocess.run calls: {pkill_calls}"


# ---------------------------------------------------------------------------
# _close_pipes_and_wait
# ---------------------------------------------------------------------------


def test_close_pipes_and_wait_returns_true_for_already_exited_process() -> None:
    """Returns True immediately when the process has already exited."""
    proc = subprocess.Popen(["true"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    proc.wait()
    assert _close_pipes_and_wait(proc, timeout=2.0) is True


def test_close_pipes_and_wait_terminates_process_writing_to_stdout() -> None:
    """Closing stdout causes a process still writing to it to die (SIGPIPE), without needing a kill."""
    proc = subprocess.Popen(
        ["python3", "-c", "import time\nwhile True:\n print('x', flush=True)\n time.sleep(0.05)"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    kill_called = False

    def _mark_kill() -> None:
        nonlocal kill_called
        kill_called = True

    proc.kill = _mark_kill  # type: ignore[method-assign]

    result = _close_pipes_and_wait(proc, timeout=5.0)
    assert result is True
    assert proc.poll() is not None
    assert not kill_called, "normal SIGPIPE exit must not require a forced kill"


def test_close_pipes_and_wait_logs_slow_teardown_and_reaps_without_kill(
    monkeypatch: pytest.MonkeyPatch,
    caplog: pytest.LogCaptureFixture,
) -> None:
    """A process that outlives the wait timeout is never force-killed; it logs a warning,
    runs the best-effort pkill sweep, and keeps waiting until the process exits on its own."""
    import capture as capture_module  # noqa: PLC0415

    pkill_calls: list[list[str]] = []
    original_run = capture_module.subprocess.run

    def _record_run(cmd: list, **kwargs):  # type: ignore[override]
        pkill_calls.append(list(cmd))
        return original_run(cmd, **kwargs)

    monkeypatch.setattr(capture_module.subprocess, "run", _record_run)

    # Never writes to stdout/stderr, so pipe-close alone won't end it; exits on its own
    # shortly after the function's wait timeout, standing in for "slow but not stuck".
    proc = subprocess.Popen(
        ["python3", "-c", "import time; time.sleep(0.6)"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    kill_called = False

    def _mark_kill() -> None:
        nonlocal kill_called
        kill_called = True

    proc.kill = _mark_kill  # type: ignore[method-assign]

    with caplog.at_level("WARNING"):
        result = _close_pipes_and_wait(proc, timeout=0.2)

    assert result is True
    assert proc.poll() is not None
    assert not kill_called, "slow teardown must not fall back to a forced kill"
    assert pkill_calls, "expected the best-effort pkill sweep to run"
    assert any("still alive" in record.message for record in caplog.records)


# ---------------------------------------------------------------------------
# CaptureProcess: text-mode vs pcap-mode teardown routing
# ---------------------------------------------------------------------------


def test_capture_process_text_mode_uses_pipe_close_not_sigint(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Text-mode capture (output_file=None) tears down via pipe-close, not stop_capture/SIGINT."""
    import capture as capture_module  # noqa: PLC0415

    close_calls: list[bool] = []
    stop_calls: list[bool] = []
    monkeypatch.setattr(capture_module, "_close_pipes_and_wait", lambda proc, timeout=5.0: close_calls.append(True))
    monkeypatch.setattr(capture_module, "stop_capture", lambda proc, timeout=5.0: stop_calls.append(True))

    proc = subprocess.Popen(["sleep", "60"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    capture = capture_module.CaptureProcess(proc, text_mode=True)
    with capture:
        pass

    assert close_calls, "expected pipe-close teardown for text mode"
    assert not stop_calls, "SIGINT teardown should not run for text mode"
    proc.kill()
    proc.wait()


def test_capture_process_pcap_mode_uses_stop_capture(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """Pcap-mode capture (output_file set) tears down via stop_capture/SIGINT, not pipe-close."""
    import capture as capture_module  # noqa: PLC0415

    close_calls: list[bool] = []
    stop_calls: list[bool] = []
    monkeypatch.setattr(capture_module, "_close_pipes_and_wait", lambda proc, timeout=5.0: close_calls.append(True))
    monkeypatch.setattr(capture_module, "stop_capture", lambda proc, timeout=5.0: stop_calls.append(True))

    proc = subprocess.Popen(["sleep", "60"], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    capture = capture_module.CaptureProcess(proc, text_mode=False)
    with capture:
        pass

    assert stop_calls, "expected SIGINT teardown for pcap mode"
    assert not close_calls, "pipe-close teardown should not run for pcap mode"
    proc.kill()
    proc.wait()


# ---------------------------------------------------------------------------
# tcpdump_capture: CAP_NET_RAW startup-failure detection
# ---------------------------------------------------------------------------


def test_tcpdump_capture_raises_on_immediate_exit(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """RuntimeError is raised when tcpdump exits immediately (CAP_NET_RAW denied or binary absent)."""
    import capture as capture_module  # noqa: PLC0415

    original_popen = capture_module.subprocess.Popen

    def _popen_false(args: list, **kwargs):  # type: ignore[override]
        return original_popen(["/usr/bin/false"], **kwargs)

    monkeypatch.setattr(capture_module.subprocess, "Popen", _popen_false)

    with pytest.raises(RuntimeError, match="tcpdump failed to start"):
        tcpdump_capture("icmp")


def test_force_stop_closes_pipes_runs_stimulus_and_reaps(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """force_stop closes pipes, runs the stimulus command, and waits for the process to die."""
    import capture as capture_module  # noqa: PLC0415

    stimulus_calls: list[list[str]] = []

    def _record_run(cmd: list, **kwargs):  # type: ignore[override]
        stimulus_calls.append(list(cmd))
        return subprocess.CompletedProcess(cmd, 0)

    monkeypatch.setattr(capture_module.subprocess, "run", _record_run)

    proc = subprocess.Popen(
        ["python3", "-c", "import time; time.sleep(0.2); print('x', flush=True)"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    capture = capture_module.CaptureProcess(proc, text_mode=True)

    result = capture.force_stop(stimulus_cmd=["true"], timeout=5.0)

    assert result is True
    assert proc.poll() is not None
    assert stimulus_calls == [["true"]]


def test_force_stop_raises_for_pcap_mode() -> None:
    """force_stop is text-mode only; pcap-mode writes to a file, not the pipe."""
    import capture as capture_module  # noqa: PLC0415

    proc = subprocess.Popen(["sleep", "60"], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    capture = capture_module.CaptureProcess(proc, text_mode=False)

    try:
        with pytest.raises(ValueError, match="text-mode"):
            capture.force_stop()
    finally:
        proc.kill()
        proc.wait()


def test_force_stop_returns_false_if_process_survives_stimulus(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """force_stop returns False (does not raise) if the process is still alive after the timeout."""
    import capture as capture_module  # noqa: PLC0415

    monkeypatch.setattr(
        capture_module.subprocess,
        "run",
        lambda cmd, **kwargs: subprocess.CompletedProcess(cmd, 0),  # stimulus is a no-op
    )

    proc = subprocess.Popen(["sleep", "60"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    capture = capture_module.CaptureProcess(proc, text_mode=True)

    try:
        result = capture.force_stop(stimulus_cmd=["true"], timeout=0.2)
        assert result is False
    finally:
        proc.kill()
        proc.wait()


def test_tcpdump_capture_does_not_raise_on_immediate_clean_exit(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """No RuntimeError on immediate exit code 0 (e.g. packet_count=1 captured instantly).

    Only non-zero exits within the 0.2s window count as a startup failure.
    """
    import capture as capture_module  # noqa: PLC0415

    original_popen = capture_module.subprocess.Popen

    def _popen_true(args: list, **kwargs):  # type: ignore[override]
        return original_popen(["true"], **kwargs)

    monkeypatch.setattr(capture_module.subprocess, "Popen", _popen_true)

    proc = tcpdump_capture("icmp")
    proc.wait()
