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
import subprocess
from pathlib import Path

import pytest

try:
    from tests.benchmarks.nodes import (
        GATEWAYD,
        SOMEIPD,
        FlamegraphManager,
        Node,
        NodeSpec,
        PreflightError,
        create_flamegraph_manager,
        preflight,
    )
except ModuleNotFoundError:
    # Fallback for direct invocation where tests/benchmarks is on sys.path.
    from nodes import (  # type: ignore
        GATEWAYD,
        SOMEIPD,
        FlamegraphManager,
        Node,
        NodeSpec,
        PreflightError,
        create_flamegraph_manager,
        preflight,
    )

BENCHMARKS = Path("tests/benchmarks/ipc_benchmarks")
ECHO_SERVER = Path("tests/benchmarks/echo_server")
CONFIG_DIR = Path("tests/benchmarks/config")

BENCH_NODE = NodeSpec(
    name="bench",
    ipc_channel="benchmark_ipc_bench",
    vsomeip_network="benchmark-node-bench",
    unicast_ip="127.0.0.2",
    someip_config=Path("tests/benchmarks/bench_someip_config.bin"),
    mw_com_config=CONFIG_DIR / "bench" / "mw_com_config.json",
    vsomeip_template=CONFIG_DIR / "bench" / "vsomeip.json",
)
ECHO_NODE = NodeSpec(
    name="echo",
    ipc_channel="benchmark_ipc_echo",
    vsomeip_network="benchmark-node-echo",
    unicast_ip="127.0.0.3",
    someip_config=Path("tests/benchmarks/echo_someip_config.bin"),
    mw_com_config=CONFIG_DIR / "echo" / "mw_com_config.json",
    vsomeip_template=CONFIG_DIR / "echo" / "vsomeip.json",
)


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
    artifact_dir = Path(os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR", ".")) / artifact_subdir
    artifact_dir.mkdir(parents=True, exist_ok=True)
    flamegraph_manager: FlamegraphManager = create_flamegraph_manager(runfiles_dir, artifact_dir)
    try:
        preflight((SOMEIPD, GATEWAYD, BENCHMARKS, ECHO_SERVER), (BENCH_NODE, ECHO_NODE))
        flamegraph_manager.preflight()
    except PreflightError as error:
        pytest.skip(str(error))

    server_log = (artifact_dir / "echo_server.log").open("wb")
    server: subprocess.Popen[bytes] | None = None
    bench_node = Node(BENCH_NODE, artifact_dir, flamegraph_manager)
    echo_node = Node(ECHO_NODE, artifact_dir, flamegraph_manager)

    benchmark_cmd = [
        str(BENCHMARKS.absolute()),
        "--service_instance_manifest",
        str(BENCH_NODE.mw_com_config.absolute()),
        f"--benchmark_out={artifact_dir / 'benchmarks.json'}",
        "--benchmark_out_format=json",
    ]
    if benchmark_filter is not None:
        benchmark_cmd.append(f"--benchmark_filter={benchmark_filter}")

    server_cmd = [
        str(ECHO_SERVER.absolute()),
        "--service_instance_manifest",
        str(ECHO_NODE.mw_com_config.absolute()),
    ]

    server_cmd = flamegraph_manager.wrap_command("echo_server", server_cmd)
    benchmark_cmd = flamegraph_manager.wrap_command("ipc_benchmarks", benchmark_cmd)

    try:
        with bench_node, echo_node:
            server = subprocess.Popen(
                server_cmd,
                stdout=server_log,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            _ = subprocess.run(
                benchmark_cmd,
                check=True,
                timeout=180,
            )
    finally:
        if server is not None:
            flamegraph_manager.terminate(server)
        server_log.close()

    flamegraph_manager.create_flamegraphs()
