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
"""TC8-SDF-001 to SDF-021: format of the SD header, the OfferService, the SubscribeEventgroupAck entry and the notification header.

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import socket
import struct
import subprocess
import time
from typing import Iterator, Optional

import pytest

from attribute_plugin import add_test_properties

from helpers.constants import (
    EVENTGROUP_ALL_EVENTS_AND_FIELDS,
    INSTANCE_ID,
    MAJOR_VERSION,
    OFFER_TTL,
    SD_PORT,
    SERVICE_ID,
)
from helpers.event_helpers import subscribe_and_wait_ack
from helpers.sd_helpers import (
    SOMEIP_HEADER_LEN,
    RawEventgroupEntry,
    offers_for_service,
    open_multicast_socket,
    parse_raw_eventgroup_entry,
    raw_sd_entries,
)
from helpers.sd_sender import (
    capture_sd_payload,
    capture_some_ip_messages,
    open_sender_socket,
    send_find_service,
    send_subscribe_eventgroup,
)
from helpers.someip_types import SOMEIPSDEntryType

#: SD service and method IDs.
_SD_HEADER_ID: bytes = bytes.fromhex("ffff8100")

_REBOOT_AND_UNICAST_FLAGS: int = 0xC0


def _capture_offer_datagram(host_ip: str, timeout_secs: float = 10.0) -> Optional[bytes]:
    """Return the first raw SD datagram on the SD multicast group that offers ``SERVICE_ID``."""
    sock = open_multicast_socket(host_ip)
    try:
        deadline = time.monotonic() + timeout_secs
        while (remaining := deadline - time.monotonic()) > 0:
            sock.settimeout(min(remaining, 0.5))
            try:
                data, _ = sock.recvfrom(65535)
            except socket.timeout:
                continue
            if data[:4] == _SD_HEADER_ID and len(data) >= SOMEIP_HEADER_LEN + 8:
                if offers_for_service(data[SOMEIP_HEADER_LEN:]):
                    return data
    finally:
        sock.close()
    return None


@pytest.fixture(scope="class")
def offer_datagram(dut: subprocess.Popen[bytes], host_ip: str) -> bytes:
    """Raw multicast SD datagram of an OfferService for ``SERVICE_ID``."""
    assert dut.poll() is None, "DUT is not running"
    data = _capture_offer_datagram(host_ip)
    assert data is not None, "No SD OfferService for the service received within 10 s"
    return data


@pytest.fixture(scope="class")
def find_response_payload(dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> bytes:
    """SD payload of the unicast answer to a FindService for ``SERVICE_ID``."""
    assert dut.poll() is None, "DUT is not running"
    sock = open_sender_socket(tester_ip)
    try:

        def _send() -> None:
            send_find_service(sock, (dut_ip, SD_PORT), service_id=SERVICE_ID)

        _send()
        payload = capture_sd_payload(sock, SOMEIPSDEntryType.OfferService, timeout_secs=5.0, resend=_send)
    finally:
        sock.close()
    assert payload is not None, "No unicast OfferService received within 5 s after FindService"
    assert offers_for_service(payload), "The answer holds no OfferService entry for the service"
    return payload


@pytest.fixture(scope="class")
def ack_payload(find_response_payload: bytes, dut_ip: str, tester_ip: str) -> Iterator[bytes]:
    """SD payload of the SubscribeEventgroupAck answering a Subscribe with TTL = ``OFFER_TTL``."""
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
        )

    try:
        _subscribe(OFFER_TTL)
        payload = capture_sd_payload(
            sock, SOMEIPSDEntryType.SubscribeAck, timeout_secs=5.0, resend=lambda: _subscribe(OFFER_TTL)
        )
        assert payload is not None, "No SubscribeEventgroupAck received within 5 s"
        yield payload
    finally:
        _subscribe(0)
        sock.close()


_SD_ENTRIES_OFFSET: int = 8  # flags and reserved (4) plus Entries Array length (4)


def _entries_length_from_datagram(payload: bytes) -> int:
    """Entries Array length implied by the SD payload size: whole payload minus the options array and the length fields.

    Only the Options Array length field is read, at the offset the Entries Array length field announces, so a wrong
    Entries Array length shifts the read and breaks the size equation.
    """
    announced = struct.unpack_from(">I", payload, 4)[0]
    options_length = struct.unpack_from(">I", payload, _SD_ENTRIES_OFFSET + announced)[0]
    return len(payload) - _SD_ENTRIES_OFFSET - 4 - options_length


def _ack_entry(payload: bytes) -> RawEventgroupEntry:
    """Return the first SubscribeEventgroupAck entry of an SD payload."""
    raw = next(e for e in raw_sd_entries(payload) if e[0] == SOMEIPSDEntryType.SubscribeAck)
    return parse_raw_eventgroup_entry(raw)


class TestSDFormat:
    """TC8-SDF-001 to SDF-007: SD header fields of messages sent by the DUT."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_01_client_id(self, offer_datagram: bytes) -> None:
        """TC8-SDF-001 (FORMAT_01): Client ID is 0x0000."""
        client_id = struct.unpack_from(">H", offer_datagram, 8)[0]
        assert client_id == 0x0000, f"FORMAT_01: Client ID = 0x{client_id:04x}, expected 0x0000"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_03_protocol_version(self, offer_datagram: bytes) -> None:
        """TC8-SDF-002 (FORMAT_03): Protocol Version is 0x01."""
        assert offer_datagram[12] == 0x01, f"FORMAT_03: Protocol Version = 0x{offer_datagram[12]:02x}, expected 0x01"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_05_message_type(self, offer_datagram: bytes) -> None:
        """TC8-SDF-003 (FORMAT_05): Message Type is 0x02."""
        assert offer_datagram[14] == 0x02, f"FORMAT_05: Message Type = 0x{offer_datagram[14]:02x}, expected 0x02"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_06_return_code(self, offer_datagram: bytes) -> None:
        """TC8-SDF-004 (FORMAT_06): Return Code is 0x00."""
        assert offer_datagram[15] == 0x00, f"FORMAT_06: Return Code = 0x{offer_datagram[15]:02x}, expected 0x00"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_08_unicast_flag(self, offer_datagram: bytes) -> None:
        """TC8-SDF-005 (FORMAT_08): Unicast Flag bit is 1."""
        flags = offer_datagram[SOMEIP_HEADER_LEN]
        assert flags & 0x40, f"FORMAT_08: Unicast Flag not set, Flags = 0x{flags:02x}"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_09_undefined_flag_bits(self, offer_datagram: bytes) -> None:
        """TC8-SDF-006 (FORMAT_09): The six Flag bits other than Reboot and Unicast are 0."""
        flags = offer_datagram[SOMEIP_HEADER_LEN]
        assert flags & ~_REBOOT_AND_UNICAST_FLAGS & 0xFF == 0, (
            f"FORMAT_09: undefined Flag bits set, Flags = 0x{flags:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_10_reserved_bits(self, offer_datagram: bytes) -> None:
        """TC8-SDF-007 (FORMAT_10): Reserved is 0x000000."""
        reserved = offer_datagram[SOMEIP_HEADER_LEN + 1 : SOMEIP_HEADER_LEN + 4].hex()
        assert reserved == "000000", f"FORMAT_10: Reserved = 0x{reserved}, expected 0x000000"


class TestSDOfferEntryFormat:
    """TC8-SDF-008 to SDF-011: fields of the OfferService (Type 1) entry."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_11_type_1_entry_length(self, offer_datagram: bytes) -> None:
        """TC8-SDF-008 (FORMAT_11): Entry Type is 0x01 and the Entries Array length is entries x 16."""
        payload = offer_datagram[SOMEIP_HEADER_LEN:]
        entries_length = struct.unpack_from(">I", payload, 4)[0]
        expected = _entries_length_from_datagram(payload)
        assert entries_length == expected, f"FORMAT_11: Entries Array length = {entries_length}, expected {expected}"
        assert offers_for_service(payload), "FORMAT_11: no entry with Entry Type 0x01 for the service"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_12_type_1_index_first_option_run(self, find_response_payload: bytes) -> None:
        """TC8-SDF-009 (FORMAT_12): Index First Option Run of the OfferService entry is 0."""
        for entry in offers_for_service(find_response_payload):
            assert entry[1] == 0, f"FORMAT_12: Index First Option Run = {entry[1]}, expected 0"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_13_type_1_number_of_options_1(self, find_response_payload: bytes) -> None:
        """TC8-SDF-010 (FORMAT_13): Number of Options 1 of the OfferService entry is at least 1."""
        for entry in offers_for_service(find_response_payload):
            count = entry[3] >> 4
            assert count >= 1, f"FORMAT_13: Number of Options 1 = {count}, expected at least 1"

    @add_test_properties(
        partially_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_07_offer_ttl(self, find_response_payload: bytes) -> None:
        """TC8-SDF-011 (SD_MESSAGE_07, steps 1 to 5): TTL of the OfferService entry equals the configured TTL."""
        for entry in offers_for_service(find_response_payload):
            ttl = struct.unpack_from(">I", entry, 8)[0] & 0xFFFFFF
            assert ttl == OFFER_TTL, f"SD_MESSAGE_07: TTL = {ttl}, expected the configured {OFFER_TTL}"


class TestSDSubscribeAckFormat:
    """TC8-SDF-012 to SDF-020: fields of the SubscribeEventgroupAck (Type 2) entry."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_19_type_2_entry_type(self, ack_payload: bytes) -> None:
        """TC8-SDF-012 (FORMAT_19): Entry Type of the SubscribeEventgroupAck is 0x07."""
        raw = ack_payload[_SD_ENTRIES_OFFSET:]
        assert raw[0] == 0x07, f"FORMAT_19: Entry Type = 0x{raw[0]:02x}, expected 0x07"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_20_type_2_entry_length(self, ack_payload: bytes) -> None:
        """TC8-SDF-013 (FORMAT_20): Entries Array length is entries x 16."""
        entries_length = struct.unpack_from(">I", ack_payload, 4)[0]
        expected = _entries_length_from_datagram(ack_payload)
        assert entries_length == expected, f"FORMAT_20: Entries Array length = {entries_length}, expected {expected}"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_21_type_2_index_first_option_run(self, ack_payload: bytes) -> None:
        """TC8-SDF-014 (FORMAT_21): Index First Option Run of the SubscribeEventgroupAck is 0."""
        raw = next(e for e in raw_sd_entries(ack_payload) if e[0] == SOMEIPSDEntryType.SubscribeAck)
        assert raw[1] == 0, f"FORMAT_21: Index First Option Run = {raw[1]}, expected 0"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_23_type_2_service_id(self, ack_payload: bytes) -> None:
        """TC8-SDF-015 (FORMAT_23): Service ID of the SubscribeEventgroupAck is the subscribed service."""
        service_id = _ack_entry(ack_payload).service_id
        assert service_id == SERVICE_ID, f"FORMAT_23: Service ID = 0x{service_id:04x}, expected 0x{SERVICE_ID:04x}"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_24_type_2_instance_id(self, ack_payload: bytes) -> None:
        """TC8-SDF-016 (FORMAT_24): Instance ID of the SubscribeEventgroupAck is the subscribed instance."""
        instance_id = _ack_entry(ack_payload).instance_id
        assert instance_id == INSTANCE_ID, f"FORMAT_24: Instance ID = 0x{instance_id:04x}, expected 0x{INSTANCE_ID:04x}"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_25_type_2_major_version(self, ack_payload: bytes) -> None:
        """TC8-SDF-017 (FORMAT_25): Major Version of the SubscribeEventgroupAck is that of the service."""
        major = _ack_entry(ack_payload).major_version
        assert major == MAJOR_VERSION, f"FORMAT_25: Major Version = 0x{major:02x}, expected 0x{MAJOR_VERSION:02x}"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_26_type_2_ttl(self, ack_payload: bytes) -> None:
        """TC8-SDF-018 (FORMAT_26): TTL of the SubscribeEventgroupAck equals the TTL of the subscription."""
        ttl = _ack_entry(ack_payload).ttl
        assert ttl == OFFER_TTL, f"FORMAT_26: TTL = {ttl}, expected {OFFER_TTL}"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_27_type_2_reserved(self, ack_payload: bytes) -> None:
        """TC8-SDF-019 (FORMAT_27): Reserved field of the SubscribeEventgroupAck is 0x0000."""
        raw = next(e for e in raw_sd_entries(ack_payload) if e[0] == SOMEIPSDEntryType.SubscribeAck)
        reserved = raw[12:14].hex()
        assert reserved == "0000", f"FORMAT_27: Reserved = 0x{reserved}, expected 0x0000"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_28_type_2_eventgroup_id(self, ack_payload: bytes) -> None:
        """TC8-SDF-020 (FORMAT_28): Eventgroup ID of the SubscribeEventgroupAck is the subscribed eventgroup."""
        eventgroup_id = _ack_entry(ack_payload).eventgroup_id
        assert eventgroup_id == EVENTGROUP_ALL_EVENTS_AND_FIELDS, (
            f"FORMAT_28: Eventgroup ID = 0x{eventgroup_id:04x}, expected 0x{EVENTGROUP_ALL_EVENTS_AND_FIELDS:04x}"
        )


class TestNotificationFormat:
    """TC8-SDF-021: header of a notification sent by the DUT."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_04_interface_version(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDF-021 (FORMAT_04): Interface Version of a notification sent by the DUT is 0x01."""
        assert dut.poll() is None, "DUT is not running"
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.bind((tester_ip, 0))
        sd_sock = subscribe_and_wait_ack(
            tester_ip,
            dut_ip,
            SD_PORT,
            SERVICE_ID,
            INSTANCE_ID,
            EVENTGROUP_ALL_EVENTS_AND_FIELDS,
            MAJOR_VERSION,
            notif_sock.getsockname()[1],
            ttl=OFFER_TTL,
        )
        try:
            notifications = capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=10.0)
        finally:
            sd_sock.close()
            notif_sock.close()
        assert notifications, "FORMAT_04: no notification received within 10 s after the subscription"
        for msg in notifications:
            assert msg.interface_version == 0x01, (
                f"FORMAT_04: Interface Version = 0x{msg.interface_version:02x}, expected 0x01"
            )
