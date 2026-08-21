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

"""Runs the Google Benchmark IPC suite through both SOME/IP gateway nodes."""

from __future__ import annotations

import os
import signal
import subprocess
import sys
from pathlib import Path

import pytest

sys.path.insert(0, str(Path("tests/benchmarks").absolute()))

from nodes import GATEWAYD, SOMEIPD, Node, NodeSpec, PreflightError, preflight  # noqa: E402

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


def test_e2e_benchmarks() -> None:
    """Benchmark a request/response pair that must traverse the SOME/IP link."""
    try:
        preflight((SOMEIPD, GATEWAYD, BENCHMARKS, ECHO_SERVER))
    except PreflightError as error:
        pytest.skip(str(error))

    artifact_dir = (
        Path(os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR", ".")) / "e2e_benchmarks"
    )
    artifact_dir.mkdir(parents=True, exist_ok=True)
    server_log = (artifact_dir / "echo_server.log").open("wb")
    server: subprocess.Popen[bytes] | None = None

    try:
        with Node(NODE_A, artifact_dir), Node(NODE_B, artifact_dir):
            server = subprocess.Popen(
                [
                    str(ECHO_SERVER.absolute()),
                    "--service_instance_manifest",
                    str(NODE_B.mw_com_config.absolute()),
                ],
                stdout=server_log,
                stderr=subprocess.STDOUT,
            )
            subprocess.run(
                [
                    str(BENCHMARKS.absolute()),
                    "--service_instance_manifest",
                    str(NODE_A.mw_com_config.absolute()),
                    f"--benchmark_out={artifact_dir / 'benchmarks.json'}",
                    "--benchmark_out_format=json",
                ],
                check=True,
                timeout=180,
            )
    finally:
        if server is not None:
            _terminate(server)
        server_log.close()
