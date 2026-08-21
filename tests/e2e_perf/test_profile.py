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

"""Flamegraph profiling for the roundtrip/xlarge case of the end-to-end performance test.

Wraps every daemon and app process (someipd/gatewayd on both nodes, perf_sender,
perf_receiver) in `perf record` and renders the captures into flamegraph SVGs, so bottlenecks
across the full mw::com -> SOME/IP -> mw::com chain can be inspected visually.

This is a separate, single-case test target so profiling overhead is not paid by the whole
`e2e_perf` suite. Run with:

    bazel test --config=perf-tests-profile //tests/e2e_perf:e2e_perf_profile \
        --test_output=streamed
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

import pytest
from python.runfiles import runfiles

# The helper modules live next to this file, which is not on sys.path under the bazel runfiles.
sys.path.insert(0, str(Path(__file__).parent))

from nodes import (  # noqa: E402
    NODE_A,
    NODE_B,
    PERF_BIN,
    PERF_RECEIVER,
    PERF_SENDER,
    Node,
    PerfApp,
    PreflightError,
    preflight,
    wait_for_file,
)

PAYLOAD_SIZE = "xlarge"
WARMUP = 5
MESSAGE_COUNT = 50
APP_TIMEOUT_S = 120.0
RUNFILES = runfiles.Create()
STACK_COLLAPSE_PERF = Path(RUNFILES.Rlocation("flamegraph/stackcollapse-perf.pl"))
FLAMEGRAPH = Path(RUNFILES.Rlocation("flamegraph/flamegraph.pl"))


def _write_flamegraph(perf_data: Path, svg_path: Path) -> None:
    perf_script = subprocess.run(
        [PERF_BIN, "script", "-i", str(perf_data)],
        capture_output=True,
        text=True,
        check=False,
    )
    if perf_script.returncode != 0:
        raise RuntimeError(
            f"'perf script' failed for {perf_data}:\n{perf_script.stderr}"
        )

    folded = subprocess.run(
        [str(STACK_COLLAPSE_PERF)],
        input=perf_script.stdout,
        capture_output=True,
        text=True,
        check=True,
    )
    flamegraph = subprocess.run(
        [str(FLAMEGRAPH), f"--title={perf_data.stem}"],
        input=folded.stdout,
        capture_output=True,
        text=True,
        check=True,
    )
    svg_path.write_text(flamegraph.stdout)


def test_profile_roundtrip_xlarge() -> None:
    """Profiles one roundtrip/xlarge run of the full chain and writes flamegraph SVGs."""
    artifact_dir = (
        Path(os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR", ".")) / "e2e_perf_profile"
    )
    artifact_dir.mkdir(parents=True, exist_ok=True)

    try:
        preflight()
    except PreflightError as error:
        pytest.skip(str(error))

    ready_file = artifact_dir / "roundtrip_xlarge_receiver_ready"
    ready_file.unlink(missing_ok=True)
    receiver_perf_data = artifact_dir / "receiver.perf.data"
    sender_perf_data = artifact_dir / "sender.perf.data"

    with (
        Node(NODE_A, artifact_dir, profile_dir=artifact_dir) as node_a,
        Node(NODE_B, artifact_dir, profile_dir=artifact_dir) as node_b,
    ):
        receiver = PerfApp(
            PERF_RECEIVER,
            artifact_dir,
            "roundtrip_xlarge_receiver",
            [
                "-s",
                str(NODE_B.mw_com_config.absolute()),
                "-p",
                PAYLOAD_SIZE,
                "-n",
                str(MESSAGE_COUNT),
                "-w",
                str(WARMUP),
                "-m",
                "roundtrip",
                "-t",
                str(APP_TIMEOUT_S - 10),
                "-I",
                "profile-1",
                "-R",
                str(ready_file),
            ],
            perf_data=receiver_perf_data,
        )
        wait_for_file(ready_file, timeout=60.0)

        sender = PerfApp(
            PERF_SENDER,
            artifact_dir,
            "roundtrip_xlarge_sender",
            [
                "-s",
                str(NODE_A.mw_com_config.absolute()),
                "-p",
                PAYLOAD_SIZE,
                "-n",
                str(MESSAGE_COUNT),
                "-r",
                "0",
                "-w",
                str(WARMUP),
                "-m",
                "roundtrip",
                "-t",
                str(APP_TIMEOUT_S - 10),
                "-I",
                "profile-1",
            ],
            perf_data=sender_perf_data,
        )

        sender_result = sender.wait(timeout=APP_TIMEOUT_S)
        receiver_result = receiver.wait(timeout=APP_TIMEOUT_S)
        perf_data_files = (
            node_a.perf_data_files
            + node_b.perf_data_files
            + [receiver_perf_data, sender_perf_data]
        )

    (artifact_dir / "roundtrip_xlarge_result.json").write_text(
        json.dumps({"sender": sender_result, "receiver": receiver_result}, indent=2)
    )

    print(f"\nFlamegraphs written to {artifact_dir}:")
    for perf_data in perf_data_files:
        svg_path = perf_data.with_suffix(".svg")
        if not perf_data.exists():
            print(f"  {perf_data.name}: no perf data captured (perf record failed?)")
            continue
        _write_flamegraph(perf_data, svg_path)
        print(f"  {svg_path}")

    assert receiver_result["corrupt"] == 0
    assert sender_result["responses_received"] > 0
