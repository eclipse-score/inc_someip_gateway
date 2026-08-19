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

"""End-to-end performance tests across the full mw::com -> SOME/IP -> mw::com chain.

    perf_sender --mw::com--> gatewayd A --> someipd A --SOME/IP--> someipd B --> gatewayd B
                                                                              --mw::com--> perf_receiver

Both nodes run on the same host but are fully name-isolated, so the traffic really crosses the
network stack instead of short-circuiting through shared memory (see README.md).
"""

from __future__ import annotations

import itertools
from pathlib import Path

import pytest

from nodes import (
    NODE_A,
    NODE_B,
    PERF_RECEIVER,
    PERF_SENDER,
    PerfApp,
    wait_for_file,
)

PAYLOAD_SIZES = ("tiny", "small", "medium")
WARMUP = 100
MESSAGE_COUNT = 2000
APP_TIMEOUT_S = 120.0

# Deliberately loose: these guard against a broken pipeline, not against machine-to-machine
# performance differences.
MAX_ONEWAY_LATENCY_S = 0.5
MAX_ROUNDTRIP_LATENCY_S = 1.0
MAX_LOSS_RATIO = 0.02

_next_run_id = itertools.count(1)


def _run_case(
    artifact_dir: Path,
    report: dict,
    payload_size: str,
    mode: str,
    rate_hz: float,
    count: int = MESSAGE_COUNT,
) -> tuple[dict, dict]:
    case = f"{mode}_{payload_size}_{int(rate_hz)}hz"
    # A fresh run id per case, so samples still buffered from the previous case are ignored.
    run_id = str(next(_next_run_id))
    ready_file = artifact_dir / f"{case}_receiver_ready"
    ready_file.unlink(missing_ok=True)

    receiver = PerfApp(
        PERF_RECEIVER,
        artifact_dir,
        f"{case}_receiver",
        [
            "-s",
            str(NODE_B.mw_com_config.absolute()),
            "-p",
            payload_size,
            "-n",
            str(count),
            "-w",
            str(WARMUP),
            "-m",
            mode,
            "-t",
            str(APP_TIMEOUT_S - 10),
            "-I",
            run_id,
            "-R",
            str(ready_file),
        ],
    )
    wait_for_file(ready_file, timeout=60.0)

    sender = PerfApp(
        PERF_SENDER,
        artifact_dir,
        f"{case}_sender",
        [
            "-s",
            str(NODE_A.mw_com_config.absolute()),
            "-p",
            payload_size,
            "-n",
            str(count),
            "-r",
            str(rate_hz),
            "-w",
            str(WARMUP),
            "-m",
            mode,
            "-t",
            str(APP_TIMEOUT_S - 10),
            "-I",
            run_id,
        ],
    )

    sender_result = sender.wait(timeout=APP_TIMEOUT_S)
    receiver_result = receiver.wait(timeout=APP_TIMEOUT_S)
    report[case] = {"sender": sender_result, "receiver": receiver_result}
    return sender_result, receiver_result


@pytest.mark.parametrize("payload_size", PAYLOAD_SIZES)
def test_oneway_latency_and_throughput(
    nodes: Path, report: dict, payload_size: str
) -> None:
    """Sender publishes as fast as possible; measures one-way latency and throughput."""
    sender, receiver = _run_case(nodes, report, payload_size, "oneway", rate_hz=0)

    assert sender["sent"] == MESSAGE_COUNT
    assert sender["send_failures"] == 0
    assert receiver["corrupt"] == 0
    assert receiver["received"] > 0, "no message crossed the SOME/IP link"
    assert receiver["throughput_msgs_per_s"] > 0
    assert receiver["oneway_latency"]["p99_ns"] < MAX_ONEWAY_LATENCY_S * 1e9


@pytest.mark.parametrize("payload_size", PAYLOAD_SIZES)
def test_roundtrip_latency(nodes: Path, report: dict, payload_size: str) -> None:
    """Receiver echoes every message back through the second gateway chain."""
    sender, receiver = _run_case(
        nodes, report, payload_size, "roundtrip", rate_hz=500, count=500
    )

    assert receiver["corrupt"] == 0
    assert sender["responses_received"] > 0, (
        "no response came back through the gateway chain"
    )
    assert sender["roundtrip_latency"]["p99_ns"] < MAX_ROUNDTRIP_LATENCY_S * 1e9
    assert sender["roundtrip_latency"]["p50_ns"] >= receiver["oneway_latency"]["p50_ns"]


@pytest.mark.parametrize("payload_size", PAYLOAD_SIZES)
def test_sustained_rate_without_loss(
    nodes: Path, report: dict, payload_size: str
) -> None:
    """At a paced rate the chain must not drop messages."""
    sender, receiver = _run_case(
        nodes, report, payload_size, "oneway", rate_hz=500, count=1000
    )

    assert sender["sent"] == 1000
    assert receiver["corrupt"] == 0
    loss_ratio = receiver["lost"] / max(1, receiver["received"] + receiver["lost"])
    assert loss_ratio <= MAX_LOSS_RATIO, (
        f"lost {receiver['lost']} of {sender['sent']} messages"
    )
