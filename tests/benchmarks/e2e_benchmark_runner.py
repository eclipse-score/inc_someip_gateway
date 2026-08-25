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

"""Shared runner for end-to-end IPC benchmark pytest targets."""

from __future__ import annotations

import os
import signal
import subprocess
from pathlib import Path

import pytest

try:
    from tests.benchmarks.nodes import (
        GATEWAYD,
        SOMEIPD,
        Node,
        NodeSpec,
        PreflightError,
        _kill_traced_child,
        _perf_record_argv,
        create_flamegraphs,
        flamegraph_tools,
        preflight,
    )
except ModuleNotFoundError:
    # Fallback for direct invocation where tests/benchmarks is on sys.path.
    from nodes import (  # type: ignore
        GATEWAYD,
        SOMEIPD,
        Node,
        NodeSpec,
        PreflightError,
        _kill_traced_child,
        _perf_record_argv,
        create_flamegraphs,
        flamegraph_tools,
        preflight,
    )

BENCHMARKS = Path("tests/benchmarks/ipc_benchmarks")
ECHO_SERVER = Path("tests/benchmarks/echo_server")
CONFIG_DIR = Path("tests/benchmarks/config")

NODE_A = NodeSpec(
    name="benchmark_node_a",
    ipc_channel="benchmark_ipc_a",
    vsomeip_network="benchmark-node-a",
    unicast_ip="127.0.0.2",
    someip_config=Path("tests/benchmarks/node_a_someip_config.bin"),
    mw_com_config=CONFIG_DIR / "node_a_mw_com_config.json",
    vsomeip_template=CONFIG_DIR / "vsomeip_node_a.json",
)
NODE_B = NodeSpec(
    name="benchmark_node_b",
    ipc_channel="benchmark_ipc_b",
    vsomeip_network="benchmark-node-b",
    unicast_ip="127.0.0.3",
    someip_config=Path("tests/benchmarks/node_b_someip_config.bin"),
    mw_com_config=CONFIG_DIR / "node_b_mw_com_config.json",
    vsomeip_template=CONFIG_DIR / "vsomeip_node_b.json",
)


def _terminate(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is not None:
        return
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def _terminate_profiled(process: subprocess.Popen[bytes]) -> None:
    """Terminates a perf-wrapped process while preserving perf.data flush."""
    if process.poll() is not None:
        return
    # Kill traced child first so perf can observe normal task exit and flush output.
    _ = _kill_traced_child(process.pid)
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=5)


def _flamegraph_scripts(runfiles_dir: Path) -> tuple[Path, Path] | None:
    """Returns Bazel-provided FlameGraph scripts when profiling is configured."""
    flamegraphs = list(runfiles_dir.glob("*/flamegraph.pl"))
    if not flamegraphs:
        return None
    flamegraph = flamegraphs[0]
    return flamegraph.with_name("stackcollapse-perf.pl"), flamegraph


def run_e2e_benchmarks(
    artifact_subdir: str,
    benchmark_filter: str | None = None,
) -> None:
    """Runs the benchmark binary through both gateway nodes.

    Args:
        artifact_subdir: Name under TEST_UNDECLARED_OUTPUTS_DIR for artifacts.
        benchmark_filter: Optional Google Benchmark filter expression.
    """
    runfiles_dir = Path(os.environ["TEST_SRCDIR"])
    scripts = _flamegraph_scripts(runfiles_dir)
    try:
        preflight((SOMEIPD, GATEWAYD, BENCHMARKS, ECHO_SERVER), (NODE_A, NODE_B))
        if scripts is not None:
            _ = flamegraph_tools(*scripts)
    except PreflightError as error:
        pytest.skip(str(error))

    artifact_dir = (
        Path(os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR", ".")) / artifact_subdir
    )
    artifact_dir.mkdir(parents=True, exist_ok=True)
    server_log = (artifact_dir / "echo_server.log").open("wb")
    server: subprocess.Popen[bytes] | None = None
    profile_dir = artifact_dir if scripts is not None else None
    benchmark_perf_data: Path | None = None
    echo_server_perf_data: Path | None = None
    node_a = Node(NODE_A, artifact_dir, profile_dir)
    node_b = Node(NODE_B, artifact_dir, profile_dir)

    benchmark_cmd = [
        str(BENCHMARKS.absolute()),
        "--service_instance_manifest",
        str(NODE_A.mw_com_config.absolute()),
        f"--benchmark_out={artifact_dir / 'benchmarks.json'}",
        "--benchmark_out_format=json",
    ]
    if benchmark_filter is not None:
        benchmark_cmd.append(f"--benchmark_filter={benchmark_filter}")

    server_cmd = [
        str(ECHO_SERVER.absolute()),
        "--service_instance_manifest",
        str(NODE_B.mw_com_config.absolute()),
    ]

    if profile_dir is not None:
        echo_server_perf_data = profile_dir / "echo_server.perf.data"
        benchmark_perf_data = profile_dir / "ipc_benchmarks.perf.data"
        server_cmd = _perf_record_argv(echo_server_perf_data, server_cmd)
        benchmark_cmd = _perf_record_argv(benchmark_perf_data, benchmark_cmd)

    try:
        with node_a, node_b:
            server = subprocess.Popen(
                server_cmd,
                stdout=server_log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            subprocess.run(
                benchmark_cmd,
                check=True,
                timeout=180,
            )
    finally:
        if server is not None:
            if profile_dir is not None:
                _terminate_profiled(server)
            else:
                _terminate(server)
        server_log.close()

    if scripts is not None:
        perf_data_files = [*node_a.perf_data_files, *node_b.perf_data_files]
        if echo_server_perf_data is not None:
            perf_data_files.append(echo_server_perf_data)
        if benchmark_perf_data is not None:
            perf_data_files.append(benchmark_perf_data)
        create_flamegraphs(perf_data_files, *scripts)
