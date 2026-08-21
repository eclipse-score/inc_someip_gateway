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

"""Process orchestration for the two gatewayd/someipd nodes of the end-to-end performance test.

Both nodes run on the same host. Because the gatewayd/someipd IPC socket name, the
gateway_ipc_binding shared memory names, the vsomeip unix sockets and the LoLa shared memory are
all global to the host, every one of them is given a per-node name; see README.md.
"""

from __future__ import annotations

import glob
import json
import os
import shutil
import signal
import socket
import subprocess
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence

SOMEIPD = Path("score/someipd/someipd")
GATEWAYD = Path("score/gatewayd/gatewayd")
PERF_SENDER = Path("tests/benchmarks/perf_sender")
PERF_RECEIVER = Path("tests/benchmarks/perf_receiver")
CONFIG_DIR = Path("tests/benchmarks/config")

GATEWAYD_READY_MARKER = "[gatewayd] IPC connection to someipd established"
SD_MULTICAST_ADDRESS = "224.244.224.245"

# Loopback addresses within the network namespace set up by setup_network.sh, so they cannot
# clash with the host.
NODE_A_IP = "127.0.0.2"
NODE_B_IP = "127.0.0.3"
NETMASK = "255.0.0.0"
VSOMEIP_LOG_LEVEL = os.environ.get("E2E_PERF_VSOMEIP_LOG_LEVEL", "warning")
PERF_BIN = os.environ.get("E2E_PERF_PERF_BIN", "perf")

STALE_PATH_PATTERNS = (
    "/tmp/perf-node-*",
    "/dev/shm/perf_*",
    "/dev/shm/counterpart_perf_*",
)


@dataclass(frozen=True)
class NodeSpec:
    """Everything that has to be unique per node so the two nodes do not collide."""

    name: str
    ipc_channel: str
    vsomeip_network: str
    unicast_ip: str
    someip_config: Path
    mw_com_config: Path
    vsomeip_template: Path


NODE_A = NodeSpec(
    name="node_a",
    ipc_channel="perf_ipc_a",
    vsomeip_network="perf-node-a",
    unicast_ip=NODE_A_IP,
    someip_config=Path("tests/benchmarks/node_a_someip_config.bin"),
    mw_com_config=CONFIG_DIR / "node_a_mw_com_config.json",
    vsomeip_template=CONFIG_DIR / "vsomeip_node_a.json",
)

NODE_B = NodeSpec(
    name="node_b",
    ipc_channel="perf_ipc_b",
    vsomeip_network="perf-node-b",
    unicast_ip=NODE_B_IP,
    someip_config=Path("tests/benchmarks/node_b_someip_config.bin"),
    mw_com_config=CONFIG_DIR / "node_b_mw_com_config.json",
    vsomeip_template=CONFIG_DIR / "vsomeip_node_b.json",
)


class PreflightError(RuntimeError):
    """Raised when the host is not set up for the test."""


def _is_local_address(address: str) -> bool:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        try:
            probe.bind((address, 0))
        except OSError:
            return False
    return True


def _has_sd_multicast_route() -> bool:
    if shutil.which("ip") is None:
        return True  # cannot tell, let the test fail later with the daemon logs
    routes = subprocess.run(
        ["ip", "route", "show"], capture_output=True, text=True, check=False
    ).stdout
    return any(line.startswith("224.") for line in routes.splitlines())


def _remove_stale_paths() -> None:
    for pattern in STALE_PATH_PATTERNS:
        for path in glob.glob(pattern):
            try:
                os.remove(path)
            except OSError:
                pass


