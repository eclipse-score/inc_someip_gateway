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

import pytest
from attribute_plugin import add_test_properties

from event_exchange import someip_peer
from event_exchange.someip_peer import PeerConfig, SomeIpHostPeer


@add_test_properties(
    partially_verifies=["feat_req__some_ip_gateway__event_forwarding"],
    test_type="interface-test",
    derivation_technique="design-analysis",
)
def test_sd_eventgroup_counter_round_trip():
    """SD subscription entries preserve the counter and reject reserved bits."""
    peer = SomeIpHostPeer(PeerConfig(host_ip="127.0.0.1", dut_ip="127.0.0.1"))
    for entry_type in (someip_peer.SD_SUBSCRIBE, someip_peer.SD_SUBSCRIBE_ACK):
        entry = someip_peer._build_sd_entry(
            entry_type,
            service_id=0x1235,
            instance_id=1,
            major_version=1,
            ttl=30,
            eventgroup_id=0x8002,
            eventgroup_counter=0xA,
        )
        assert entry[12:14] == b"\x00\x0a"

        parsed_entries = someip_peer._parse_sd_datagram(peer._build_sd_packet(entry))
        assert len(parsed_entries) == 1
        assert parsed_entries[0].eventgroup_counter == 0xA
        assert parsed_entries[0].eventgroup_id == 0x8002

        malformed_entry = bytearray(entry)
        malformed_entry[12:14] = b"\x00\x1a"
        assert someip_peer._parse_sd_datagram(peer._build_sd_packet(bytes(malformed_entry))) == []


@add_test_properties(
    partially_verifies=["feat_req__some_ip_gateway__event_forwarding"],
    test_type="interface-test",
    derivation_technique="design-analysis",
)
def test_someip_parser_rejects_invalid_header_fields():
    """SOME/IP parsing rejects invalid interface versions and return codes."""
    valid_packet = someip_peer._someip_packet(0x1234, 0x8001, b"")

    invalid_interface_version = bytearray(valid_packet)
    invalid_interface_version[13] = 2
    with pytest.raises(ValueError):
        someip_peer._parse_someip(bytes(invalid_interface_version))

    invalid_return_code = bytearray(valid_packet)
    invalid_return_code[15] = 1
    with pytest.raises(ValueError):
        someip_peer._parse_someip(bytes(invalid_return_code))
