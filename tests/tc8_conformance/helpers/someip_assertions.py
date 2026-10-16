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
"""
SOME/IP assertion helpers for TC8 conformance tests.

Reusable assertion functions for SD entry fields and SOME/IP message headers.
"""

import ipaddress

from helpers.sd_helpers import RawEventgroupEntry
from helpers.someip_types import (
    IPv4EndpointOption,
    L4Protocols,
    SOMEIPHeader,
    SOMEIPMessageType,
    SOMEIPReturnCode,
    SOMEIPSDEntry,
    SOMEIPSDEntryType,
)


def assert_sd_offer_entry(
    entry: SOMEIPSDEntry,
    expected_service_id: int,
    expected_instance_id: int,
    expected_major_version: int = 0x00,
    expected_minor_version: int = 0x00000000,
) -> None:
    """Assert that an SD OFFER entry has the expected fields (TC8-SD-002)."""
    assert entry.sd_type == SOMEIPSDEntryType.OfferService, (
        f"TC8-SD-002: expected OfferService entry, got {entry.sd_type.name}"
    )

    assert entry.service_id == expected_service_id, (
        f"TC8-SD-002: service_id mismatch: got 0x{entry.service_id:04x}, expected 0x{expected_service_id:04x}"
    )

    assert entry.instance_id == expected_instance_id, (
        f"TC8-SD-002: instance_id mismatch: got 0x{entry.instance_id:04x}, expected 0x{expected_instance_id:04x}"
    )

    assert entry.major_version == expected_major_version, (
        f"TC8-SD-002: major_version mismatch: got 0x{entry.major_version:02x}, expected 0x{expected_major_version:02x}"
    )

    assert entry.service_minor_version == expected_minor_version, (
        f"TC8-SD-002: minor_version mismatch: "
        f"got 0x{entry.service_minor_version:08x}, "
        f"expected 0x{expected_minor_version:08x}"
    )

    # TTL=0 means StopOffer, so an OFFER entry's TTL must be > 0.
    assert entry.ttl > 0, f"TC8-SD-002: OFFER TTL must be > 0; got {entry.ttl}"


def assert_eventgroup_ack_entry(
    entry: RawEventgroupEntry,
    label: str,
    service_id: int,
    instance_id: int,
    major_version: int,
    eventgroup_id: int,
    ttl: int,
) -> None:
    """Assert a SubscribeAck entry (NAck when *ttl* is 0) echoes the subscription, with Reserved 0."""
    assert entry.entry_type == SOMEIPSDEntryType.SubscribeAck, (
        f"{label}: entry type 0x{entry.entry_type:02x}, expected SubscribeEventgroupAck (0x07)"
    )
    expected = (service_id, instance_id, major_version, eventgroup_id, ttl, 0)
    actual = (
        entry.service_id,
        entry.instance_id,
        entry.major_version,
        entry.eventgroup_id,
        entry.ttl,
        entry.reserved,
    )
    assert actual == expected, (
        f"{label}: Ack (service, instance, major, eventgroup, ttl, reserved) = "
        f"{tuple(hex(v) for v in actual)}, expected {tuple(hex(v) for v in expected)}"
    )


def assert_raw_ipv4_option(
    option: bytes,
    label: str,
    option_type: int,
    address: str,
    l4proto: int,
    port: int,
) -> None:
    """Assert a raw IPv4 option has Length 0x0009, both Reserved fields 0x00 and the given content."""
    assert len(option) == 12, f"{label}: option is {len(option)} bytes, expected 12"
    actual = (
        int.from_bytes(option[0:2], "big"),
        option[2],
        option[3],
        str(ipaddress.IPv4Address(option[4:8])),
        option[8],
        option[9],
        int.from_bytes(option[10:12], "big"),
    )
    expected = (0x0009, option_type, 0x00, address, 0x00, l4proto, port)
    assert actual == expected, (
        f"{label}: option (length, type, reserved, address, reserved, l4proto, port) = {actual}, expected {expected}"
    )