def preflight(required_binaries: Sequence[Path] | None = None) -> None:
    """Validates the host setup and removes leftovers from a previous run."""
    binaries = required_binaries or (SOMEIPD, GATEWAYD, PERF_SENDER, PERF_RECEIVER)
    for binary in binaries:
        if not binary.exists():
            raise PreflightError(f"missing binary {binary} (cwd: {Path.cwd()})")

    for spec in (NODE_A, NODE_B):
        if not _is_local_address(spec.unicast_ip):
            raise PreflightError(
                f"{spec.name} needs the local address {spec.unicast_ip}, which "
                "tests/benchmarks/setup_network.sh could not configure. Run the test with "
                "--config=perf-tests."
            )

    if not _has_sd_multicast_route():
        raise PreflightError(
            f"no route for the SOME/IP-SD multicast address {SD_MULTICAST_ADDRESS}, which "
            "tests/benchmarks/setup_network.sh could not configure. Run the test with "
            "--config=perf-tests."
        )

    for name in ("someipd", "gatewayd", "perf_sender", "perf_receiver"):
        subprocess.run(["pkill", "-9", "-f", name], check=False)
    time.sleep(0.5)
    _remove_stale_paths()


def _render_vsomeip_config(spec: NodeSpec, workdir: Path) -> Path:
    config = json.loads(spec.vsomeip_template.read_text())
    config["unicast"] = spec.unicast_ip
    config["netmask"] = NETMASK
    config["network"] = spec.vsomeip_network
    config["logging"]["level"] = VSOMEIP_LOG_LEVEL
    rendered = workdir / f"vsomeip_{spec.name}.json"
    rendered.write_text(json.dumps(config, indent=4))
    return rendered


def _perf_record_argv(perf_data: Path, argv: Sequence[str]) -> list[str]:
    """Wraps argv so the process is profiled with `perf record` into `perf_data`."""
    return [PERF_BIN, "record", "--call-graph", "fp", "-o", str(perf_data), "--", *argv]


def _terminate_process_group(process: subprocess.Popen, sig: int) -> None:
    # Each spawned process is its own session leader (see Node._spawn), so signalling its group
    # also reaches a `perf record` wrapper's traced child, which does not get signals otherwise.
    try:
        os.killpg(process.pid, sig)
    except ProcessLookupError:
        pass


def _kill_traced_child(pid: int) -> bool:
    """Kills the direct child of `pid`, e.g. the process `perf record` traces.

    Killing the traced child (rather than `perf record` itself) lets perf observe a normal task
    exit and flush a valid perf.data file instead of being cut off mid-write. Returns whether a
    child was found and signalled.
    """
    try:
        children = Path(f"/proc/{pid}/task/{pid}/children").read_text().split()
    except (FileNotFoundError, ProcessLookupError):
        return False
    killed = False
    for child in children:
        try:
            os.kill(int(child), signal.SIGKILL)
            killed = True
        except (ProcessLookupError, ValueError):
            pass
    return killed


