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
"""TC8 SD Format & Options Field Assertions — FORMAT_01 through OPTIONS_14.

Verifies byte-level field values in SD OfferService messages and
SubscribeEventgroup Ack responses from someipd.  No DUT behaviour is
triggered beyond normal SD operation; tests only observe what the DUT
already sends.

Test classes
------------
TestSdHeaderFieldsOfferService  — FORMAT_01/02/04/05/06/09/10
TestSdOfferEntryFields          — FORMAT_11/12/13/15/16/18
TestSdHeaderFieldsSubscribeAck  — FORMAT_19/20/21/23/24/25/26/27/28
TestSdOptionsEndpoint           — OPTIONS_01/02/03/05/06
TestSdOptionsMulticast          — OPTIONS_08/09/10/11/12/13/14
"""

import ipaddress
import socket
import subprocess
import time
from typing import Optional, Tuple

import pytest

from attribute_plugin import add_test_properties

from helpers.constants import DUT_UNRELIABLE_PORT, SD_MULTICAST_ADDR, SD_PORT
from helpers.sd_helpers import open_multicast_socket
from helpers.sd_sender import (
    capture_unicast_sd_entries,
    open_sender_socket,
    send_subscribe_eventgroup,
)
from someip.header import (
    IPv4EndpointOption,
    IPv4MulticastOption,
    L4Protocols,
    SD_INTERFACE_VERSION,
    SD_METHOD,
    SD_SERVICE,
    SOMEIPHeader,
    SOMEIPMessageType,
    SOMEIPReturnCode,
    SOMEIPSDEntry,
    SOMEIPSDEntryType,
    SOMEIPSDHeader,
)

# ---------------------------------------------------------------------------
# Module-level configuration
# ---------------------------------------------------------------------------

#: SOME/IP stack config template used for all tests in this module.
SOMEIP_CONFIG: str = "tc8_someipd_sd.json"

#: Service and instance IDs declared in ``tc8_someipd_sd.json``.
_SERVICE_ID: int = 0x1234
_INSTANCE_ID: int = 0x5678
_EVENTGROUP_ID: int = 0x4455  # UDP unicast eventgroup
_MULTICAST_EVENTGROUP_ID: int = 0x4465  # multicast eventgroup

#: Defaults — no version configured in tc8_someipd_sd.json.
_MAJOR_VERSION: int = 0x00
_MINOR_VERSION: int = 0x00000000

#: Multicast config values from ``tc8_someipd_sd.json``.
_MULTICAST_ADDR: str = "239.0.0.1"
_MULTICAST_PORT: int = 40490


# ---------------------------------------------------------------------------
# Internal capture helpers
# ---------------------------------------------------------------------------


def _capture_raw_sd_offer(
    host_ip: str,
    timeout_secs: float = 5.0,
) -> Tuple[SOMEIPHeader, SOMEIPSDHeader, SOMEIPSDEntry, SOMEIPSDEntry]:
    """Capture the first raw OfferService for ``_SERVICE_ID`` on the multicast group.

    Returns a 4-tuple:
      - raw SOME/IP header
      - resolved SD header (options populated in entries)
      - resolved offer entry (``options_1`` / ``options_2`` populated)
      - unresolved offer entry (``num_options_1`` / ``option_index_1`` populated)

    Callers that only need the resolved entry may use ``result[2]``; callers
    that need raw option-count fields should use ``result[3]``.

    Raises ``TimeoutError`` when nothing arrives within *timeout_secs*.
    """
    sock = open_multicast_socket(host_ip)
    try:
        deadline = time.monotonic() + timeout_secs
        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            sock.settimeout(min(remaining, 1.0))
            try:
                data, _ = sock.recvfrom(65535)
            except socket.timeout:
                continue
            try:
                someip_msg, _ = SOMEIPHeader.parse(data)
                if someip_msg.service_id != SD_SERVICE:
                    continue
                sd_hdr_raw, _ = SOMEIPSDHeader.parse(someip_msg.payload)
                sd_hdr_resolved = sd_hdr_raw.resolve_options()
                for raw_entry, resolved_entry in zip(
                    sd_hdr_raw.entries, sd_hdr_resolved.entries
                ):
                    if (
                        resolved_entry.sd_type == SOMEIPSDEntryType.OfferService
                        and resolved_entry.service_id == _SERVICE_ID
                    ):
                        return someip_msg, sd_hdr_resolved, resolved_entry, raw_entry
            except Exception:  # noqa: BLE001
                continue
    finally:
        sock.close()

    raise TimeoutError(
        f"No OfferService for service 0x{_SERVICE_ID:04x} received within "
        f"{timeout_secs:.1f}s on {host_ip}:{SD_PORT}"
    )