def assert_valid_response(resp: SOMEIPHeader, req_service_id: int, req_method_id: int) -> None:
    """Assert a SOME/IP RESPONSE has correct header fields."""
    assert resp.protocol_version == 1, f"TC8-MSG: protocol_version mismatch: got {resp.protocol_version}, expected 1"
    assert resp.message_type == SOMEIPMessageType.RESPONSE, (
        f"TC8-MSG: message_type mismatch: got {resp.message_type}, "
        f"expected RESPONSE (0x{SOMEIPMessageType.RESPONSE:02x})"
    )
    assert resp.service_id == req_service_id, (
        f"TC8-MSG: service_id mismatch: got 0x{resp.service_id:04x}, expected 0x{req_service_id:04x}"
    )
    assert resp.method_id == req_method_id, (
        f"TC8-MSG: method_id mismatch: got 0x{resp.method_id:04x}, expected 0x{req_method_id:04x}"
    )


def assert_return_code(resp: SOMEIPHeader, expected: SOMEIPReturnCode) -> None:
    """Assert a SOME/IP message has the expected return code."""
    assert resp.return_code == expected, (
        f"TC8-MSG: return_code mismatch: got 0x{resp.return_code:02x}, "
        f"expected {expected.name} (0x{expected.value:02x})"
    )


def assert_session_echo(resp: SOMEIPHeader, expected_session_id: int) -> None:
    """Assert RESPONSE session_id matches the REQUEST session_id."""
    assert resp.session_id == expected_session_id, (
        f"TC8-MSG: session_id mismatch: got 0x{resp.session_id:04x}, expected 0x{expected_session_id:04x}"
    )


def assert_client_echo(resp: SOMEIPHeader, expected_client_id: int) -> None:
    """Assert RESPONSE client_id matches the REQUEST client_id."""
    assert resp.client_id == expected_client_id, (
        f"TC8-MSG: client_id mismatch: got 0x{resp.client_id:04x}, expected 0x{expected_client_id:04x}"
    )


def assert_offer_has_ipv4_endpoint_option(
    entry: SOMEIPSDEntry,
    expected_ip: str,
    expected_port: int,
) -> None:
    """TC8-SD-011: OfferService entry must include an IPv4EndpointOption (UDP).

    Checks that the SD OFFER carries the correct unicast endpoint so a client
    can reach the service.  The address and port must match the DUT configuration.
    """
    options = list(getattr(entry, "options_1", ())) + list(getattr(entry, "options_2", ()))
    ipv4_opts = [o for o in options if isinstance(o, IPv4EndpointOption) and o.l4proto == L4Protocols.UDP]
    assert ipv4_opts, (
        "TC8-SD-011: No IPv4EndpointOption (UDP) found in OfferService entry options. "
        f"Entry has {len(options)} option(s): {options}"
    )
    opt = ipv4_opts[0]
    assert str(opt.address) == expected_ip, (
        f"TC8-SD-011: endpoint address mismatch: got {opt.address}, expected {expected_ip}"
    )
    assert opt.port == expected_port, f"TC8-SD-011: endpoint port mismatch: got {opt.port}, expected {expected_port}"


def assert_offer_has_tcp_endpoint_option(
    entry: SOMEIPSDEntry,
    expected_ip: str,
    expected_port: int,
) -> None:
    """Assert OfferService includes an IPv4EndpointOption with L4Proto=TCP.

    SOMEIPSRV_OPTIONS_15: the SD OfferService entry must advertise a TCP
    endpoint option when the service is configured with a reliable port.
    """
    options = list(getattr(entry, "options_1", ())) + list(getattr(entry, "options_2", ()))
    ipv4_opts = [o for o in options if isinstance(o, IPv4EndpointOption)]
    tcp_opts = [o for o in ipv4_opts if o.l4proto == L4Protocols.TCP]
    assert tcp_opts, (
        "SOMEIPSRV_OPTIONS_15: No IPv4EndpointOption with L4Proto=TCP found in "
        f"OfferService entry. Options present: {options}"
    )
    opt = tcp_opts[0]
    assert str(opt.address) == expected_ip, (
        f"SOMEIPSRV_OPTIONS_15: TCP endpoint address mismatch: got {opt.address}, expected {expected_ip}"
    )
    assert opt.port == expected_port, (
        f"SOMEIPSRV_OPTIONS_15: TCP endpoint port mismatch: got {opt.port}, expected {expected_port}"
    )