class Node:
    """Starts and stops the someipd/gatewayd pair of one node."""

    def __init__(
        self,
        spec: NodeSpec,
        workdir: Path,
        profile_dir: Path | None = None,
        shutdown_timeout: float = 5.0,
    ) -> None:
        self._spec = spec
        self._workdir = workdir
        self._profile_dir = profile_dir
        self._shutdown_timeout = shutdown_timeout
        self.perf_data_files: list[Path] = []
        self._processes: list[tuple[str, subprocess.Popen]] = []
        self._log_handles: list = []

    def __enter__(self) -> "Node":
        vsomeip_config = _render_vsomeip_config(self._spec, self._workdir)
        self._spawn(
            "someipd",
            [
                str(SOMEIPD.absolute()),
                "-c",
                str(self._spec.someip_config.absolute()),
                "-i",
                self._spec.ipc_channel,
            ],
            env={"VSOMEIP_CONFIGURATION": str(vsomeip_config.absolute())},
        )
        gatewayd_log = self._spawn(
            "gatewayd",
            [
                str(GATEWAYD.absolute()),
                "-c",
                str(self._spec.someip_config.absolute()),
                "-s",
                str(self._spec.mw_com_config.absolute()),
                "-i",
                self._spec.ipc_channel,
            ],
        )
        self._wait_for_marker(gatewayd_log, GATEWAYD_READY_MARKER)
        return self

    def __exit__(self, *_exc_info) -> None:
        for _, process in reversed(self._processes):
            _terminate_process_group(process, signal.SIGTERM)
        deadline = time.monotonic() + self._shutdown_timeout
        for _, process in reversed(self._processes):
            try:
                process.wait(timeout=max(0.1, deadline - time.monotonic()))
            except subprocess.TimeoutExpired:
                # someipd does not always act on SIGTERM before its blocking main loop returns.
                # Kill the traced child (if any, e.g. under `perf record`) first, so the wrapper
                # can still flush a valid file instead of being cut off by a hard kill itself.
                if not _kill_traced_child(process.pid):
                    _terminate_process_group(process, signal.SIGKILL)
                try:
                    process.wait(timeout=5.0)
                except subprocess.TimeoutExpired:
                    _terminate_process_group(process, signal.SIGKILL)
                    process.wait(timeout=5.0)
        for handle in self._log_handles:
            handle.close()
        _remove_stale_paths()

    def _spawn(
        self, name: str, argv: Sequence[str], env: dict[str, str] | None = None
    ) -> Path:
        log_path = self._workdir / f"{self._spec.name}_{name}.log"
        process_env = os.environ.copy()
        process_env.update(env or {})
        # Line buffering is not available for the child, so the readiness check tails the file.
        handle = log_path.open("wb")
        self._log_handles.append(handle)
        spawn_argv = list(argv)
        if self._profile_dir is not None:
            perf_data = self._profile_dir / f"{self._spec.name}_{name}.perf.data"
            self.perf_data_files.append(perf_data)
            spawn_argv = _perf_record_argv(perf_data, spawn_argv)
        process = subprocess.Popen(
            spawn_argv,
            stdout=handle,
            stderr=subprocess.STDOUT,
            env=process_env,
            start_new_session=True,
        )
        self._processes.append((name, process))
        return log_path

    def _wait_for_marker(
        self, log_path: Path, marker: str, timeout: float = 30.0
    ) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            for name, process in self._processes:
                if process.poll() is not None:
                    raise RuntimeError(
                        f"{self._spec.name} {name} exited with {process.returncode}:\n"
                        f"{log_path.read_text(errors='replace')}"
                    )
            if log_path.exists() and marker in log_path.read_text(errors="replace"):
                return
            time.sleep(0.1)
        raise TimeoutError(
            f"{self._spec.name} gatewayd did not report '{marker}' within {timeout}s:\n"
            f"{log_path.read_text(errors='replace')}"
        )


class PerfApp:
    """Runs perf_sender or perf_receiver in the background and collects its result JSON."""

    def __init__(
        self,
        binary: Path,
        workdir: Path,
        name: str,
        argv: Sequence[str],
        perf_data: Path | None = None,
    ) -> None:
        self._binary = binary
        self._name = name
        self.log_path = workdir / f"{name}.log"
        self.result_path = workdir / f"{name}_result.json"
        app_argv = [str(binary.absolute()), "-o", str(self.result_path), *argv]
        self._argv = _perf_record_argv(perf_data, app_argv) if perf_data else app_argv
        self._handle = self.log_path.open("wb")
        self._process = subprocess.Popen(
            self._argv, stdout=self._handle, stderr=subprocess.STDOUT
        )

    def wait(self, timeout: float) -> dict:
        try:
            self._process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            self._process.kill()
            self._process.wait(timeout=5.0)
            raise
        finally:
            self._handle.close()
        if not self.result_path.exists():
            raise RuntimeError(
                f"{self._name} exited with {self._process.returncode} and wrote no result:\n"
                f"{self.log_path.read_text(errors='replace')}"
            )
        return json.loads(self.result_path.read_text())


def wait_for_file(path: Path, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if path.exists():
            return
        time.sleep(0.05)
    raise TimeoutError(f"{path} was not created within {timeout}s")
