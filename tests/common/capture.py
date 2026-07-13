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
    """Send SIGINT to *proc* (tcpdump flushes pcap cleanly), wait, fall back to SIGKILL.

    Returns True if SIGINT was sufficient; False if SIGKILL was needed.
    """
    if proc.poll() is not None:
        return True  # already exited

    try:
        proc.send_signal(signal.SIGINT)
    except OSError:
        # Process may have exited between poll() and send_signal().
        return True

    try:
        proc.wait(timeout=timeout)
        return True
    except subprocess.TimeoutExpired:
        pass

    # Graceful shutdown timed out — force kill.
    try:
        proc.kill()
        proc.wait(timeout=5.0)
    except (OSError, subprocess.TimeoutExpired):
        pass

    return False


def tcpdump_capture(
    filter_expression: str,
    packet_count: int | None = None,
    output_file: str | None = None,
) -> subprocess.Popen[bytes]:
    """Start tcpdump on the host and return the Popen object.

    Args:
        filter_expression: BPF filter string.
        packet_count: Exit after this many packets; omit when using stop_capture.
        output_file: Write pcap to this path; None streams text to stdout.

    Raises:
        RuntimeError: tcpdump exited immediately (missing binary or CAP_NET_RAW).
    """
    tcpdump_user = pwd.getpwuid(os.getuid()).pw_name
    args = [
        "/usr/bin/tcpdump",
        "-n",
        "-i",
        "any",
        "-Z",
        tcpdump_user,
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

    proc = subprocess.Popen(
        args,
        stdout=subprocess.PIPE if output_file is None else subprocess.DEVNULL,
        stderr=subprocess.PIPE,
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

    return proc
