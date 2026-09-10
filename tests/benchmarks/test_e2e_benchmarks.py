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

import sys
from pathlib import Path

sys.path.insert(0, str(Path(".").absolute()))

from tests.benchmarks.e2e_benchmark_runner import run_e2e_benchmarks  # noqa: E402


def test_e2e_benchmarks() -> None:
    """Benchmark a request/response pair that must traverse the SOME/IP link."""
    run_e2e_benchmarks(artifact_subdir="e2e_benchmarks")
