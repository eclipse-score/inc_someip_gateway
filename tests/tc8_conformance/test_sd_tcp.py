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
"""TC8-SDT-001 and SDT-002: SD behavior of a service configured with a reliable (TCP) port.

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import struct
import subprocess

from attribute_plugin import add_test_properties

from helpers.constants import (
    EVENTGROUP_ALL_EVENTS_AND_FIELDS,
    INSTANCE_ID,
    MAJOR_VERSION,
    SD_PORT,
    SERVICE_ID,
)
from helpers.sd_helpers import (
    IPV4_ENDPOINT_OPTION_TYPE,
    L4PROTO_OFFSET,
    PORT_OFFSET,
    parse_raw_eventgroup_entry,
    raw_entry_options,
    raw_sd_entries,
)
from helpers.sd_sender import capture_sd_payload, open_sender_socket, send_find_service, send_subscribe_eventgroup
from helpers.someip_assertions import assert_eventgroup_ack_entry
from helpers.someip_types import L4Protocols, SOMEIPSDEntryType

#: Uses the service config, which has a reliable port.
SOMEIP_CONFIG: str = "tc8_someipd_service.json"


class TestSDTcpBinding:
    """SD messages for a service with a TCP endpoint."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_endpoint_option"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_15_tcp_endpoint_l4_protocol(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDT-001 (OPTIONS_15): the Offer holds one IPv4 Endpoint Option with Layer 4 protocol 0x06."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:

            def _send() -> None:
                send_find_service(sock, (dut_ip, SD_PORT), service_id=SERVICE_ID)

            _send()
            payload = capture_sd_payload(sock, SOMEIPSDEntryType.OfferService, timeout_secs=10.0, resend=_send)
        finally:
            sock.close()
        assert payload is not None, "OPTIONS_15: No OfferService received within 10 s after FindService"

        offer = next(
            e
            for e in raw_sd_entries(payload)
            if e[0] == SOMEIPSDEntryType.OfferService and struct.unpack_from(">H", e, 4)[0] == SERVICE_ID
        )
        endpoints = [o for o in raw_entry_options(payload, offer) if o[2] == IPV4_ENDPOINT_OPTION_TYPE]
        observed = [(o[L4PROTO_OFFSET], struct.unpack_from(">H", o, PORT_OFFSET)[0]) for o in endpoints]
        assert len(endpoints) == 1, (
            f"OPTIONS_15: expected 1 IPv4 Endpoint Option in the Offer, got {len(endpoints)}, "
            f"(l4proto, port) = {observed}"
        )
        assert endpoints[0][L4PROTO_OFFSET] == 0x06, (
            f"OPTIONS_15: Layer 4 protocol = 0x{endpoints[0][L4PROTO_OFFSET]:02x}, expected 0x06 (TCP)"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_096_subscribe_tcp_option_without_connection(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDT-002 (ETS_096): Subscribe for eventgroup 0x0002 with a TCP Endpoint Option, no TCP connection, is NAcked."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        port = sock.getsockname()[1]

        def _subscribe(ttl: int) -> None:
            send_subscribe_eventgroup(
                sock,
                (dut_ip, SD_PORT),
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_ALL_EVENTS_AND_FIELDS,
                MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=port,
                ttl=ttl,
                l4proto=L4Protocols.TCP,
            )

        try:
            _subscribe(3)
            payload = capture_sd_payload(
                sock, SOMEIPSDEntryType.SubscribeAck, timeout_secs=5.0, resend=lambda: _subscribe(3)
            )
        finally:
            _subscribe(0)
            sock.close()
        assert payload is not None, "ETS_096: No SubscribeEventgroupNAck received within 5 s"

        raw_ack = next(e for e in raw_sd_entries(payload) if e[0] == SOMEIPSDEntryType.SubscribeAck)
        assert_eventgroup_ack_entry(
            parse_raw_eventgroup_entry(raw_ack),
            "ETS_096",
            service_id=SERVICE_ID,
            instance_id=INSTANCE_ID,
            major_version=MAJOR_VERSION,
            eventgroup_id=EVENTGROUP_ALL_EVENTS_AND_FIELDS,
            ttl=0,
        )