def _capture_subscribe_ack(
    host_ip: str,
    tester_ip: str,
    eventgroup_id: int,
    timeout_secs: float = 5.0,
) -> SOMEIPSDEntry:
    """Subscribe to *eventgroup_id* and capture the SubscribeEventgroupAck entry.

    Raises ``AssertionError`` when no matching ack arrives.
    """
    sock = open_sender_socket(tester_ip)
    try:
        sender_port: int = sock.getsockname()[1]

        def _send_sub() -> None:
            send_subscribe_eventgroup(
                sock,
                (host_ip, SD_PORT),
                _SERVICE_ID,
                _INSTANCE_ID,
                eventgroup_id,
                _MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=sender_port,
            )

        _send_sub()
        entries = capture_unicast_sd_entries(
            sock,
            filter_types=(SOMEIPSDEntryType.SubscribeAck,),
            timeout_secs=timeout_secs,
            resend=_send_sub,
            max_results=1,
        )
        acks = [e for e in entries if e.eventgroup_id == eventgroup_id and e.ttl > 0]
        assert acks, (
            f"No SubscribeEventgroupAck received for eventgroup "
            f"0x{eventgroup_id:04x} within {timeout_secs:.1f}s"
        )
        return acks[0]
    finally:
        sock.close()


def _capture_subscribe_ack_with_options(
    host_ip: str,
    tester_ip: str,
    eventgroup_id: int,
    timeout_secs: float = 5.0,
) -> Tuple[SOMEIPSDEntry, SOMEIPSDEntry]:
    """Subscribe to *eventgroup_id* and return (unresolved_ack, resolved_ack).

    The unresolved entry comes from ``capture_unicast_sd_entries``;
    the resolved entry is from a second pass that calls ``resolve_options()``.
    """
    sock = open_sender_socket(tester_ip)
    try:
        sender_port: int = sock.getsockname()[1]

        def _send_sub() -> None:
            send_subscribe_eventgroup(
                sock,
                (host_ip, SD_PORT),
                _SERVICE_ID,
                _INSTANCE_ID,
                eventgroup_id,
                _MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=sender_port,
            )

        _send_sub()
        deadline = time.monotonic() + timeout_secs
        next_resend = time.monotonic() + 1.5

        while time.monotonic() < deadline:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            if time.monotonic() >= next_resend:
                _send_sub()
                next_resend = time.monotonic() + 1.5
            sock.settimeout(min(remaining, 0.5))
            try:
                data, _ = sock.recvfrom(65535)
            except socket.timeout:
                continue
            try:
                someip_msg, _ = SOMEIPHeader.parse(data)
                if someip_msg.service_id != SD_SERVICE:
                    continue
                sd_hdr, _ = SOMEIPSDHeader.parse(someip_msg.payload)
                # Resolved copy (options populated).
                sd_hdr_resolved = sd_hdr.resolve_options()
                for unresolved_entry, resolved_entry in zip(
                    sd_hdr.entries, sd_hdr_resolved.entries
                ):
                    if (
                        unresolved_entry.sd_type == SOMEIPSDEntryType.SubscribeAck
                        and unresolved_entry.eventgroup_id == eventgroup_id
                        and unresolved_entry.ttl > 0
                    ):
                        return unresolved_entry, resolved_entry
            except Exception:  # noqa: BLE001
                continue
    finally:
        sock.close()

    raise AssertionError(
        f"No SubscribeEventgroupAck with options received for eventgroup "
        f"0x{eventgroup_id:04x} within {timeout_secs:.1f}s"
    )


# ---------------------------------------------------------------------------
# TestSdHeaderFieldsOfferService — FORMAT_01/02/04/05/06/09/10
# ---------------------------------------------------------------------------


