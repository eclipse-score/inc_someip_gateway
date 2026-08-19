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

"""Fixtures for the end-to-end performance test."""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Generator

import pytest

# The helper module lives next to this file, which is not on sys.path under the bazel runfiles.
sys.path.insert(0, str(Path(__file__).parent))

from nodes import NODE_A, NODE_B, Node, PreflightError, preflight  # noqa: E402


@pytest.fixture(scope="session")
def artifact_dir() -> Path:
    """Directory for daemon logs, rendered configs and result JSONs."""
    directory = Path(os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR", ".")) / "e2e_perf"
    directory.mkdir(parents=True, exist_ok=True)
    return directory


@pytest.fixture(scope="session")
def report(artifact_dir: Path) -> Generator[dict, None, None]:
    """Collects every measurement of the session into a single report file."""
    measurements: dict[str, dict] = {}
    yield measurements

    (artifact_dir / "e2e_perf_report.json").write_text(
        json.dumps(measurements, indent=2)
    )
    header = f"{'case':<32} {'msgs/s':>10} {'MB/s':>8} {'p50 us':>10} {'p99 us':>10} {'lost':>6}"
    print(f"\n{header}")
    for case, results in measurements.items():
        receiver = results["receiver"]
        latency = receiver["oneway_latency"]
        print(
            f"{case:<32} {receiver['throughput_msgs_per_s']:>10.0f} "
            f"{receiver['throughput_mb_per_s']:>8.2f} "
            f"{latency['p50_ns'] / 1000:>10.1f} {latency['p99_ns'] / 1000:>10.1f} "
            f"{receiver['lost']:>6}"
        )


@pytest.fixture(scope="session")
def nodes(artifact_dir: Path) -> Generator[Path, None, None]:
    """Starts both gatewayd/someipd pairs for the whole session."""
    try:
        preflight()
    except PreflightError as error:
        pytest.skip(str(error))

    with Node(NODE_A, artifact_dir), Node(NODE_B, artifact_dir):
        yield artifact_dir
