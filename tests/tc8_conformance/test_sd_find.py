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
"""TC8-SDR-001 to SDR-003: unicast OfferService answers to FindService messages.

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import ipaddress
import socket
import struct
import subprocess
import time
from typing import Optional

from attribute_plugin import add_test_properties

from helpers.constants import (
    DUT_UNRELIABLE_PORT,
    INSTANCE_ID,
    MAJOR_VERSION,
    MINOR_VERSION,
    SD_MULTICAST_ADDR,
    SD_PORT,
    SERVICE_ID,
)
from helpers.sd_helpers import (
    IPV4_ENDPOINT_OPTION_TYPE,
    L4PROTO_OFFSET,
    PORT_OFFSET,
    offers_for_service,
    raw_entry_options,
)
from helpers.sd_sender import capture_sd_payload, open_sender_socket, send_find_service, send_sd_entries
from helpers.someip_types import IPv4EndpointOption, L4Protocols, SOMEIPSDEntry, SOMEIPSDEntryType

_CAPTURE_SECS: float = 10.0
_FIND_COUNT: int = 10
_FIND_INTERVAL_SECS: float = 0.1


def _capture_offer(sock: socket.socket) -> Optional[bytes]:
    """Return the first unicast SD payload on *sock* holding an OfferService for ``SERVICE_ID``."""
    return capture_sd_payload(sock, SOMEIPSDEntryType.OfferService, timeout_secs=_CAPTURE_SECS)


class TestSDFindResponse:
    """OfferService answers to FindService messages."""

    @add_test_properties(
        partially_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_060_discover_port_and_ip(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDR-001 (ETS_060): a multicast FindService is answered with a unicast Offer stating the UDP IP and port."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            send_find_service(sock, (SD_MULTICAST_ADDR, SD_PORT), service_id=SERVICE_ID, instance_id=INSTANCE_ID)
            payload = _capture_offer(sock)
        finally:
            sock.close()
        assert payload is not None, f"ETS_060: No unicast OfferService received within {_CAPTURE_SECS:.0f} s"

        offers = offers_for_service(payload, INSTANCE_ID)
        assert offers, f"ETS_060: unicast answer holds no OfferService for 0x{SERVICE_ID:04x}/0x{INSTANCE_ID:04x}"
        udp_endpoints = [
            (str(ipaddress.IPv4Address(o[4:8])), struct.unpack_from(">H", o, PORT_OFFSET)[0])
            for o in raw_entry_options(payload, offers[0])
            if o[2] == IPV4_ENDPOINT_OPTION_TYPE and o[L4PROTO_OFFSET] == L4Protocols.UDP
        ]
        assert (dut_ip, DUT_UNRELIABLE_PORT) in udp_endpoints, (
            f"ETS_060: UDP IPv4 Endpoint Options (IP, port) = {udp_endpoints}, expected ({dut_ip}, {DUT_UNRELIABLE_PORT})"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_118_ignore_options_in_findservice(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDR-002 (ETS_118): FindService messages with an IPv4 Endpoint Option are answered with a unicast Offer."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        find_entry = SOMEIPSDEntry(
            sd_type=SOMEIPSDEntryType.FindService,
            service_id=SERVICE_ID,
            instance_id=INSTANCE_ID,
            major_version=MAJOR_VERSION,
            ttl=3,
            minver_or_counter=MINOR_VERSION,
            options_1=(
                IPv4EndpointOption(
                    address=ipaddress.IPv4Address(tester_ip),
                    l4proto=L4Protocols.UDP,
                    port=sock.getsockname()[1],
                ),
            ),
        )
        try:
            for _ in range(_FIND_COUNT):
                send_sd_entries(sock, (dut_ip, SD_PORT), (find_entry,))
                time.sleep(_FIND_INTERVAL_SECS)
            payload = _capture_offer(sock)
        finally:
            sock.close()
        assert payload is not None, f"ETS_118: No unicast OfferService received within {_CAPTURE_SECS:.0f} s"
        assert offers_for_service(payload, INSTANCE_ID), (
            f"ETS_118: answer holds no OfferService for 0x{SERVICE_ID:04x}/0x{INSTANCE_ID:04x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_127_multicast_findservice(self, dut: subprocess.Popen[bytes], tester_ip: str) -> None:
        """TC8-SDR-003 (ETS_127): multicast FindService for a valid service and instance is answered with a unicast Offer."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            for _ in range(_FIND_COUNT):
                send_find_service(
                    sock,
                    (SD_MULTICAST_ADDR, SD_PORT),
                    service_id=SERVICE_ID,
                    instance_id=INSTANCE_ID,
                    major_version=MAJOR_VERSION,
                    minor_version=MINOR_VERSION,
                )
                time.sleep(_FIND_INTERVAL_SECS)
            payload = _capture_offer(sock)
        finally:
            sock.close()
        assert payload is not None, f"ETS_127: No unicast OfferService received within {_CAPTURE_SECS:.0f} s"
        assert offers_for_service(payload, INSTANCE_ID), (
            f"ETS_127: answer holds no OfferService for 0x{SERVICE_ID:04x}/0x{INSTANCE_ID:04x}"
        )
