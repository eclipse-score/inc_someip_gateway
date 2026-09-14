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

"""Host-side packet-capture helpers shared across test suites."""

import io
import logging
import os
import pwd
import shutil
import signal
import subprocess
import time
from typing import Any


def as_text(output: Any) -> str:
    """Decode bytes to str; pass str through unchanged."""
    if isinstance(output, bytes):
        return output.decode(errors="replace")
    return str(output)


def _get_content_of_file_object(file_object: io.BufferedReader | None) -> str:
    """Read available bytes from *file_object* in non-blocking mode; empty string on None."""
    if file_object is None:
        return ""

    # Non-blocking so read() returns immediately instead of waiting for more data.
    os.set_blocking(file_object.fileno(), False)

    data = file_object.read()
    if data is None:
        return ""
    return data.decode(errors="replace")


def get_output(process: subprocess.Popen[bytes]) -> str:
    """Return combined stdout + stderr from *process* as a single string."""
    return _get_content_of_file_object(process.stdout) + "\n, stderr: " + _get_content_of_file_object(process.stderr)


def is_process_alive(pid: int) -> bool:
    """Return True if *pid* exists and is not a zombie (defunct) process.

    A zombie has already exited; it is kept in the process table only until
    its parent reaps it via wait(). It should not count as "running".
    """
    try:
        with open(f"/proc/{pid}/status") as status_file:
            for line in status_file:
                if line.startswith("State:"):
                    return "zombie" not in line.lower()
        return True  # status file exists but no State line; assume alive
    except FileNotFoundError:
        return False


def wait_until_process_exits(process: subprocess.Popen[bytes], timeout: float = 10.0) -> str:
    """Poll *process* until it exits or *timeout* seconds elapse.

    Returns the combined stdout+stderr output on success.
    Raises ``TimeoutError`` if the process has not exited within *timeout*
    seconds.
    """
    start_time = time.time()
    while time.time() - start_time < timeout:
        if process.poll() is not None:
            return get_output(process)
        time.sleep(0.5)
    raise TimeoutError(f"Process did not exit within {timeout} seconds. Last output: {get_output(process)}")


def stop_capture(
    proc: subprocess.Popen[bytes],
    timeout: float = 5.0,
) -> bool:
    """Send SIGINT to *proc*; fall back to SIGKILL + pkill sweep on timeout.

    Pkill mops up -Z privilege-separation children that survive the SIGKILL. Returns True if exited cleanly, False if SIGKILL was needed.
    """
    if proc.poll() is not None:
        return True

    try:
        proc.send_signal(signal.SIGINT)
    except ProcessLookupError:
        return True  # process exited between poll() and send_signal()
    except PermissionError:
        return False  # EPERM from -Z sandbox barrier; process likely still alive

    try:
        proc.wait(timeout=timeout)
        return True
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        subprocess.run(["pkill", "-9", "-x", "tcpdump"], check=False)
        return False


def _close_pipes_and_wait(proc: subprocess.Popen[bytes], timeout: float = 30.0) -> bool:
    """Close *proc*'s stdout/stderr pipes, then wait for it to exit naturally.

    No force-kill: SIGKILL is not reliably delivered to tcpdump's -Z privsep
    child in the CI sandbox. *timeout* only gates a diagnostic warning and a
    best-effort `pkill -9 -x tcpdump` sweep; it never triggers a kill. Always
    returns True once the process has exited.
    """
    logger = logging.getLogger(__name__)

    if proc.poll() is not None:
        return True

    for stream in (proc.stdin, proc.stdout, proc.stderr):
        if stream is not None:
            try:
                stream.close()
            except OSError:
                pass

    logger.info("tcpdump (pid=%s) pipes closed; waiting for natural exit", proc.pid)

    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        logger.warning(
            "tcpdump (pid=%s) still alive %.0fs after pipe close (alive=%s); "
            "running best-effort pkill sweep and continuing to wait",
            proc.pid,
            timeout,
            is_process_alive(proc.pid),
        )
        subprocess.run(["pkill", "-9", "-x", "tcpdump"], check=False)
        proc.wait()

    logger.info("tcpdump (pid=%s) exited with returncode=%s", proc.pid, proc.returncode)
    return True


