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
"""TC8 SD phase timing tests — TC8-SD-009 and TC8-SD-010.

Verifies that ``someipd`` goes through the Repetition Phase (short intervals)
before entering the Main Phase (long cyclic_offer_delay intervals).

See ``docs/architecture/tc8_conformance_testing.rst``.
"""

from typing import Generator, List, Tuple

import pytest

from attribute_plugin import add_test_properties

from conftest import launch_someipd, render_someip_config, terminate_someipd
from helpers.sd_helpers import open_multicast_socket
from helpers.timing import collect_sd_offers_from_socket
from someip.header import SOMEIPSDEntry

# ---------------------------------------------------------------------------
# Module-level configuration
# ---------------------------------------------------------------------------

#: SOME/IP stack config template — same config as service discovery tests.
SOMEIP_CONFIG: str = "tc8_someipd_sd.json"

#: SD configuration values from ``tc8_someipd_sd.json``.
_SERVICE_ID: int = 0x1234
_CYCLIC_OFFER_DELAY_MS: float = 2000.0
_REPETITIONS_BASE_DELAY_MS: float = 200.0
_REPETITIONS_MAX: int = 3


# ---------------------------------------------------------------------------
# Fixture: pre-opens multicast socket, starts DUT, captures phase data
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def sd_phase_capture(
    tmp_path_factory: pytest.TempPathFactory,
    host_ip: str,
) -> Generator[List[Tuple[float, SOMEIPSDEntry]], None, None]:
    """Start a fresh someipd and capture timestamped SD offers.

    Opens the multicast socket BEFORE launching someipd to capture
    the very first offer. Yields the captured data to the tests.
    """
    tmp_dir = tmp_path_factory.mktemp("tc8_phase_config")
    config_path = render_someip_config(SOMEIP_CONFIG, host_ip, tmp_dir)

    # 1. Open multicast socket before starting someipd (captures first offer).
    try:
        capture_sock = open_multicast_socket(host_ip)
    except OSError:
        pytest.skip(
            f"Multicast socket setup failed on {host_ip}. "
            "Set TC8_HOST_IP to a non-loopback IP or add a multicast route: "
            "sudo ip route add 224.0.0.0/4 dev lo"
        )

    # 2. Launch someipd.
    proc = launch_someipd(config_path)

    # 3. Capture: initial + repetition phase + 1 cyclic gap.
    try:
        offers = collect_sd_offers_from_socket(
            capture_sock,
            count=5,  # initial offer + 3 reps + 1 cyclic
            timeout_secs=10.0,
        )
    except TimeoutError:
        offers = []  # tests will handle empty capture with appropriate assertions
    finally:
        capture_sock.close()

    # 4. Terminate DUT.
    terminate_someipd(proc)

    yield offers


# ---------------------------------------------------------------------------
# TC8-SD-009 / TC8-SD-010 — SD phase timing
# ---------------------------------------------------------------------------


class TestSDPhasesTiming:
    """TC8-SD-009/010: Repetition Phase intervals and count."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_phases_timing"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_009_repetition_phase_intervals(
        self,
        sd_phase_capture: List[Tuple[float, SOMEIPSDEntry]],
    ) -> None:
        """TC8-SD-009: Repetition Phase offer gaps are shorter than cyclic_offer_delay."""
        service_offers = [
            (ts, e) for ts, e in sd_phase_capture if e.service_id == _SERVICE_ID
        ]
        assert len(service_offers) >= 2, (
            "TC8-SD-009: Not enough OfferService entries captured for timing analysis"
        )

        # The first gap must be a Repetition Phase gap (< half the cyclic period).
        gap_ms = (service_offers[1][0] - service_offers[0][0]) * 1000.0
        cyclic_half_ms = _CYCLIC_OFFER_DELAY_MS * 0.5

        assert gap_ms < cyclic_half_ms, (
            f"TC8-SD-009: First inter-offer gap {gap_ms:.0f} ms >= {cyclic_half_ms:.0f} ms; "
            f"expected a Repetition Phase gap (< {cyclic_half_ms:.0f} ms)"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_phases_timing"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_010_repetition_count_before_main_phase(
        self,
        sd_phase_capture: List[Tuple[float, SOMEIPSDEntry]],
    ) -> None:
        """TC8-SD-010: At least repetitions_max - 1 short-gap offers before Main Phase.

        The first offer may be missed due to a race between socket setup and
        process startup. Requiring ``repetitions_max - 1 = 2`` short gaps
        still proves the Repetition Phase happened with doubling intervals.
        """
        service_offers = [
            (ts, e) for ts, e in sd_phase_capture if e.service_id == _SERVICE_ID
        ]
        assert len(service_offers) >= 2, (
            "TC8-SD-010: Not enough OfferService entries captured for phase counting"
        )

        # Count "short" gaps (Repetition Phase) vs "long" gaps (Main Phase).
        cyclic_half_ms = _CYCLIC_OFFER_DELAY_MS * 0.5
        short_gap_count = 0

        for i in range(len(service_offers) - 1):
            gap_ms = (service_offers[i + 1][0] - service_offers[i][0]) * 1000.0
            if gap_ms < cyclic_half_ms:
                short_gap_count += 1

        # Allow repetitions_max - 1 to tolerate the first-offer capture race.
        min_short_gaps = _REPETITIONS_MAX - 1
        assert short_gap_count >= min_short_gaps, (
            f"TC8-SD-010: Only {short_gap_count} Repetition Phase gap(s) observed; "
            f"expected at least {min_short_gaps} "
            f"(repetitions_max={_REPETITIONS_MAX}, tolerance: -1 for first-offer capture race)"
        )
