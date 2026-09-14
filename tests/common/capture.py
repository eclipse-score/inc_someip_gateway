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
    """Send SIGINT to *proc* (tcpdump flushes the pcap cleanly on SIGINT), wait,
    fall back to SIGKILL + pkill sweep on timeout.

    After SIGKILL, ``pkill -9 -x tcpdump`` mops up any privilege-separation
    child processes forked by tcpdump's -Z handling that survive a parent kill.

    Returns True if the process exited cleanly (or was already gone), False if
    SIGKILL was required.
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


def _close_pipes_and_wait(proc: subprocess.Popen[bytes], timeout: float = 5.0) -> bool:
    """Close *proc*'s stdout/stderr pipes so it gets SIGPIPE/EPIPE on its next
    write, then wait for it to exit; SIGKILL on timeout.

    Text-mode tcpdump captures (no output file, stdout=PIPE) are torn down
    this way rather than via SIGINT: tcpdump's -Z privilege-separation forks
    a child even when dropping to its own current user, and that child does
    not receive SIGINT reliably in a sandboxed user namespace (EPERM).
    Closing the pipe it writes to is delivery-independent and reliably
    terminates it.

    Returns True if the process exited cleanly, False if SIGKILL was required.
    """
    if proc.poll() is not None:
        return True

    for stream in (proc.stdin, proc.stdout, proc.stderr):
        if stream is not None:
            try:
                stream.close()
            except OSError:
                pass

    try:
        proc.wait(timeout=timeout)
        return True
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
        return False


class CaptureProcess:
    """Context manager wrapper around a tcpdump Popen.

    Text-mode captures (no output file) are torn down by closing stdout/stderr
    so tcpdump gets SIGPIPE on its next write; this avoids relying on SIGINT
    delivery to a possibly privilege-separated -Z child. Pcap-mode captures
    (output_file set, stdout=DEVNULL) have no pipe to close, so SIGINT via
    stop_capture is used instead, falling back to SIGKILL on timeout. If the
    pcap was written to a temporary /tmp path, it is moved to the caller's
    requested destination after capture stops.
    Direct attribute access (poll, kill, wait, returncode, …) is delegated to
    the wrapped Popen so callers that store the object directly continue to work.
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
    # Pass -Z <current_user> so tcpdump drops to the process's own uid rather
    # than its compiled-in default user, which fails without CAP_SETUID in CI
    # (observed even as root: tcpdump still tries to drop to its default
    # non-root user unless told otherwise).
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
