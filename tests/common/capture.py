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
    return (
        _get_content_of_file_object(process.stdout)
        + "\n, stderr: "
        + _get_content_of_file_object(process.stderr)
    )


def wait_until_process_exits(
    process: subprocess.Popen[bytes], timeout: float = 10.0
) -> str:
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
    raise TimeoutError(
        f"Process did not exit within {timeout} seconds. "
        f"Last output: {get_output(process)}"
    )


def stop_capture(
    proc: subprocess.Popen[bytes],
    timeout: float = 5.0,
) -> bool:
    """Send SIGINT to the process group of *proc* (tcpdump flushes pcap cleanly), wait,
    fall back to SIGKILL on the group.

    Killing the entire process group ensures that any child forked by tcpdump
    (e.g. due to -Z privilege-separation) is also terminated, preventing orphans.

    Returns True if the process exited cleanly (or was already gone), False if
    SIGKILL was required.
    """
    if proc.poll() is not None:
        return True  # already exited

    try:
        os.killpg(os.getpgid(proc.pid), signal.SIGINT)
    except ProcessLookupError:
        return True  # group already gone

    try:
        proc.wait(timeout=timeout)
        return True
    except subprocess.TimeoutExpired:
        try:
            os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
        except ProcessLookupError:
            pass  # group gone between timeout and SIGKILL
        proc.wait()
        return False


class CaptureProcess:
    """Context manager wrapper around a tcpdump Popen.

    On context exit, sends SIGINT to flush the pcap cleanly, then falls back
    to SIGKILL if the process does not terminate within the grace period.
    Direct attribute access (poll, kill, wait, returncode, …) is delegated to
    the wrapped Popen so callers that store the object directly continue to work.
    """

    def __init__(self, proc: subprocess.Popen[bytes]) -> None:
        self._proc = proc

    def __enter__(self) -> subprocess.Popen[bytes]:
        return self._proc

    def __exit__(
        self,
        exc_type: type[BaseException] | None,
        exc: BaseException | None,
        tb: Any,
    ) -> None:
        stop_capture(self._proc)

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
        output_file: Write pcap to this path; None streams text to stdout.

    Raises:
        RuntimeError: tcpdump exited immediately (missing binary or CAP_NET_RAW).
    """
    # Pass -Z <current_user> so tcpdump drops to the process's own uid rather
    # than its compiled-in default user, which fails in CI without CAP_SETUID.
    try:
        _z_user = pwd.getpwuid(os.getuid()).pw_name
    except KeyError:
        # uid has no /etc/passwd entry (minimal container); use numeric fallback
        _z_user = "root" if os.getuid() == 0 else str(os.getuid())

    args = [
        "/usr/bin/tcpdump",
        "-n",
        "-i",
        "any",
        "-Z",
        _z_user,
    ]
    if output_file is not None:
        args.extend(
            ["-U", "-w", output_file]
        )  # -U: packet-buffered, readable after abrupt stop
    else:
        # -l: line-buffered output for text (non-pcap) mode.
        args.append("-l")
    if packet_count is not None:
        args.extend(["-c", str(packet_count)])
    if filter_expression:
        args.append(filter_expression)

    if output_file is not None:
        pcap_dir = os.path.dirname(output_file)
        if pcap_dir:
            os.makedirs(pcap_dir, exist_ok=True)

    proc = subprocess.Popen(
        args,
        stdout=subprocess.PIPE if output_file is None else subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        start_new_session=True,
    )

    # tcpdump exits within milliseconds if CAP_NET_RAW is missing or binary absent.
    time.sleep(0.2)
    if proc.poll() is not None:
        stderr_text = _get_content_of_file_object(proc.stderr)
        raise RuntimeError(
            f"tcpdump failed to start (exit code {proc.returncode}). "
            f"Check that /usr/bin/tcpdump exists and the process has "
            f"CAP_NET_RAW capability. stderr: {stderr_text}"
        )

    return CaptureProcess(proc)
