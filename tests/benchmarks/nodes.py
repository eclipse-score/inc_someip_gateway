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
import abc
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

GATEWAYD_READY_MARKER = "[gatewayd] IPC connection to someipd established"
SD_MULTICAST_ADDRESS = "224.244.224.245"

# Loopback addresses within the network namespace set up by setup_network.sh, so they cannot
# clash with the host.
NETMASK = "255.0.0.0"
VSOMEIP_LOG_LEVEL = os.environ.get("E2E_PERF_VSOMEIP_LOG_LEVEL", "warning")
PERF_BIN = os.environ.get("E2E_PERF_PERF_BIN", "perf")

STALE_PATH_PATTERNS = (
    "/tmp/perf-node-*",
    "/dev/shm*/perf_*",
    "/dev/shm*/counterpart_perf_*",
    # lola Linux
    "/dev/shm/lola-*",
    "/tmp/mw_com_lola/*",
    "/tmp/lola-*-*_lock",
    # lola QNX
    "/dev/shmem/lola-*",
    "/tmp_discovery/mw_com_lola/*",
    "/tmp_discovery/lola-*-*_lock",
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
    routes = subprocess.run(["ip", "route", "show"], capture_output=True, text=True, check=False).stdout
    return any(
        line.startswith(f"{SD_MULTICAST_ADDRESS} ") or line.startswith(f"{SD_MULTICAST_ADDRESS}/")
        for line in routes.splitlines()
    )


def _remove_stale_paths() -> None:
    for pattern in STALE_PATH_PATTERNS:
        for path in glob.glob(pattern):
            try:
                os.remove(path)
            except OSError:
                pass


def preflight(
    required_binaries: Sequence[Path],
    nodes: Sequence[NodeSpec],
) -> None:
    """Validates the host setup and removes leftovers from a previous run."""
    binaries = required_binaries or ()
    for binary in binaries:
        if not binary.exists():
            raise PreflightError(f"missing binary {binary} (cwd: {Path.cwd()})")

    for spec in nodes or ():
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

    for spec in nodes or ():
        subprocess.run(["pkill", "-9", "-f", spec.ipc_channel], check=False)
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


class FlamegraphManager(abc.ABC):
    """Encapsulates optional perf profiling and flamegraph generation."""

    @abc.abstractmethod
    def preflight(self) -> None:
        """Validates prerequisites for profiling and flamegraph generation."""

    @abc.abstractmethod
    def wrap_command(self, name: str, argv: Sequence[str]) -> list[str]:
        """Wraps a command for profiling under the given name, recording it for later use."""

    @abc.abstractmethod
    def terminate(self, process: subprocess.Popen[bytes]) -> None:
        """Terminates a process launched with wrap_command."""

    @abc.abstractmethod
    def create_flamegraphs(self) -> None:
        """Creates flamegraphs for all perf.data files recorded by wrap_command."""


class NoOpFlamegraphManager(FlamegraphManager):
    """No-op manager for runs without FlameGraph scripts."""

    def preflight(self) -> None:
        return

    def wrap_command(self, name: str, argv: Sequence[str]) -> list[str]:
        _ = name
        return list(argv)

    def terminate(self, process: subprocess.Popen[bytes]) -> None:
        if process.poll() is not None:
            return
        process.send_signal(signal.SIGTERM)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)

    def create_flamegraphs(self) -> None:
        return


class PerfFlamegraphManager(FlamegraphManager):
    """Flamegraph manager for perf profiling with FlameGraph scripts."""

    def __init__(self, stackcollapse: Path, flamegraph: Path, artifact_dir: Path) -> None:
        self._stackcollapse = stackcollapse
        self._flamegraph = flamegraph
        self._artifact_dir = artifact_dir
        self._perf_bin = ""
        self._perf_data_files: list[Path] = []

    def preflight(self) -> None:
        perf = shutil.which(PERF_BIN)
        if not perf or not self._stackcollapse.is_file() or not self._flamegraph.is_file():
            raise PreflightError(
                "flamegraph generation requires perf and the FlameGraph scripts supplied by "
                "--config=perf-tests-flamegraphs; see tests/benchmarks/README.md"
            )
        self._perf_bin = perf

    def wrap_command(self, name: str, argv: Sequence[str]) -> list[str]:
        perf_data = self._artifact_dir / f"{name}.perf.data"
        self._perf_data_files.append(perf_data)
        return [self._perf_bin, "record", "--call-graph", "fp", "-o", str(perf_data), "--", *argv]

    def terminate(self, process: subprocess.Popen[bytes]) -> None:
        """Terminates a perf-wrapped process while preserving perf.data flush."""
        if process.poll() is not None:
            return
        _ = _kill_traced_child(process.pid)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=5)

    def create_flamegraphs(self) -> None:
        if not self._perf_data_files:
            return
        self.preflight()
        for perf_data in self._perf_data_files:
            perf_script = subprocess.run(
                [self._perf_bin, "script", "-i", str(perf_data)],
                capture_output=True,
                check=True,
            )
            folded = subprocess.run(
                [str(self._stackcollapse)],
                input=perf_script.stdout,
                capture_output=True,
                check=True,
            )
            output = perf_data.with_suffix(".svg")
            with output.open("wb") as svg:
                subprocess.run(
                    [str(self._flamegraph), "--title", perf_data.stem],
                    input=folded.stdout,
                    stdout=svg,
                    check=True,
                )


def create_flamegraph_manager(runfiles_dir: Path, artifact_dir: Path) -> FlamegraphManager:
    """Creates a flamegraph manager based on Bazel-provided runfiles."""
    flamegraphs = list(runfiles_dir.glob("*/flamegraph.pl"))
    if not flamegraphs:
        return NoOpFlamegraphManager()
    flamegraph = flamegraphs[0]
    return PerfFlamegraphManager(flamegraph.with_name("stackcollapse-perf.pl"), flamegraph, artifact_dir)


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
        flamegraph_manager: FlamegraphManager,
    ) -> None:
        self._spec = spec
        self._workdir = workdir
        self._flamegraph_manager = flamegraph_manager
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
            self._flamegraph_manager.terminate(process)
        for handle in self._log_handles:
            handle.close()
        _remove_stale_paths()

    def _spawn(self, name: str, argv: Sequence[str], env: dict[str, str] | None = None) -> Path:
        log_path = self._workdir / f"{self._spec.name}_{name}.log"
        process_env = os.environ.copy()
        process_env.update(env or {})
        # Line buffering is not available for the child, so the readiness check tails the file.
        handle = log_path.open("wb")
        self._log_handles.append(handle)
        spawn_argv = self._flamegraph_manager.wrap_command(f"{self._spec.name}_{name}", list(argv))
        process = subprocess.Popen(
            spawn_argv,
            stdout=handle,
            stderr=subprocess.STDOUT,
            env=process_env,
            start_new_session=True,
        )
        self._processes.append((name, process))
        return log_path

    def _wait_for_marker(self, log_path: Path, marker: str, timeout: float = 30.0) -> None:
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