class CaptureProcess:
    """Context manager wrapper around a tcpdump Popen.

    Text-mode captures close pipes (SIGPIPE); pcap-mode captures use SIGINT via stop_capture. Moves the pcap from /tmp to the final path on exit and delegates attribute access to the wrapped Popen.
    """

    def __init__(
        self,
        proc: subprocess.Popen[bytes],
        text_mode: bool,
        tmp_pcap: str | None = None,
        final_pcap: str | None = None,
    ) -> None:
        self._proc = proc
        self._text_mode = text_mode
        self._tmp_pcap = tmp_pcap
        self._final_pcap = final_pcap

    def __enter__(self) -> subprocess.Popen[bytes]:
        return self._proc

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: Any,
    ) -> None:
        if self._text_mode:
            _close_pipes_and_wait(self._proc)
        else:
            stop_capture(self._proc)
        if self._tmp_pcap and self._final_pcap and self._tmp_pcap != self._final_pcap:
            try:
                shutil.move(self._tmp_pcap, self._final_pcap)
            except OSError:
                pass  # best-effort; destination may be in a restricted sandbox path

    def __getattr__(self, name: str) -> Any:  # noqa: ANN401
        return getattr(self._proc, name)

    def force_stop(self, stimulus_cmd: list[str] | None = None, timeout: float = 5.0) -> bool:
        """Stop an idle text-mode capture: close pipes, trigger one matching packet, wait.

        Signals to tcpdump are not reliably delivered in this sandbox; this
        instead forces the SIGPIPE-on-write path the traffic-based tests
        already rely on. Not valid for pcap mode (writes to a file, not the pipe).
        """
        if not self._text_mode:
            raise ValueError("force_stop only applies to text-mode captures")

        for stream in (self._proc.stdin, self._proc.stdout, self._proc.stderr):
            if stream is not None:
                try:
                    stream.close()
                except OSError:
                    pass

        subprocess.run(
            stimulus_cmd or ["ping", "-c", "1", "-W", "1", "127.0.0.1"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )

        try:
            self._proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            pass

        return self._proc.poll() is not None


def tcpdump_capture(
    filter_expression: str,
    packet_count: int | None = None,
    output_file: str | None = None,
) -> CaptureProcess:
    """Start tcpdump on the host and return a CaptureProcess context manager.

    Args:
        filter_expression: BPF filter string.
        packet_count: Exit after this many packets; omit when using stop_capture.
        output_file: Move the completed pcap to this path after capture stops;
                     None streams text to stdout.

    Raises:
        RuntimeError: tcpdump exited immediately (missing binary or CAP_NET_RAW).
    """
    # -Z <current_user>: avoids tcpdump dropping to its default user, which fails without CAP_SETUID in CI (even as root).
    try:
        _z_user = pwd.getpwuid(os.getuid()).pw_name
    except KeyError:
        # uid has no /etc/passwd entry (minimal container); use numeric fallback
        _z_user = str(os.getuid())

    args = ["/usr/bin/tcpdump", "-n", "-i", "any", "-Z", _z_user]

    # Write pcap to /tmp, which is writable in all sandbox configurations.
    # CaptureProcess.__exit__ moves it to output_file after capture stops.
    text_mode = output_file is None
    tmp_pcap: str | None = None
    if output_file is not None:
        tmp_pcap = f"/tmp/tcpdump_{os.urandom(8).hex()}.pcap"
        args.extend(["-U", "-w", tmp_pcap])  # -U: packet-buffered
    else:
        args.append("-l")  # line-buffered output for text mode

    if packet_count is not None:
        args.extend(["-c", str(packet_count)])
    if filter_expression:
        args.append(filter_expression)

    proc = subprocess.Popen(
        args,
        stdout=subprocess.PIPE if output_file is None else subprocess.DEVNULL,
        stderr=subprocess.PIPE,
    )

    # tcpdump exits within milliseconds if CAP_NET_RAW is missing or binary absent.
    time.sleep(0.2)
    if proc.poll() is not None and proc.returncode != 0:
        stderr_text = _get_content_of_file_object(proc.stderr)
        raise RuntimeError(
            f"tcpdump failed to start (exit code {proc.returncode}). "
            f"Check that /usr/bin/tcpdump exists and the process has "
            f"CAP_NET_RAW capability. stderr: {stderr_text}"
        )

    return CaptureProcess(proc, text_mode=text_mode, tmp_pcap=tmp_pcap, final_pcap=output_file)