class TestSdHeaderFieldsOfferService:
    """FORMAT_01/02/04/05/06/09/10: SOME/IP header fields of an OfferService SD message."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_01_client_id_is_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_01: SOME/IP SD header client_id must be 0x0000."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        someip_msg, _, _, _ = _capture_raw_sd_offer(host_ip)

        assert someip_msg.client_id == 0x0000, (
            f"FORMAT_01: client_id must be 0x0000; got 0x{someip_msg.client_id:04x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_02_session_id_is_nonzero_and_in_range(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_02: SD session_id must be non-zero and fit in 16 bits.

        Per PRS_SOMEIPSD_00154 the session_id starts at 0x0001 and
        increments with each SD message; the value 0x0000 is reserved.  Because
        the module-scoped DUT may have already sent earlier SD messages (during
        preceding test classes), the captured value may be greater than 1, but
        it must never be 0 and must not exceed 0xFFFF.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        someip_msg, _, _, _ = _capture_raw_sd_offer(host_ip, timeout_secs=5.0)

        assert someip_msg.session_id != 0x0000, (
            "FORMAT_02: session_id must never be 0x0000 (reserved)"
        )
        assert someip_msg.session_id <= 0xFFFF, (
            f"FORMAT_02: session_id must fit in 16 bits; "
            f"got 0x{someip_msg.session_id:08x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_04_interface_version_is_one(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_04: SOME/IP SD interface_version must be 0x01."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        someip_msg, _, _, _ = _capture_raw_sd_offer(host_ip)

        assert someip_msg.interface_version == SD_INTERFACE_VERSION, (
            f"FORMAT_04: interface_version must be {SD_INTERFACE_VERSION}; "
            f"got {someip_msg.interface_version}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_05_message_type_is_notification(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_05: SOME/IP SD message_type must be NOTIFICATION (0x02)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        someip_msg, _, _, _ = _capture_raw_sd_offer(host_ip)

        assert someip_msg.message_type == SOMEIPMessageType.NOTIFICATION, (
            f"FORMAT_05: message_type must be NOTIFICATION "
            f"(0x{SOMEIPMessageType.NOTIFICATION:02x}); "
            f"got 0x{someip_msg.message_type:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_06_return_code_is_e_ok(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_06: SOME/IP SD return_code must be E_OK (0x00)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        someip_msg, _, _, _ = _capture_raw_sd_offer(host_ip)

        assert someip_msg.return_code == SOMEIPReturnCode.E_OK, (
            f"FORMAT_06: return_code must be E_OK "
            f"(0x{SOMEIPReturnCode.E_OK:02x}); "
            f"got 0x{someip_msg.return_code:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_09_sd_flags_reserved_bits_are_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_09: Reserved bits (5-0) of the SD Flags byte must be 0."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, sd_hdr, _, _ = _capture_raw_sd_offer(host_ip)

        # flags_unknown captures bits 5-0 (the reserved/undefined bits).
        assert sd_hdr.flags_unknown == 0, (
            f"FORMAT_09: SD Flags reserved bits (5-0) must be 0; "
            f"flags_unknown=0x{sd_hdr.flags_unknown:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_10_sd_entry_reserved_bytes_are_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_10: Reserved bytes in the 16-byte OfferService SD entry must be 0.

        SOME/IP-SD entry layout (16 bytes):
          [0]    type
          [1]    index_first_option_run  (0 when no options before type-1 entries)
          [2]    index_second_option_run (reserved in OfferService, must be 0)
          [3]    high nibble = num_options_run1, low nibble = num_options_run2
          [4-5]  service_id
          [6-7]  instance_id
          [8]    major_version
          [9-11] TTL (3 bytes)
          [12-15] minor_version (4 bytes)

        Byte [2] (index_second_option_run) is reserved for Type-1 OfferService
        entries and must be transmitted as 0.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        # Assign option index so the entry can be built.
        assigned = entry.assign_option_index([])
        raw = assigned.build()

        # Byte [2] is reserved for OfferService and must be 0.
        reserved_byte = raw[2]
        assert reserved_byte == 0, (
            f"FORMAT_10: Reserved byte [2] in OfferService SD entry must be 0; "
            f"got 0x{reserved_byte:02x}"
        )


# ---------------------------------------------------------------------------
# TestSdOfferEntryFields — FORMAT_11/12/13/15/16/18
# ---------------------------------------------------------------------------


class TestSdOfferEntryFields:
    """FORMAT_11/12/13/15/16/18: OfferService SD entry field assertions."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_11_entry_is_16_bytes(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_11: Each SD entry must be exactly 16 bytes."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        assigned = entry.assign_option_index([])
        entry_bytes = assigned.build()

        assert len(entry_bytes) == 16, (
            f"FORMAT_11: SD entry must be 16 bytes; got {len(entry_bytes)}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_12_first_option_run_index_is_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_12: OfferService first option run index must be 0."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        assigned = entry.assign_option_index([])
        raw = assigned.build()

        # Byte [1] holds option_index_1.
        option_index_1 = raw[1]
        assert option_index_1 == 0, (
            f"FORMAT_12: option_index_1 (byte [1]) must be 0; "
            f"got 0x{option_index_1:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_13_num_options_matches_options_list(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_13: num_options_1 field must equal the number of resolved options."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, raw_entry = _capture_raw_sd_offer(host_ip)

        # After resolve_options() the ``num_options_1`` field is None (the library
        # replaces the counter with the actual option objects).  Use the raw entry's
        # counter (preserved before resolve) against the resolved entry's options list.
        assert raw_entry.num_options_1 == len(entry.options_1), (
            f"FORMAT_13: num_options_1 ({raw_entry.num_options_1}) must equal "
            f"len(options_1) ({len(entry.options_1)})"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_15_instance_id_matches_config(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_15: OfferService instance_id must match the configured value."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        assert entry.instance_id == _INSTANCE_ID, (
            f"FORMAT_15: instance_id must be 0x{_INSTANCE_ID:04x}; "
            f"got 0x{entry.instance_id:04x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_16_major_version_matches_config(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_16: OfferService major_version must match the configured value."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        assert entry.major_version == _MAJOR_VERSION, (
            f"FORMAT_16: major_version must be 0x{_MAJOR_VERSION:02x}; "
            f"got 0x{entry.major_version:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_18_minor_version_matches_config(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_18: OfferService minor_version must match the configured value."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        assert entry.service_minor_version == _MINOR_VERSION, (
            f"FORMAT_18: minor_version must be 0x{_MINOR_VERSION:08x}; "
            f"got 0x{entry.service_minor_version:08x}"
        )


# ---------------------------------------------------------------------------
# TestSdHeaderFieldsSubscribeAck — FORMAT_19/20/21/23/24/25/26/27/28
# ---------------------------------------------------------------------------


class TestSdHeaderFieldsSubscribeAck:
    """FORMAT_19/20/21/23/24/25/26/27/28: SubscribeEventgroupAck entry field assertions."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_19_ack_entry_type_is_subscribe_ack(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """FORMAT_19: SubscribeEventgroupAck SD entry type must be 0x07."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        ack = _capture_subscribe_ack(host_ip, tester_ip, _EVENTGROUP_ID)

        assert ack.sd_type == SOMEIPSDEntryType.SubscribeAck, (
            f"FORMAT_19: sd_type must be SubscribeAck (0x07); got {ack.sd_type!r}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_20_ack_entry_is_16_bytes(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """FORMAT_20: SubscribeEventgroupAck SD entry must be exactly 16 bytes."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        ack = _capture_subscribe_ack(host_ip, tester_ip, _EVENTGROUP_ID)

        assigned = ack.assign_option_index([])
        entry_bytes = assigned.build()

        assert len(entry_bytes) == 16, (
            f"FORMAT_20: SubscribeAck SD entry must be 16 bytes; got {len(entry_bytes)}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_21_ack_option_run_index_is_zero_when_no_options(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """FORMAT_21: SubscribeAck option_index_1 matches the attached options."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        ack = _capture_subscribe_ack(host_ip, tester_ip, _EVENTGROUP_ID)

        # When no options are present, option_index_1 must be 0.
        # When options are present, option_index_1 must be a valid index.
        assigned = ack.assign_option_index([])
        raw = assigned.build()
        option_index_1 = raw[1]
        expected_option_count = ack.num_options_1

        if expected_option_count == 0:
            assert option_index_1 == 0, (
                f"FORMAT_21: option_index_1 must be 0 when no options; "
                f"got 0x{option_index_1:02x}"
            )
        else:
            # Non-zero index is valid only when the SD packet carries options.
            assert option_index_1 < 16, (
                f"FORMAT_21: option_index_1 must be a valid index (< 16); "
                f"got 0x{option_index_1:02x}"
            )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_23_ack_service_id_matches_config(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """FORMAT_23: SubscribeAck service_id must match the subscribed service."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        ack = _capture_subscribe_ack(host_ip, tester_ip, _EVENTGROUP_ID)

        assert ack.service_id == _SERVICE_ID, (
            f"FORMAT_23: service_id must be 0x{_SERVICE_ID:04x}; "
            f"got 0x{ack.service_id:04x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_24_ack_instance_id_matches_config(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """FORMAT_24: SubscribeAck instance_id must match the subscribed instance."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        ack = _capture_subscribe_ack(host_ip, tester_ip, _EVENTGROUP_ID)

        assert ack.instance_id == _INSTANCE_ID, (
            f"FORMAT_24: instance_id must be 0x{_INSTANCE_ID:04x}; "
            f"got 0x{ack.instance_id:04x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_25_ack_major_version_matches_config(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """FORMAT_25: SubscribeAck major_version must match the service definition."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        ack = _capture_subscribe_ack(host_ip, tester_ip, _EVENTGROUP_ID)

        assert ack.major_version == _MAJOR_VERSION, (
            f"FORMAT_25: major_version must be 0x{_MAJOR_VERSION:02x}; "
            f"got 0x{ack.major_version:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_26_ack_ttl_is_nonzero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """FORMAT_26: SubscribeEventgroupAck TTL must be > 0 (TTL=0 means Nack)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        ack = _capture_subscribe_ack(host_ip, tester_ip, _EVENTGROUP_ID)

        assert ack.ttl > 0, f"FORMAT_26: SubscribeAck TTL must be > 0; got {ack.ttl}"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_27_ack_reserved_field_is_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """FORMAT_27: Reserved bits (high 12 bits of last 4 bytes) of SubscribeAck must be 0.

        In the 16-byte SD entry for SubscribeEventgroupAck:
          bytes [12-13]: high 4 bits = reserved counter (must be 0)
          bytes [14-15]: eventgroup_id

        The ``minver_or_counter`` field encodes ``(reserved << 16) | eventgroup_id``.
        The high 16 bits must be 0.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        ack = _capture_subscribe_ack(host_ip, tester_ip, _EVENTGROUP_ID)

        reserved_high = (ack.minver_or_counter >> 16) & 0xFFFF
        assert reserved_high == 0, (
            f"FORMAT_27: High 16 bits of SubscribeAck minver_or_counter must be 0; "
            f"got 0x{reserved_high:04x} (minver_or_counter=0x{ack.minver_or_counter:08x})"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_28_ack_eventgroup_id_matches_subscribe(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """FORMAT_28: SubscribeAck eventgroup_id must match the subscribed eventgroup."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        ack = _capture_subscribe_ack(host_ip, tester_ip, _EVENTGROUP_ID)

        assert (ack.minver_or_counter & 0xFFFF) == _EVENTGROUP_ID, (
            f"FORMAT_28: eventgroup_id in SubscribeAck must be "
            f"0x{_EVENTGROUP_ID:04x}; "
            f"got 0x{ack.minver_or_counter & 0xFFFF:04x}"
        )


# ---------------------------------------------------------------------------
# TestSdOptionsEndpoint — OPTIONS_01/02/03/05/06
# ---------------------------------------------------------------------------


class TestSdOptionsEndpoint:
    """OPTIONS_01/02/03/05/06: IPv4EndpointOption field assertions from OfferService."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_01_endpoint_option_length_is_nine(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """OPTIONS_01: IPv4EndpointOption length field must be 0x0009."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        ipv4_opts = [o for o in entry.options_1 if isinstance(o, IPv4EndpointOption)]
        assert ipv4_opts, (
            "OPTIONS_01: No IPv4EndpointOption found in OfferService entry"
        )
        opt = ipv4_opts[0]
        raw = opt.build()

        # Wire format: [0-1] length field (big-endian).
        length_field = int.from_bytes(raw[0:2], "big")
        assert length_field == 0x0009, (
            f"OPTIONS_01: IPv4EndpointOption length field must be 0x0009; "
            f"got 0x{length_field:04x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_02_endpoint_option_type_is_0x04(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """OPTIONS_02: IPv4EndpointOption type byte must be 0x04."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        ipv4_opts = [o for o in entry.options_1 if isinstance(o, IPv4EndpointOption)]
        assert ipv4_opts, (
            "OPTIONS_02: No IPv4EndpointOption found in OfferService entry"
        )
        opt = ipv4_opts[0]
        raw = opt.build()

        # Wire format: [2] type byte.
        type_byte = raw[2]
        assert type_byte == 0x04, (
            f"OPTIONS_02: IPv4EndpointOption type byte must be 0x04; "
            f"got 0x{type_byte:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_03_endpoint_option_reserved_after_type_is_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """OPTIONS_03: Reserved byte at offset [3] of IPv4EndpointOption must be 0x00."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        ipv4_opts = [o for o in entry.options_1 if isinstance(o, IPv4EndpointOption)]
        assert ipv4_opts, (
            "OPTIONS_03: No IPv4EndpointOption found in OfferService entry"
        )
        opt = ipv4_opts[0]
        raw = opt.build()

        # Wire format: [3] reserved byte after type.
        reserved_byte = raw[3]
        assert reserved_byte == 0x00, (
            f"OPTIONS_03: IPv4EndpointOption reserved byte [3] must be 0x00; "
            f"got 0x{reserved_byte:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_05_endpoint_option_reserved_before_protocol_is_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """OPTIONS_05: Reserved byte at offset [8] of IPv4EndpointOption must be 0x00."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        ipv4_opts = [o for o in entry.options_1 if isinstance(o, IPv4EndpointOption)]
        assert ipv4_opts, (
            "OPTIONS_05: No IPv4EndpointOption found in OfferService entry"
        )
        opt = ipv4_opts[0]
        raw = opt.build()

        # Wire format: [8] reserved byte before protocol byte.
        reserved_byte = raw[8]
        assert reserved_byte == 0x00, (
            f"OPTIONS_05: IPv4EndpointOption reserved byte [8] must be 0x00; "
            f"got 0x{reserved_byte:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_06_endpoint_option_protocol_is_udp(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """OPTIONS_06: IPv4EndpointOption L4 protocol must be UDP (0x11)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        ipv4_opts = [o for o in entry.options_1 if isinstance(o, IPv4EndpointOption)]
        assert ipv4_opts, (
            "OPTIONS_06: No IPv4EndpointOption found in OfferService entry"
        )
        opt = ipv4_opts[0]

        assert opt.l4proto == L4Protocols.UDP, (
            f"OPTIONS_06: IPv4EndpointOption l4proto must be UDP "
            f"(0x{L4Protocols.UDP:02x}); got {opt.l4proto!r}"
        )


# ---------------------------------------------------------------------------
# TestSdOptionsMulticast — OPTIONS_08/09/10/11/12/13/14
# ---------------------------------------------------------------------------


class TestSdOptionsMulticast:
    """OPTIONS_08/09/10/11/12/13/14: IPv4MulticastOption field assertions from SubscribeAck."""

    @pytest.mark.network
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_08_multicast_option_length_is_nine(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """OPTIONS_08: IPv4MulticastOption length field must be 0x0009."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        if ipaddress.ip_address(host_ip).is_loopback:
            pytest.skip(
                "OPTIONS_08: Multicast endpoint option in SubscribeAck requires a non-loopback interface. "
                "Set TC8_HOST_IP to a non-loopback address."
            )

        _, resolved_ack = _capture_subscribe_ack_with_options(
            host_ip, tester_ip, _MULTICAST_EVENTGROUP_ID
        )

        all_opts = list(resolved_ack.options_1) + list(resolved_ack.options_2)
        mcast_opts = [o for o in all_opts if isinstance(o, IPv4MulticastOption)]
        assert mcast_opts, (
            f"OPTIONS_08: No IPv4MulticastOption found in SubscribeAck for "
            f"eventgroup 0x{_MULTICAST_EVENTGROUP_ID:04x}"
        )
        opt = mcast_opts[0]
        raw = opt.build()

        length_field = int.from_bytes(raw[0:2], "big")
        assert length_field == 0x0009, (
            f"OPTIONS_08: IPv4MulticastOption length field must be 0x0009; "
            f"got 0x{length_field:04x}"
        )

    @pytest.mark.network
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_09_multicast_option_type_is_0x14(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """OPTIONS_09: IPv4MulticastOption type byte must be 0x14 (decimal 20)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        if ipaddress.ip_address(host_ip).is_loopback:
            pytest.skip(
                "OPTIONS_09: Multicast endpoint option in SubscribeAck requires a non-loopback interface. "
                "Set TC8_HOST_IP to a non-loopback address."
            )

        _, resolved_ack = _capture_subscribe_ack_with_options(
            host_ip, tester_ip, _MULTICAST_EVENTGROUP_ID
        )

        all_opts = list(resolved_ack.options_1) + list(resolved_ack.options_2)
        mcast_opts = [o for o in all_opts if isinstance(o, IPv4MulticastOption)]
        assert mcast_opts, (
            f"OPTIONS_09: No IPv4MulticastOption found in SubscribeAck for "
            f"eventgroup 0x{_MULTICAST_EVENTGROUP_ID:04x}"
        )
        opt = mcast_opts[0]
        raw = opt.build()

        type_byte = raw[2]
        assert type_byte == 0x14, (
            f"OPTIONS_09: IPv4MulticastOption type byte must be 0x14; "
            f"got 0x{type_byte:02x}"
        )

    @pytest.mark.network
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_10_multicast_option_reserved_is_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """OPTIONS_10: Reserved byte at offset [3] of IPv4MulticastOption must be 0x00."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        if ipaddress.ip_address(host_ip).is_loopback:
            pytest.skip(
                "OPTIONS_10: Multicast endpoint option in SubscribeAck requires a non-loopback interface. "
                "Set TC8_HOST_IP to a non-loopback address."
            )

        _, resolved_ack = _capture_subscribe_ack_with_options(
            host_ip, tester_ip, _MULTICAST_EVENTGROUP_ID
        )

        all_opts = list(resolved_ack.options_1) + list(resolved_ack.options_2)
        mcast_opts = [o for o in all_opts if isinstance(o, IPv4MulticastOption)]
        assert mcast_opts, (
            f"OPTIONS_10: No IPv4MulticastOption found in SubscribeAck for "
            f"eventgroup 0x{_MULTICAST_EVENTGROUP_ID:04x}"
        )
        opt = mcast_opts[0]
        raw = opt.build()

        reserved_byte = raw[3]
        assert reserved_byte == 0x00, (
            f"OPTIONS_10: IPv4MulticastOption reserved byte [3] must be 0x00; "
            f"got 0x{reserved_byte:02x}"
        )

    @pytest.mark.network
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_11_multicast_address_matches_config(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """OPTIONS_11: IPv4MulticastOption address must match the configured multicast address."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        if ipaddress.ip_address(host_ip).is_loopback:
            pytest.skip(
                "OPTIONS_11: Multicast endpoint option in SubscribeAck requires a non-loopback interface. "
                "Set TC8_HOST_IP to a non-loopback address."
            )

        _, resolved_ack = _capture_subscribe_ack_with_options(
            host_ip, tester_ip, _MULTICAST_EVENTGROUP_ID
        )

        all_opts = list(resolved_ack.options_1) + list(resolved_ack.options_2)
        mcast_opts = [o for o in all_opts if isinstance(o, IPv4MulticastOption)]
        assert mcast_opts, (
            f"OPTIONS_11: No IPv4MulticastOption found in SubscribeAck for "
            f"eventgroup 0x{_MULTICAST_EVENTGROUP_ID:04x}"
        )
        opt = mcast_opts[0]

        expected_addr = ipaddress.IPv4Address(_MULTICAST_ADDR)
        assert opt.address == expected_addr, (
            f"OPTIONS_11: multicast address must be {_MULTICAST_ADDR}; "
            f"got {opt.address}"
        )

    @pytest.mark.network
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_12_multicast_option_reserved_before_port_is_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """OPTIONS_12: Reserved byte at offset [8] of IPv4MulticastOption must be 0x00."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        if ipaddress.ip_address(host_ip).is_loopback:
            pytest.skip(
                "OPTIONS_12: Multicast endpoint option in SubscribeAck requires a non-loopback interface. "
                "Set TC8_HOST_IP to a non-loopback address."
            )

        _, resolved_ack = _capture_subscribe_ack_with_options(
            host_ip, tester_ip, _MULTICAST_EVENTGROUP_ID
        )

        all_opts = list(resolved_ack.options_1) + list(resolved_ack.options_2)
        mcast_opts = [o for o in all_opts if isinstance(o, IPv4MulticastOption)]
        assert mcast_opts, (
            f"OPTIONS_12: No IPv4MulticastOption found in SubscribeAck for "
            f"eventgroup 0x{_MULTICAST_EVENTGROUP_ID:04x}"
        )
        opt = mcast_opts[0]
        raw = opt.build()

        reserved_byte = raw[8]
        assert reserved_byte == 0x00, (
            f"OPTIONS_12: IPv4MulticastOption reserved byte [8] must be 0x00; "
            f"got 0x{reserved_byte:02x}"
        )

    @pytest.mark.network
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_13_multicast_option_protocol_is_udp(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """OPTIONS_13: IPv4MulticastOption L4 protocol must be UDP (0x11)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        if ipaddress.ip_address(host_ip).is_loopback:
            pytest.skip(
                "OPTIONS_13: Multicast endpoint option in SubscribeAck requires a non-loopback interface. "
                "Set TC8_HOST_IP to a non-loopback address."
            )

        _, resolved_ack = _capture_subscribe_ack_with_options(
            host_ip, tester_ip, _MULTICAST_EVENTGROUP_ID
        )

        all_opts = list(resolved_ack.options_1) + list(resolved_ack.options_2)
        mcast_opts = [o for o in all_opts if isinstance(o, IPv4MulticastOption)]
        assert mcast_opts, (
            f"OPTIONS_13: No IPv4MulticastOption found in SubscribeAck for "
            f"eventgroup 0x{_MULTICAST_EVENTGROUP_ID:04x}"
        )
        opt = mcast_opts[0]

        assert opt.l4proto == L4Protocols.UDP, (
            f"OPTIONS_13: IPv4MulticastOption l4proto must be UDP "
            f"(0x{L4Protocols.UDP:02x}); got {opt.l4proto!r}"
        )

    @pytest.mark.network
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_options_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_options_14_multicast_port_matches_config(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """OPTIONS_14: IPv4MulticastOption port must match the configured multicast port."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        if ipaddress.ip_address(host_ip).is_loopback:
            pytest.skip(
                "OPTIONS_14: Multicast endpoint option in SubscribeAck requires a non-loopback interface. "
                "Set TC8_HOST_IP to a non-loopback address."
            )

        _, resolved_ack = _capture_subscribe_ack_with_options(
            host_ip, tester_ip, _MULTICAST_EVENTGROUP_ID
        )

        all_opts = list(resolved_ack.options_1) + list(resolved_ack.options_2)
        mcast_opts = [o for o in all_opts if isinstance(o, IPv4MulticastOption)]
        assert mcast_opts, (
            f"OPTIONS_14: No IPv4MulticastOption found in SubscribeAck for "
            f"eventgroup 0x{_MULTICAST_EVENTGROUP_ID:04x}"
        )
        opt = mcast_opts[0]

        assert opt.port == _MULTICAST_PORT, (
            f"OPTIONS_14: multicast port must be {_MULTICAST_PORT}; got {opt.port}"
        )


# ---------------------------------------------------------------------------
# TestSdMissingFormatFields — FORMAT_03/07/14/17/22
# ---------------------------------------------------------------------------


class TestSdMissingFormatFields:
    """FORMAT_03/07/14/17/22 — previously unverified SD format fields."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_03_protocol_version_is_one(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_03: SOME/IP protocol_version in the SD message header must be 1.

        The SOME/IP header byte 12 (protocol_version) must be 0x01 per
        PRS_SOMEIP_00052.  This applies to all SD messages including OfferService.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        someip_msg, _, _, _ = _capture_raw_sd_offer(host_ip)

        assert someip_msg.protocol_version == 1, (
            f"FORMAT_03: protocol_version must be 0x01; "
            f"got 0x{someip_msg.protocol_version:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_07_unicast_flag_set(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_07: SD Flags byte — Unicast flag (bit 6) must be set in OfferService.

        PRS_SOMEIPSD_00351: The Unicast flag indicates the sender supports unicast
        communication.  vsomeip sets this flag in all SD messages it sends.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, sd_hdr, _, _ = _capture_raw_sd_offer(host_ip)

        assert sd_hdr.flag_unicast, (
            "FORMAT_07: SD Flags Unicast flag (bit 6) must be set in OfferService; "
            f"flag_unicast={sd_hdr.flag_unicast}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_14_entry_type_is_offer(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_14: OfferService entry Type byte (byte[0]) must be 0x01.

        SOME/IP-SD Type 1 entry (OfferService) has type byte 0x01 per
        PRS_SOMEIPSD_00306.  This test reads the raw serialised entry byte
        directly rather than relying on the parsed enum value.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        assigned = entry.assign_option_index([])
        raw = assigned.build()

        # Byte [0] of the 16-byte entry is the Type field.
        type_byte = raw[0]
        assert type_byte == 0x01, (
            f"FORMAT_14: OfferService entry Type byte must be 0x01; "
            f"got 0x{type_byte:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_17_ttl_is_nonzero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """FORMAT_17: OfferService entry TTL (3-byte big-endian, bytes[9–11]) must be > 0.

        TTL = 0 in an OfferService entry is reserved for StopOfferService
        (PRS_SOMEIPSD_00137).  A live service must advertise TTL > 0.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        assigned = entry.assign_option_index([])
        raw = assigned.build()

        # Bytes [9-11] are the 3-byte big-endian TTL field.
        ttl_value = int.from_bytes(raw[9:12], "big")
        assert ttl_value > 0, (
            f"FORMAT_17: OfferService entry TTL (bytes[9-11]) must be > 0; "
            f"got {ttl_value}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_format_22_ack_num_options_1_matches(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """FORMAT_22: SubscribeAck entry num_options_1 must equal len(options_1) list.

        After options are resolved, the original num_options_1 counter in the
        raw entry must match the number of option objects in the resolved list.
        This verifies the DUT serialises the options-count nibble correctly.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        unresolved_ack, resolved_ack = _capture_subscribe_ack_with_options(
            host_ip, tester_ip, _EVENTGROUP_ID
        )

        # unresolved_ack.num_options_1 is the raw counter from the wire.
        # resolved_ack.options_1 is the list populated by resolve_options().
        assert unresolved_ack.num_options_1 == len(resolved_ack.options_1), (
            f"FORMAT_22: SubscribeAck num_options_1 ({unresolved_ack.num_options_1}) "
            f"must equal len(options_1) ({len(resolved_ack.options_1)})"
        )


# ---------------------------------------------------------------------------
# TestSdEntryOptionFields — SD_MESSAGE_07/08/09/11
# ---------------------------------------------------------------------------


class TestSdEntryOptionFields:
    """SD_MESSAGE_07–09, SD_MESSAGE_11 — entry option-run fields."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_07_offer_entry_type_byte(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SD_MESSAGE_07: OfferService entry raw Type byte must be 0x01.

        PRS_SOMEIPSD_00306 assigns Type=0x01 to OfferService.
        Duplicate of FORMAT_14 at the SD_MESSAGE layer to satisfy the
        SD_MESSAGE_07 traceability requirement.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        assigned = entry.assign_option_index([])
        raw = assigned.build()

        type_byte = raw[0]
        assert type_byte == 0x01, (
            f"SD_MESSAGE_07: OfferService entry Type byte must be 0x01; "
            f"got 0x{type_byte:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_08_offer_entry_option_run2_index_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SD_MESSAGE_08: OfferService entry option_index_2 must be 0 (single option run).

        A standard OfferService entry uses only the first option run.
        Byte[2] of the 16-byte entry holds option_index_2 (the index into
        the Options Array for the second option run) and must be 0 when only
        one option run is used.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, _ = _capture_raw_sd_offer(host_ip)

        assigned = entry.assign_option_index([])
        raw = assigned.build()

        # Byte [2] is option_index_2 (second option-run start index).
        option_index_2 = raw[2]
        assert option_index_2 == 0, (
            f"SD_MESSAGE_08: OfferService option_index_2 (byte[2]) must be 0; "
            f"got 0x{option_index_2:02x}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_09_offer_entry_num_options_2_zero(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """SD_MESSAGE_09: OfferService entry num_options_2 must be 0 (single option run).

        Byte[3] of the 16-byte SD entry encodes num_options_1 in the high nibble
        and num_options_2 in the low nibble.  A standard OfferService with a single
        option run must have num_options_2 == 0 (low nibble = 0).
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        _, _, entry, raw_entry = _capture_raw_sd_offer(host_ip)

        assigned = entry.assign_option_index([])
        raw = assigned.build()

        # Byte [3]: high nibble = num_options_1, low nibble = num_options_2.
        num_options_2 = raw[3] & 0x0F
        assert num_options_2 == 0, (
            f"SD_MESSAGE_09: OfferService num_options_2 (low nibble of byte[3]) "
            f"must be 0; got 0x{num_options_2:01x} "
            f"(full byte[3]: 0x{raw[3]:02x})"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_format_fields"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_11_subscribe_entry_type_byte(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SD_MESSAGE_11: A SubscribeEventgroup entry must have Type byte 0x06.

        PRS_SOMEIPSD_00306 assigns Type=0x06 to SubscribeEventgroup (Type 2 entry).
        This test builds a SubscribeEventgroup entry using the same helper used
        by the SD sender (``send_subscribe_eventgroup``) and verifies that the
        serialised entry byte[0] == 0x06.  This confirms our sender constructs
        conformant SD messages.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port: int = sock.getsockname()[1]
            # Build a subscribe entry directly and inspect the raw bytes.
            endpoint_opt = IPv4EndpointOption(
                address=ipaddress.IPv4Address(tester_ip),
                l4proto=L4Protocols.UDP,
                port=sender_port,
            )
            subscribe_entry = SOMEIPSDEntry(
                sd_type=SOMEIPSDEntryType.Subscribe,
                service_id=_SERVICE_ID,
                instance_id=_INSTANCE_ID,
                major_version=_MAJOR_VERSION,
                ttl=3,
                minver_or_counter=_EVENTGROUP_ID & 0xFFFF,
                options_1=(endpoint_opt,),
            )
            options: list = []
            assigned = subscribe_entry.assign_option_index(options)
            raw = assigned.build()
        finally:
            sock.close()

        # Byte [0] of the 16-byte entry is the Type field.
        type_byte = raw[0]
        assert type_byte == 0x06, (
            f"SD_MESSAGE_11: SubscribeEventgroup entry Type byte must be 0x06; "
            f"got 0x{type_byte:02x}"
        )
