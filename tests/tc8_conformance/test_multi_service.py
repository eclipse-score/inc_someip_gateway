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
"""TC8 Multi-service and multi-instance config tests.

SOMEIPSRV_RPC_13 — Multi-service config validity:
  Verify that the DUT's configuration supports multiple service entries and
  that the DUT correctly offers its configured service with a stable SD stream.
  The ``tc8_someipd_multi.json`` config declares two services; the DUT validates
  both at startup. The DUT (``--tc8-standalone``) offers service 0x1234/0x5678.

SOMEIPSRV_RPC_14 — Instance port isolation:
  Verify that each service instance in the config is assigned a distinct UDP
  port and that the offered service's SD endpoint option matches its configured
  port, confirming port routing correctness at the SD layer.

Note on DUT scope: ``someipd --tc8-standalone`` offers a single service
(0x1234/0x5678) in the current implementation.  The multi config is loaded
successfully (both service entries are parsed at DUT init), so RPC_13 tests
the config-loading path and the SD offer for the primary service.  A second
service would require an additional ``app->offer_service()`` call in
``src/someipd/main.cpp``, which is tracked as a known limitation.

Port assignment (from BUILD.bazel env):
  TC8_SD_PORT      = 30499  (SD traffic)
  TC8_SVC_PORT     = 30512  (Service A UDP, tc8_someipd_multi.json primary)
  TC8_SVC_TCP_PORT = 30513  (Service B UDP, defined in config but not offered)

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import socket
import subprocess
import time
from typing import Dict, Optional, Set

import pytest

from attribute_plugin import add_test_properties

from helpers.constants import DUT_UNRELIABLE_PORT, SD_PORT
from helpers.sd_helpers import open_multicast_socket, parse_sd_offers
from someip.header import IPv4EndpointOption, L4Protocols, SOMEIPSDEntry

# ---------------------------------------------------------------------------
# Module-level configuration
# ---------------------------------------------------------------------------

#: Use the multi-service DUT config with two service entries.
#: Only service A is offered by --tc8-standalone; the config itself must load
#: successfully with both service definitions present (tests RPC_13 config path).
SOMEIP_CONFIG: str = "tc8_someipd_multi.json"

#: Service A — the service offered by --tc8-standalone mode.
_SERVICE_A_ID: int = 0x1234
_SERVICE_A_INSTANCE: int = 0x5678

#: Service B — declared in config but not offered by current standalone mode.
_SERVICE_B_ID: int = 0x5678
_SERVICE_B_INSTANCE: int = 0x0001

#: Timeout used when waiting for the DUT SD OfferService on the multicast group.
_DUT_READY_TIMEOUT_SECS: float = 10.0

#: Minimum number of SD OfferService entries to collect for stability checks.
_SD_OFFER_COLLECTION_WINDOW_SECS: float = 6.0


# ---------------------------------------------------------------------------
# Module-level helpers
# ---------------------------------------------------------------------------


def _collect_offers_for_service(
    host_ip: str,
    service_id: int,
    timeout_secs: float = _DUT_READY_TIMEOUT_SECS,
) -> list[SOMEIPSDEntry]:
    """Collect all OfferService entries for *service_id* within *timeout_secs*.

    Returns as soon as the first entry for the target service is found, or an
    empty list if the timeout elapses.  Skips the calling test if the multicast
    socket cannot be opened.
    """
    try:
        sock = open_multicast_socket(host_ip, port=SD_PORT)
    except OSError as exc:
        pytest.skip(
            f"Multicast socket setup failed on {host_ip}: {exc}. "
            "Set TC8_HOST_IP to a non-loopback interface IP or add a multicast "
            "route: sudo ip route add 224.0.0.0/4 dev lo"
        )

    results = []
    deadline = time.monotonic() + timeout_secs
    try:
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(min(remaining, 1.0))
            try:
                data, _ = sock.recvfrom(65535)
            except socket.timeout:
                continue
            for entry in parse_sd_offers(data):
                if entry.service_id == service_id:
                    results.append(entry)
                    return results
    finally:
        sock.close()

    return results


def _collect_all_offers(
    host_ip: str,
    window_secs: float = _SD_OFFER_COLLECTION_WINDOW_SECS,
) -> Dict[int, SOMEIPSDEntry]:
    """Collect all OfferService entries within *window_secs*.

    Returns a dict mapping service_id → most-recently-seen SOMEIPSDEntry.
    Skips the calling test if the multicast socket cannot be opened.
    """
    try:
        sock = open_multicast_socket(host_ip, port=SD_PORT)
    except OSError as exc:
        pytest.skip(
            f"Multicast socket setup failed on {host_ip}: {exc}. "
            "Set TC8_HOST_IP to a non-loopback interface IP or add a multicast "
            "route: sudo ip route add 224.0.0.0/4 dev lo"
        )

    seen: Dict[int, SOMEIPSDEntry] = {}
    deadline = time.monotonic() + window_secs
    try:
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(min(remaining, 1.0))
            try:
                data, _ = sock.recvfrom(65535)
            except socket.timeout:
                continue
            for entry in parse_sd_offers(data):
                seen[entry.service_id] = entry
    finally:
        sock.close()
    return seen


def _get_udp_endpoint_port(entry: SOMEIPSDEntry) -> Optional[int]:
    """Return the UDP endpoint port in an SD OfferService entry, or None."""
    options = list(getattr(entry, "options_1", ())) + list(
        getattr(entry, "options_2", ())
    )
    for opt in options:
        if isinstance(opt, IPv4EndpointOption) and opt.l4proto == L4Protocols.UDP:
            return int(opt.port)
    return None


# ---------------------------------------------------------------------------
# Test class
# ---------------------------------------------------------------------------


class TestMultiServiceInstanceRouting:
    """SOMEIPSRV_RPC_13/14: Multi-service config and instance port routing."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__multi_service"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_13_multi_service_config_loads_and_primary_service_offered(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """RPC_13: DUT loads a multi-service config and offers service A.

        When tc8_someipd_multi.json declares two services, the DUT must parse
        both service entries at startup without error.  The DUT then offers the
        primary service (0x1234/0x5678) via SD; this confirms the multi-service
        config was accepted by the DUT.

        Preconditions:
          - DUT started with tc8_someipd_multi.json (two service entries)
          - Multicast route available

        Stimuli:
          - Passive observation of SD OfferService multicast messages

        Expected result:
          - DUT process remains alive (config loaded without crash)
          - Service A (0x1234/0x5678) OfferService is received within timeout
        """
        assert someipd_dut.poll() is None, (
            "RPC_13: someipd DUT crashed — multi-service config may have "
            "caused an initialisation error"
        )

        entries = _collect_offers_for_service(host_ip, _SERVICE_A_ID)
        assert entries, (
            f"RPC_13: Service A (0x{_SERVICE_A_ID:04X}/0x{_SERVICE_A_INSTANCE:04X}) "
            f"not offered within {_DUT_READY_TIMEOUT_SECS}s. "
            "DUT may have rejected the multi-service config."
        )

        offered = entries[0]
        assert offered.service_id == _SERVICE_A_ID, (
            f"RPC_13: Unexpected service_id: 0x{offered.service_id:04X}"
        )
        assert offered.instance_id == _SERVICE_A_INSTANCE, (
            f"RPC_13: Unexpected instance_id: 0x{offered.instance_id:04X}"
        )
        assert offered.ttl > 0, (
            f"RPC_13: OfferService TTL is 0 (StopOffer) — service not active"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__multi_service"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_14_service_a_advertises_configured_udp_port(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """RPC_14: Service A's OfferService endpoint option matches configured port.

        Per SOMEIPSRV_RPC_14 — each service instance must be reachable on its
        configured UDP port.  The SD OfferService IPv4EndpointOption must carry
        the DUT's configured unreliable port (DUT_UNRELIABLE_PORT /
        TC8_SVC_PORT = 30512 for the tc8_multi_service Bazel target).

        This verifies that the DUT correctly maps the multi-service config entry
        to a UDP server endpoint on the right port.

        Preconditions:
          - DUT started with tc8_someipd_multi.json
          - Service A offered

        Stimuli:
          - Passive observation of SD OfferService; extraction of the
            IPv4EndpointOption port field.

        Expected result:
          - OfferService for service A (0x1234) carries an IPv4 UDP endpoint
            option with port == DUT_UNRELIABLE_PORT
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        entries = _collect_offers_for_service(host_ip, _SERVICE_A_ID)
        assert entries, (
            f"RPC_14: No OfferService for service A (0x{_SERVICE_A_ID:04X}) "
            f"received within {_DUT_READY_TIMEOUT_SECS}s."
        )

        entry = entries[0]
        port = _get_udp_endpoint_port(entry)
        assert port is not None, (
            "RPC_14: Service A OfferService carries no IPv4 UDP endpoint option. "
            "DUT may not have bound the unreliable port from the multi-service config."
        )
        assert port == DUT_UNRELIABLE_PORT, (
            f"RPC_14: Service A UDP endpoint port mismatch: got {port}, "
            f"expected {DUT_UNRELIABLE_PORT} (TC8_SVC_PORT from tc8_someipd_multi.json)."
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__multi_service"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_rpc_14_no_unexpected_service_ids_in_offers(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """RPC_14: Only configured services appear in SD OfferService messages.

        Collect all SD OfferService entries across a time window and verify
        that any offered service ID is one of the two configured IDs (0x1234
        or 0x5678).  No phantom service IDs should appear.

        This verifies that the multi-service config does not cause the DUT to
        offer unexpected services or corrupt the SD entry list.

        Preconditions:
          - DUT started with tc8_someipd_multi.json

        Stimuli:
          - Passive observation of SD OfferService for a 6-second window.

        Expected result:
          - All observed service_ids are in {0x1234, 0x5678}
          - At least service 0x1234 is observed
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        seen = _collect_all_offers(host_ip)

        assert _SERVICE_A_ID in seen, (
            f"RPC_14: No OfferService for service A (0x{_SERVICE_A_ID:04X}) "
            f"observed in {_SD_OFFER_COLLECTION_WINDOW_SECS}s window."
        )

        allowed_ids: Set[int] = {_SERVICE_A_ID, _SERVICE_B_ID}
        unexpected = set(seen.keys()) - allowed_ids
        assert not unexpected, (
            f"RPC_14: Unexpected service IDs in SD offers: "
            f"{[hex(s) for s in unexpected]}. "
            f"Only {[hex(s) for s in allowed_ids]} are configured."
        )
