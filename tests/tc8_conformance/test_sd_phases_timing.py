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
"""TC8 SD phase timing tests: TC8-SD-009.

Verifies that ``someipd`` goes through the Repetition Phase (short intervals)
before entering the Main Phase (long cyclic_offer_delay intervals).

See ``docs/architecture/tc8_conformance_testing.rst``.
"""

from typing import Generator, List, Tuple

import pytest

from attribute_plugin import add_test_properties

from helpers.dut_lifecycle import (
    launch_dut,
    render_someip_config,
    terminate_dut,
)
from helpers.constants import INSTANCE_ID, SERVICE_ID
from helpers.sd_helpers import open_multicast_socket
from helpers.timing import collect_sd_offers_from_socket
from helpers.someip_types import SOMEIPSDEntry

SOMEIP_CONFIG: str = "tc8_someipd_sd.json"

#: SD configuration values from ``tc8_someipd_sd.json``.
_CYCLIC_OFFER_DELAY_MS: float = 2000.0
_REPETITIONS_BASE_DELAY_MS: float = 200.0

#: OA ParamToleranceTimeMillisec (default 100 ms), applied to the second gap.
_TOLERANCE_MILLISEC_MS: float = 100.0

#: Capture timeout accounts for DUT startup, repetition phase, and one cyclic
#: gap. Theoretical minimum ~3.5 s (100 ms initial + 1.4 s reps + 2 s cyclic),
#: with generous margin for slow CI/container environments.
_PHASE_CAPTURE_TIMEOUT_SECS: float = 20.0


@pytest.fixture(scope="module")
def sd_phase_capture(
    tmp_path_factory: pytest.TempPathFactory,
    host_ip: str,
    request: pytest.FixtureRequest,
) -> Generator[List[Tuple[float, SOMEIPSDEntry]], None, None]:
    """Start a fresh someipd and capture timestamped SD offers.

    Opens the multicast socket BEFORE launching someipd to guarantee the
    first offer is captured.
    """
    target_init = request.getfixturevalue("target_init")

    # Ensure the guest SOME/IP configs are rendered (sed on QEMU guest).
    # Without this the guest /tc8_sd.json is absent or has literal placeholders,
    # causing the DUT to bind loopback and SD multicast to never reach the host.
    request.getfixturevalue("tc8_itf_config_setup")

    tmp_dir = tmp_path_factory.mktemp("tc8_phase_config")
    # Host-side render is harmless; launch_dut uses the filename to select
    # the pre-rendered guest config.
    config_path = render_someip_config(SOMEIP_CONFIG, host_ip, tmp_dir)

    # Open the socket BEFORE launching someipd so the first offer is captured.
    try:
        capture_sock = open_multicast_socket(host_ip)
    except OSError:
        pytest.skip(
            f"Multicast socket setup failed on {host_ip}. "
            "Set TC8_HOST_IP to a non-loopback IP or add a multicast route: "
            "sudo ip route add 224.0.0.0/4 dev lo"
        )

    proc = launch_dut(config_path, target_init=target_init)

    try:
        offers = collect_sd_offers_from_socket(
            capture_sock,
            count=5,  # initial offer + 3 reps + 1 cyclic
            timeout_secs=_PHASE_CAPTURE_TIMEOUT_SECS,
            instance_id=INSTANCE_ID,
        )
    except TimeoutError:
        offers = []  # tests will handle empty capture with appropriate assertions
    finally:
        capture_sock.close()

    terminate_dut(proc)

    yield offers


class TestSDPhasesTiming:
    """TC8-SD-009: Repetition Phase intervals."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_phases_timing"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_009_repetition_phase_intervals(
        self,
        sd_phase_capture: List[Tuple[float, SOMEIPSDEntry]],
    ) -> None:
        """TC8-SD-009: The first gap is a Repetition Phase gap, the second is twice the base delay."""
        service_offers = [(ts, e) for ts, e in sd_phase_capture if e.service_id == SERVICE_ID]
        assert len(service_offers) >= 3, "TC8-SD-009: Not enough OfferService entries captured for timing analysis"

        gaps_ms = [(service_offers[i + 1][0] - service_offers[i][0]) * 1000.0 for i in range(2)]
        cyclic_half_ms = _CYCLIC_OFFER_DELAY_MS * 0.5
        assert gaps_ms[0] < cyclic_half_ms, (
            f"TC8-SD-009: First inter-offer gap {gaps_ms[0]:.0f} ms >= {cyclic_half_ms:.0f} ms; "
            f"expected a Repetition Phase gap (< {cyclic_half_ms:.0f} ms)"
        )

        expected_ms = 2 * _REPETITIONS_BASE_DELAY_MS
        lower_ms = expected_ms - _TOLERANCE_MILLISEC_MS
        upper_ms = expected_ms + _TOLERANCE_MILLISEC_MS
        assert lower_ms < gaps_ms[1] < upper_ms, (
            f"TC8-SD-009: Second inter-offer gap {gaps_ms[1]:.0f} ms not in "
            f"({lower_ms:.0f}, {upper_ms:.0f}) ms; expected {expected_ms:.0f} ms "
            f"(2 x repetitions_base_delay {_REPETITIONS_BASE_DELAY_MS:.0f} ms) "
            f"+-{_TOLERANCE_MILLISEC_MS:.0f} ms. Measured gaps: {[round(g) for g in gaps_ms]} ms"
        )
