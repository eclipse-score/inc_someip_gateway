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
"""TC8 Service Discovery tests: TC8-SD (SD-012 is not used), TC8-SDM and TC8-SDLC entries.

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import ipaddress
import socket
import struct
import subprocess
import time
from typing import List, Optional, Tuple

import pytest

from attribute_plugin import add_test_properties

from helpers.sd_helpers import (
    capture_sd_offers,
    open_multicast_socket,
    parse_raw_eventgroup_entry,
    parse_sd_offers,
    raw_entry_options,
    raw_sd_entries,
)
from helpers.sd_malformed import send_sd_subscribe_option_reserved_set
from helpers.sd_sender import (
    SOMEIPSDEntryType,
    build_subscribe_entry,
    capture_sd_payload,
    capture_some_ip_messages,
    capture_unicast_sd_entries,
    open_sender_socket,
    send_find_service,
    send_sd_entries,
    send_subscribe_eventgroup,
)
from helpers.someip_assertions import (
    assert_eventgroup_ack_entry,
    assert_raw_ipv4_option,
    assert_sd_offer_entry,
)
from helpers.constants import (
    DUT_UNRELIABLE_PORT,
    EVENT_FIELD_UINT8,
    EVENT_FIELD_UINT8_ARRAY,
    EVENT_INTERFACE_VERSION,
    EVENTGROUP_TCP_RELIABLE,
    EVENTGROUP_UDP_MULTICAST,
    EVENTGROUP_UDP_UNICAST,
    INSTANCE_ID,
    INSTANCE_ID_2,
    MAJOR_VERSION,
    MINOR_VERSION,
    MULTICAST_ADDR,
    MULTICAST_EVENT_PORT,
    OFFER_TTL,
    SD_MULTICAST_ADDR,
    SD_PORT,
    SERVICE_ID,
)
from helpers.timing import capture_sd_offers_with_timestamps
from helpers.someip_types import SOMEIPHeader, SOMEIPSDEntry, SOMEIPSDHeader

#: SOME/IP stack config template used for all tests in this module.
SOMEIP_CONFIG: str = "tc8_someipd_sd.json"

#: Service and instance IDs declared in ``tc8_someipd_sd.json``.
_UNKNOWNEVENTGROUP_UDP_UNICAST: int = 0xBEEF
_UNKNOWNSERVICE_ID: int = 0xBEEF

#: Eventgroups subscribed together in ETS_088.
_ETS_088_EVENTGROUPS = (EVENTGROUP_UDP_UNICAST, EVENTGROUP_TCP_RELIABLE, EVENTGROUP_UDP_MULTICAST)

#: SD configuration values from ``tc8_someipd_sd.json``.
_CYCLIC_OFFER_DELAY_MS: float = 2000.0
_REQUEST_RESPONSE_DELAY_MS: float = 500.0

#: ParamListenTime of the OA TC8 specification.
_PARAM_LISTEN_TIME_SECS: float = 10.0

#: Minimum gap between two OfferService messages that marks the main phase.
_MAIN_PHASE_MIN_GAP_SECS: float = _CYCLIC_OFFER_DELAY_MS * 0.75 / 1000.0

#: Fields published by the DUT without an update cycle.
_INITIAL_FIELDS = (EVENT_INTERFACE_VERSION, EVENT_FIELD_UINT8, EVENT_FIELD_UINT8_ARRAY)


def _xfail_no_initial_fields(oa_id: str) -> pytest.MarkDecorator:
    """Mark a test whose OA criterion, initial fields after a Subscribe, the DUT does not meet."""
    return pytest.mark.xfail(
        strict=False,
        reason=(
            f"OA {oa_id}: the initial fields 0x8005 to 0x8007 are expected after the subscription. "
            "Observed with the current DUT stack: they are not received after the subscription."
        ),
    )


def _assert_initial_fields(notifs: List[SOMEIPHeader], label: str) -> None:
    """Assert that every initial field was received among *notifs*."""
    received = {m.method_id for m in notifs}
    missing = [f"0x{f:04x}" for f in _INITIAL_FIELDS if f not in received]
    assert not missing, (
        f"{label}: initial field(s) {missing} not received after the subscription, "
        f"got events {sorted(hex(m) for m in received)}"
    )


def _subscribe_for_ack(
    sock: socket.socket,
    label: str,
    dut_ip: str,
    tester_ip: str,
    eventgroup_id: int,
    ttl: int = 0xFFFFFF,
    service_id: int = SERVICE_ID,
    instance_id: int = INSTANCE_ID,
    major_version: int = MAJOR_VERSION,
) -> Tuple[bytes, bytes]:
    """Send a SubscribeEventgroup until a SubscribeEventgroupAck arrives.

    Returns the SD payload of the answer and its raw 16-byte Ack entry.
    """
    sender_port = sock.getsockname()[1]

    def _send() -> None:
        send_subscribe_eventgroup(
            sock,
            (dut_ip, SD_PORT),
            service_id,
            instance_id,
            eventgroup_id,
            major_version,
            subscriber_ip=tester_ip,
            subscriber_port=sender_port,
            ttl=ttl,
        )

    _send()
    payload = capture_sd_payload(sock, SOMEIPSDEntryType.SubscribeAck, timeout_secs=5.0, resend=_send)
    assert payload is not None, f"{label}: No SubscribeEventgroupAck received within 5 s"
    raw_ack = next(e for e in raw_sd_entries(payload) if e[0] == SOMEIPSDEntryType.SubscribeAck)
    return payload, raw_ack


def _stop_subscribe(sock: socket.socket, dut_ip: str, tester_ip: str, eventgroup_id: int) -> None:
    """End the subscription of *sock* to *eventgroup_id* (TTL = 0)."""
    send_subscribe_eventgroup(
        sock,
        (dut_ip, SD_PORT),
        SERVICE_ID,
        INSTANCE_ID,
        eventgroup_id,
        MAJOR_VERSION,
        subscriber_ip=tester_ip,
        subscriber_port=sock.getsockname()[1],
        ttl=0,
    )


def _offers_for_find(
    sock: socket.socket, dut_ip: str, max_results: Optional[int] = 1, **find_args: int
) -> List[SOMEIPSDEntry]:
    """Send a unicast FindService until OfferService entries arrive.

    Returns the OfferService entries for the service in the answer.
    """

    def _send() -> None:
        send_find_service(sock, (dut_ip, SD_PORT), service_id=SERVICE_ID, ttl=0xFFFFFF, **find_args)

    _send()
    entries = capture_unicast_sd_entries(
        sock,
        filter_types=(SOMEIPSDEntryType.OfferService,),
        timeout_secs=5.0,
        resend=_send,
        max_results=max_results,
    )
    return [e for e in entries if e.service_id == SERVICE_ID]


def _wait_for_cyclic_offer(mc_sock: socket.socket, timeout_secs: float = 15.0) -> float:
    """Return the receive time of a main phase OfferService.

    A main phase OfferService follows the previous OfferService by about cyclic_offer_delay.
    """
    deadline = time.monotonic() + timeout_secs
    previous: Optional[float] = None
    while (remaining := deadline - time.monotonic()) > 0:
        mc_sock.settimeout(min(remaining, 0.5))
        try:
            data, _ = mc_sock.recvfrom(65535)
        except socket.timeout:
            continue
        if not any(e.service_id == SERVICE_ID for e in parse_sd_offers(data)):
            continue
        now = time.monotonic()
        if previous is not None and now - previous >= _MAIN_PHASE_MIN_GAP_SECS:
            return now
        previous = now
    pytest.fail(f"No main phase OfferService within {timeout_secs:.0f} s")


class TestSDOfferFormat:
    """TC8-SD-002/003: Offer fields, and cyclic timing."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_002_offer_entry_format(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-SD-002: OfferService entry has correct service ID, instance ID, version, and TTL."""
        assert dut.poll() is None, "DUT is not running"

        offers = capture_sd_offers(host_ip, min_count=1, timeout_secs=5.0)

        service_offers = [e for e in offers if e.service_id == SERVICE_ID and e.instance_id == INSTANCE_ID]
        assert service_offers, (
            f"TC8-SD-002: No OfferService entry found for service 0x{SERVICE_ID:04x} "
            f"instance 0x{INSTANCE_ID:04x} in {len(offers)} captured SD entries."
        )

        assert_sd_offer_entry(
            service_offers[0],
            expected_service_id=SERVICE_ID,
            expected_instance_id=INSTANCE_ID,
            expected_major_version=MAJOR_VERSION,
            expected_minor_version=MINOR_VERSION,
        )
        assert service_offers[0].ttl == OFFER_TTL, (
            f"TC8-SD-002: OFFER TTL = {service_offers[0].ttl}, expected the configured {OFFER_TTL}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_cyclic_timing"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_003_cyclic_offer_timing(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-SD-003: Offers repeat at cyclic_offer_delay ±20% in the main phase (stricter than the OA bound)."""
        assert dut.poll() is None, "DUT is not running"

        # Wait for main phase. Repetition phase ends ~1.5 s after first offer
        # (200+400+800 ms doubling, repetitions_max=3).  Add one cyclic gap
        # (2000 ms) to ensure we capture main-phase offers.
        time.sleep(3.5)

        # Capture enough offers to derive at least 2 inter-cycle gaps.
        # The DUT may send multiple SD packets per cycle (service + eventgroup),
        # so we capture extra and de-duplicate within a 500 ms window.
        timed = capture_sd_offers_with_timestamps(host_ip, count=5, timeout_secs=15.0, instance_id=INSTANCE_ID)

        service_offers = [(ts, e) for ts, e in timed if e.service_id == SERVICE_ID]
        assert len(service_offers) >= 2, "TC8-SD-003: Not enough OfferService entries for timing analysis"

        # De-duplicate: collapse offers within 500 ms into one cycle timestamp.
        cycle_timestamps: list[float] = [service_offers[0][0]]
        for ts, _ in service_offers[1:]:
            if (ts - cycle_timestamps[-1]) * 1000.0 > 500:
                cycle_timestamps.append(ts)

        assert len(cycle_timestamps) >= 3, (
            f"TC8-SD-003: Only {len(cycle_timestamps)} distinct cycles captured "
            f"(need at least 3 for 2 gap measurements)"
        )

        lo_ms = _CYCLIC_OFFER_DELAY_MS * 0.80
        hi_ms = _CYCLIC_OFFER_DELAY_MS * 1.20

        for i in range(len(cycle_timestamps) - 1):
            gap_ms = (cycle_timestamps[i + 1] - cycle_timestamps[i]) * 1000.0
            assert lo_ms <= gap_ms <= hi_ms, (
                f"TC8-SD-003: inter-offer gap {gap_ms:.0f} ms not in "
                f"[{lo_ms:.0f}, {hi_ms:.0f}] ms "
                f"(cyclic_offer_delay={_CYCLIC_OFFER_DELAY_MS:.0f} ms ±20%)"
            )


class TestSDFindResponse:
    """TC8-SD-004/005: FindService response for known/unknown services."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_004_find_known_service_unicast_offer(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-004: FindService for offered service triggers a unicast OfferService."""
        assert dut.poll() is None, "DUT is not running"

        # tester_ip differs from host_ip; both need SD_PORT (SD spec requirement).
        # Send FindService via unicast to the DUT (multicast delivery on loopback
        # between different 127.x addresses is unreliable in some environments).
        sock = open_sender_socket(tester_ip)
        try:

            def _send_find() -> None:
                send_find_service(
                    sock,
                    (dut_ip, SD_PORT),
                    service_id=SERVICE_ID,
                    instance_id=INSTANCE_ID,
                )

            _send_find()
            # Provider responds to the sender's address via unicast.
            # DUT may defer the response until the next SD cycle (~2 s).
            # Resend periodically to handle batching/dedup in long-running DUT.
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.OfferService,),
                timeout_secs=5.0,
                resend=_send_find,
            )
            service_offers = [e for e in entries if e.service_id == SERVICE_ID]
            assert service_offers, (
                f"TC8-SD-004: No unicast OfferService received for service 0x{SERVICE_ID:04x} within 5 s of FindService"
            )
        finally:
            sock.close()


class TestSDSubscribeLifecycle:
    """TC8-SD-006/007/008: Subscribe Ack, Nack, and StopSubscribe."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_006_subscribe_valid_eventgroup_ack(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-006: Valid SubscribeEventgroup receives a SubscribeEventgroupAck that echoes the subscription."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            _, raw_ack = _subscribe_for_ack(sock, "TC8-SD-006", dut_ip, tester_ip, EVENTGROUP_UDP_UNICAST)
            assert_eventgroup_ack_entry(
                parse_raw_eventgroup_entry(raw_ack),
                "TC8-SD-006",
                service_id=SERVICE_ID,
                instance_id=INSTANCE_ID,
                major_version=MAJOR_VERSION,
                eventgroup_id=EVENTGROUP_UDP_UNICAST,
                ttl=0xFFFFFF,
            )
        finally:
            # TTL 0xFFFFFF does not expire, end the subscription.
            _stop_subscribe(sock, dut_ip, tester_ip, EVENTGROUP_UDP_UNICAST)
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_007_subscribe_unknown_eventgroup_nack(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-007: SubscribeEventgroup for unknown eventgroup receives Nack (TTL=0)."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            _, raw_ack = _subscribe_for_ack(sock, "TC8-SD-007", dut_ip, tester_ip, _UNKNOWNEVENTGROUP_UDP_UNICAST)
        finally:
            sock.close()

        ack = parse_raw_eventgroup_entry(raw_ack)
        assert (ack.eventgroup_id, ack.ttl) == (_UNKNOWNEVENTGROUP_UDP_UNICAST, 0), (
            f"TC8-SD-007: expected SubscribeEventgroupNack (TTL=0) for unknown eventgroup "
            f"0x{_UNKNOWNEVENTGROUP_UDP_UNICAST:04x}, got eventgroup 0x{ack.eventgroup_id:04x} TTL {ack.ttl}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_008_stop_subscribe_ceases_notifications(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-008 (ETS_108): after StopSubscribeEventgroup for eventgroup 0x05 no events are sent."""
        assert dut.poll() is None, "DUT is not running"

        # sd_sock: SD sender at tester_ip:SD_PORT.
        # notif_sock: receives event notifications at tester_ip:<ephemeral>.
        sd_sock = open_sender_socket(tester_ip)
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port: int = notif_sock.getsockname()[1]

        try:
            # Notifications will arrive on notif_sock.
            def _send_sub_008() -> None:
                send_subscribe_eventgroup(
                    sd_sock,
                    (dut_ip, SD_PORT),
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_TCP_RELIABLE,
                    MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=notif_port,
                )

            _send_sub_008()
            acks = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_send_sub_008,
            )
            assert any(e.eventgroup_id == EVENTGROUP_TCP_RELIABLE and e.ttl > 0 for e in acks), (
                "TC8-SD-008: Prerequisite failed, no SubscribeEventgroupAck received"
            )

            # Expect at least one notification (DUT fires notify() every 2 s).
            notifs = capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=4.0)
            assert notifs, "TC8-SD-008: No SOME/IP notifications received after subscribe"

            send_subscribe_eventgroup(
                sd_sock,
                (dut_ip, SD_PORT),
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_TCP_RELIABLE,
                MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=notif_port,
                ttl=0,
            )

            post = capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=4.0)
            assert not post, f"TC8-SD-008: {len(post)} notification(s) received after StopSubscribeEventgroup"
        finally:
            sd_sock.close()
            notif_sock.close()


class TestSDOptionFormat:
    """TC8-SD-011: OfferService entry carries a valid IPv4EndpointOption."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_endpoint_option"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_011_offer_ipv4_endpoint_option(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
    ) -> None:
        """TC8-SD-011: SD OFFER entry has exactly one IPv4EndpointOption with correct content."""
        assert dut.poll() is None, "DUT is not running"

        # Raw SD payload: the checks cover Length and Reserved, which the parsed options drop.
        sock = open_multicast_socket(host_ip)
        try:
            payload = capture_sd_payload(sock, SOMEIPSDEntryType.OfferService, timeout_secs=5.0)
        finally:
            sock.close()

        assert payload is not None, "TC8-SD-011: No OfferService received within 5 s"
        raw_offer = next(
            (
                e
                for e in raw_sd_entries(payload)
                if e[0] == SOMEIPSDEntryType.OfferService
                and struct.unpack_from(">HH", e, 4) == (SERVICE_ID, INSTANCE_ID)
            ),
            None,
        )
        assert raw_offer is not None, (
            f"TC8-SD-011: No OfferService entry for service 0x{SERVICE_ID:04x} instance 0x{INSTANCE_ID:04x}"
        )
        options = raw_entry_options(payload, raw_offer)
        assert len(options) == 1, (
            f"TC8-SD-011: expected exactly one option in the OfferService entry, got {len(options)}"
        )
        assert_raw_ipv4_option(
            options[0],
            "TC8-SD-011",
            option_type=0x04,
            address=dut_ip,
            l4proto=0x11,
            port=DUT_UNRELIABLE_PORT,
        )


class TestSDMulticastEventgroup:
    """TC8-SD-013: SUBSCRIBE_ACK for multicast eventgroup includes multicast IP option."""

    @pytest.mark.network
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_mcast_eg"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_013_subscribe_ack_has_multicast_option(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-013: Subscribing to multicast eventgroup 0x0006 yields a multicast endpoint option."""

        # On loopback, the multicast endpoint option is missing in SUBSCRIBE_ACK.
        # Requires a non-loopback interface.
        if ipaddress.ip_address(host_ip).is_loopback:
            pytest.skip(
                "TC8-SD-013: Multicast endpoint option in SUBSCRIBE_ACK requires a "
                "non-loopback interface. "
                "Set TC8_HOST_IP to a non-loopback address (e.g. TC8_HOST_IP=192.168.x.y)."
            )

        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            payload, raw_ack = _subscribe_for_ack(
                sock, "TC8-SD-013", dut_ip, tester_ip, EVENTGROUP_UDP_MULTICAST, ttl=3
            )
            assert parse_raw_eventgroup_entry(raw_ack).ttl > 0, (
                f"TC8-SD-013: SubscribeEventgroupAck for eventgroup 0x{EVENTGROUP_UDP_MULTICAST:04x} has TTL 0"
            )
            # The multicast option (type 0x14) differs from the IPv4 endpoint option (type 0x04).
            multicast_options = [o for o in raw_entry_options(payload, raw_ack) if o[2] == 0x14]
            assert len(multicast_options) == 1, (
                f"TC8-SD-013: SUBSCRIBE_ACK for eventgroup 0x{EVENTGROUP_UDP_MULTICAST:04x} carries "
                f"{len(multicast_options)} multicast option(s) (SD option type 0x14), expected 1"
            )
            assert_raw_ipv4_option(
                multicast_options[0],
                "TC8-SD-013",
                option_type=0x14,
                address=MULTICAST_ADDR,
                l4proto=0x11,
                port=MULTICAST_EVENT_PORT,
            )
        finally:
            _stop_subscribe(sock, dut_ip, tester_ip, EVENTGROUP_UDP_MULTICAST)
            sock.close()


class TestSDTTLExpiry:
    """TC8-SD-014: Subscription with finite TTL is cleaned up after expiry."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_014_ttl_expiry_ceases_notifications(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-014: No notifications after subscription TTL expires."""
        assert dut.poll() is None, "DUT is not running"

        sd_sock = open_sender_socket(tester_ip)
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port: int = notif_sock.getsockname()[1]

        try:
            _TTL_SECS = 3

            def _send_sub_ttl() -> None:
                send_subscribe_eventgroup(
                    sd_sock,
                    (dut_ip, SD_PORT),
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_UDP_UNICAST,
                    MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=notif_port,
                    ttl=_TTL_SECS,
                )

            _send_sub_ttl()
            acks = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_send_sub_ttl,
            )
            assert any(e.eventgroup_id == EVENTGROUP_UDP_UNICAST and e.ttl > 0 for e in acks), (
                "TC8-SD-014: Prerequisite failed, no SubscribeEventgroupAck received"
            )

            pre_expiry = capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=4.0)
            assert pre_expiry, "TC8-SD-014: No notifications received before TTL expiry"

            # Wait for TTL to expire (TTL + 2 s margin).
            time.sleep(_TTL_SECS + 2)

            post_expiry = capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=3.0)
            assert not post_expiry, (
                f"TC8-SD-014: {len(post_expiry)} notification(s) received after TTL expiry ({_TTL_SECS} s + 2 s margin)"
            )
        finally:
            sd_sock.close()
            notif_sock.close()


class TestSDVersionMatching:
    """SOMEIPSRV_SD_MESSAGE_01-06: FindService with Any/specific Instance, Major and Minor Version."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_01_any_instance(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_01: FindService with Any Instance (0xFFFF) returns every instance."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            offers = _offers_for_find(sock, dut_ip, max_results=2, instance_id=0xFFFF)
        finally:
            sock.close()

        assert len(offers) == 2 and {e.instance_id for e in offers} == {INSTANCE_ID, INSTANCE_ID_2}, (
            f"SOMEIPSRV_SD_MESSAGE_01: expected one OfferService message with the instances "
            f"0x{INSTANCE_ID:04x} and 0x{INSTANCE_ID_2:04x}, got {[hex(e.instance_id) for e in offers]}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_02_instance_specific(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_02: FindService with an exact instance_id returns only that instance."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            for instance_id in (INSTANCE_ID, INSTANCE_ID_2):
                offers = _offers_for_find(sock, dut_ip, instance_id=instance_id)
                assert [e.instance_id for e in offers] == [instance_id], (
                    f"SOMEIPSRV_SD_MESSAGE_02: expected one OfferService entry for instance "
                    f"0x{instance_id:04x}, got {[hex(e.instance_id) for e in offers]}"
                )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_03_any_major_version(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_03: FindService with Any Major Version (0xFF) returns the service."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            offers = _offers_for_find(sock, dut_ip, instance_id=INSTANCE_ID, major_version=0xFF)
        finally:
            sock.close()

        assert offers and all(e.major_version == MAJOR_VERSION for e in offers), (
            f"SOMEIPSRV_SD_MESSAGE_03: expected OfferService with major_version=0x{MAJOR_VERSION:02x} "
            f"for Any Major Version, got {[hex(e.major_version) for e in offers]}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_04_major_version_specific(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_04: FindService with the exact major_version returns the service."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            offers = _offers_for_find(sock, dut_ip, instance_id=INSTANCE_ID, major_version=MAJOR_VERSION)
        finally:
            sock.close()

        assert offers and all(e.major_version == MAJOR_VERSION for e in offers), (
            f"SOMEIPSRV_SD_MESSAGE_04: expected OfferService with major_version=0x{MAJOR_VERSION:02x}, "
            f"got {[hex(e.major_version) for e in offers]}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_05_any_minor_version(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_05: FindService with Any Minor Version (0xFFFFFFFF) returns the service."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            offers = _offers_for_find(
                sock, dut_ip, instance_id=INSTANCE_ID, major_version=MAJOR_VERSION, minor_version=0xFFFFFFFF
            )
        finally:
            sock.close()

        assert offers and all(e.service_minor_version == MINOR_VERSION for e in offers), (
            f"SOMEIPSRV_SD_MESSAGE_05: expected OfferService with minor_version=0x{MINOR_VERSION:08x} "
            f"for Any Minor Version, got {[hex(e.service_minor_version) for e in offers]}"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_06_minor_version_specific(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_06: FindService with the exact minor_version returns the service."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            offers = _offers_for_find(
                sock, dut_ip, instance_id=INSTANCE_ID, major_version=MAJOR_VERSION, minor_version=MINOR_VERSION
            )
        finally:
            sock.close()

        assert offers and all(e.service_minor_version == MINOR_VERSION for e in offers), (
            f"SOMEIPSRV_SD_MESSAGE_06: expected OfferService with minor_version=0x{MINOR_VERSION:08x}, "
            f"got {[hex(e.service_minor_version) for e in offers]}"
        )


class TestSDSubscribeNAck:
    """SOMEIPSRV_SD_MESSAGE_14-19 and ETS_144: SubscribeEventgroup answered with a NAck or an Ack."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_18_wrong_major_version(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_18: Subscribe with an unknown major_version is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            _, raw_ack = _subscribe_for_ack(
                sock,
                "SOMEIPSRV_SD_MESSAGE_18",
                dut_ip,
                tester_ip,
                EVENTGROUP_UDP_UNICAST,
                major_version=MAJOR_VERSION + 1,
            )
        finally:
            sock.close()

        assert_eventgroup_ack_entry(
            parse_raw_eventgroup_entry(raw_ack),
            "SOMEIPSRV_SD_MESSAGE_18",
            service_id=SERVICE_ID,
            instance_id=INSTANCE_ID,
            major_version=MAJOR_VERSION + 1,
            eventgroup_id=EVENTGROUP_UDP_UNICAST,
            ttl=0,
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_14_15_16_unknown_service_id(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_14/15/16: Subscribe to an unknown service_id is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            _, raw_ack = _subscribe_for_ack(
                sock,
                "SOMEIPSRV_SD_MESSAGE_14/15/16",
                dut_ip,
                tester_ip,
                EVENTGROUP_UDP_UNICAST,
                service_id=_UNKNOWNSERVICE_ID,
            )
        finally:
            sock.close()

        assert_eventgroup_ack_entry(
            parse_raw_eventgroup_entry(raw_ack),
            "SOMEIPSRV_SD_MESSAGE_14/15/16",
            service_id=_UNKNOWNSERVICE_ID,
            instance_id=INSTANCE_ID,
            major_version=MAJOR_VERSION,
            eventgroup_id=EVENTGROUP_UDP_UNICAST,
            ttl=0,
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_17_wrong_instance_id(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_17: Subscribe to an unknown instance_id is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            _, raw_ack = _subscribe_for_ack(
                sock,
                "SOMEIPSRV_SD_MESSAGE_17",
                dut_ip,
                tester_ip,
                EVENTGROUP_UDP_UNICAST,
                instance_id=INSTANCE_ID + 1,
            )
        finally:
            sock.close()

        assert_eventgroup_ack_entry(
            parse_raw_eventgroup_entry(raw_ack),
            "SOMEIPSRV_SD_MESSAGE_17",
            service_id=SERVICE_ID,
            instance_id=INSTANCE_ID + 1,
            major_version=MAJOR_VERSION,
            eventgroup_id=EVENTGROUP_UDP_UNICAST,
            ttl=0,
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_19_unknown_eventgroup_id(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_19: Subscribe to an unknown eventgroup_id is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            _, raw_ack = _subscribe_for_ack(
                sock, "SOMEIPSRV_SD_MESSAGE_19", dut_ip, tester_ip, _UNKNOWNEVENTGROUP_UDP_UNICAST
            )
        finally:
            sock.close()

        assert_eventgroup_ack_entry(
            parse_raw_eventgroup_entry(raw_ack),
            "SOMEIPSRV_SD_MESSAGE_19",
            service_id=SERVICE_ID,
            instance_id=INSTANCE_ID,
            major_version=MAJOR_VERSION,
            eventgroup_id=_UNKNOWNEVENTGROUP_UDP_UNICAST,
            ttl=0,
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=False,
        reason=(
            "OA ETS_144 (PRS_SOMEIPSD_00307): a SubscribeEventgroup whose IPv4 endpoint option has "
            "non-zero Reserved fields is expected to be answered with a SubscribeEventgroupAck. "
            "Observed with the current DUT stack: no Ack (TTL>0) is received"
        ),
    )
    def test_ets_144_reserved_field_endpoint_option_set(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_144: Subscribe with the Reserved fields of the endpoint option set is answered with an Ack."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]

            def _send() -> None:
                send_sd_subscribe_option_reserved_set(
                    sock,
                    (dut_ip, SD_PORT),
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_UDP_UNICAST,
                    MAJOR_VERSION,
                    tester_ip,
                    sender_port,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_send,
            )
            acks = [e for e in entries if e.eventgroup_id == EVENTGROUP_UDP_UNICAST and e.ttl > 0]
            assert acks, "ETS_144: No SubscribeEventgroupAck (TTL>0) received for an endpoint option with Reserved set"
        finally:
            sock.close()


class TestSDFindServiceTiming:
    """SOMEIPSRV_SD_BEHAVIOR_03-04: FindService answered with a multicast OfferService."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_cyclic_timing"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_behavior_03_unicast_findservice_timing(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_BEHAVIOR_03: FindService with Unicast Flag set, sent half a cyclic offer delay after
        the last OfferService, is answered with a multicast OfferService within the listen time (ParamListenTime)."""
        assert dut.poll() is None, "DUT is not running"

        mc_sock = open_multicast_socket(host_ip)
        send_sock = open_sender_socket(tester_ip)
        try:
            last_offer = _wait_for_cyclic_offer(mc_sock)
            time.sleep(max(0.0, last_offer + _CYCLIC_OFFER_DELAY_MS / 2000.0 - time.monotonic()))

            send_find_service(
                send_sock,
                (SD_MULTICAST_ADDR, SD_PORT),
                service_id=SERVICE_ID,
                instance_id=INSTANCE_ID,
                major_version=MAJOR_VERSION,
            )
            payload = capture_sd_payload(mc_sock, SOMEIPSDEntryType.OfferService, timeout_secs=_PARAM_LISTEN_TIME_SECS)
        finally:
            send_sock.close()
            mc_sock.close()

        assert payload is not None, (
            f"SOMEIPSRV_SD_BEHAVIOR_03: No multicast OfferService within the listen time "
            f"({_PARAM_LISTEN_TIME_SECS:.1f} s) of a FindService with the Unicast Flag set"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_cyclic_timing"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=False,
        reason=(
            "OA SOMEIPSRV_SD_BEHAVIOR_04: a FindService with the Unicast Flag cleared is expected to be "
            "answered with a multicast OfferService within cyclic_offer_delay. "
            "Observed: no multicast OfferService response; the next cyclic OfferService arrives about "
            "2.003 s after the FindService, just outside the window"
        ),
    )
    def test_sd_behavior_04_multicast_findservice_timing(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_BEHAVIOR_04: FindService with Unicast Flag cleared is answered with a multicast
        OfferService within cyclic_offer_delay."""
        assert dut.poll() is None, "DUT is not running"

        mc_sock = open_multicast_socket(host_ip)
        send_sock = open_sender_socket(tester_ip)
        try:
            _wait_for_cyclic_offer(mc_sock)

            send_find_service(
                send_sock,
                (SD_MULTICAST_ADDR, SD_PORT),
                service_id=SERVICE_ID,
                instance_id=0xFFFF,
                major_version=0xFF,
                flag_unicast=False,
            )
            window_secs = _CYCLIC_OFFER_DELAY_MS / 1000.0
            payload = capture_sd_payload(mc_sock, SOMEIPSDEntryType.OfferService, timeout_secs=window_secs)
        finally:
            send_sock.close()
            mc_sock.close()

        assert payload is not None, (
            f"SOMEIPSRV_SD_BEHAVIOR_04: No multicast OfferService within cyclic_offer_delay "
            f"({window_secs:.1f} s) of a FindService with the Unicast Flag cleared"
        )


class TestSDSubscribeLifecycleAdvanced:
    """ETS_088/092/095/107/120/155 and one project specific test: multi-subscribe and lifecycle edge cases."""

    @add_test_properties(
        partially_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_088_two_subscribes_same_session(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_088: One SD message with SubscribeEventgroup entries for 0x02, 0x05, 0x06 gets an ACK for each."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]
            entries_in = [
                build_subscribe_entry(
                    SERVICE_ID, INSTANCE_ID, eg, MAJOR_VERSION, subscriber_ip=tester_ip, subscriber_port=sender_port
                )
                for eg in _ETS_088_EVENTGROUPS
            ]

            def _send() -> None:
                send_sd_entries(sock, (dut_ip, SD_PORT), entries_in)

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=6.0,
                resend=_send,
            )
            for eg in _ETS_088_EVENTGROUPS:
                acks = [e for e in entries if e.eventgroup_id == eg and e.ttl > 0]
                assert acks, f"ETS_088: No SubscribeAck received for eventgroup 0x{eg:04x}"
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_092_ttl_zero_stop_subscribe_no_nack(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_092: SubscribeEventgroup with TTL=0 is a StopSubscribe (PRS_SOMEIPSD_00386/00387); no NAck sent."""
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]
            send_subscribe_eventgroup(
                sock,
                (dut_ip, SD_PORT),
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_UDP_UNICAST,
                MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=sender_port,
                ttl=0,
            )
            # No resend; a StopSubscribe should never produce an ACK.
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=2.0,
            )
            assert not entries, (
                f"ETS_092: DUT reacted to StopSubscribeEventgroup (TTL=0) with {len(entries)} SubscribeAck/NAck entries"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @_xfail_no_initial_fields("ETS_107")
    def test_ets_107_consider_entries_order(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_107 (SD_Consider_Entries_Order): after an initial subscribe, one SD message carries a
        StopSubscribe (TTL=0) entry followed by a Subscribe entry for the same eventgroup. The DUT must
        process them in order: the tester ends up subscribed, with a new ACK and the initial fields."""
        assert dut.poll() is None, "DUT is not running"

        sd_sock = open_sender_socket(tester_ip)
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port: int = notif_sock.getsockname()[1]
        try:

            def _entry(ttl: int) -> SOMEIPSDEntry:
                return build_subscribe_entry(
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_TCP_RELIABLE,
                    MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=notif_port,
                    ttl=ttl,
                )

            def _first() -> None:
                send_sd_entries(sd_sock, (dut_ip, SD_PORT), [_entry(30)])

            _first()
            first_acks = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_first,
                max_results=1,
            )
            assert any(e.eventgroup_id == EVENTGROUP_TCP_RELIABLE and e.ttl > 0 for e in first_acks), (
                "ETS_107: Prerequisite failed, no SubscribeAck for the initial subscribe"
            )

            # Discard the initial events and any duplicate ACK.
            capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=1.0)
            capture_unicast_sd_entries(sd_sock, timeout_secs=0.2)

            send_sd_entries(sd_sock, (dut_ip, SD_PORT), [_entry(0), _entry(30)])

            acks = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                max_results=1,
            )
            assert any(e.eventgroup_id == EVENTGROUP_TCP_RELIABLE and e.ttl > 0 for e in acks), (
                "ETS_107: No SubscribeAck after one message with StopSubscribe followed by Subscribe. "
                "DUT must process the entries in order."
            )
            notifs = capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=4.0)
            _assert_initial_fields(notifs, "ETS_107")
        finally:
            sd_sock.close()
            notif_sock.close()

    @add_test_properties(
        partially_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @_xfail_no_initial_fields("ETS_120")
    def test_ets_120_events_sent_to_endpoint_option_port(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_120: The ACK goes to the SD packet source; the initial fields go to the address and port in the
        endpoint option.

        The endpoint option names a different port than the SD source port.
        """
        assert dut.poll() is None, "DUT is not running"

        sock = open_sender_socket(tester_ip)
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port: int = notif_sock.getsockname()[1]
        try:

            def _send() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (dut_ip, SD_PORT),
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_UDP_UNICAST,
                    MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=notif_port,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_send,
            )
            acks = [e for e in entries if e.eventgroup_id == EVENTGROUP_UDP_UNICAST and e.ttl > 0]
            assert acks, f"ETS_120: No SubscribeAck received at tester_ip={tester_ip} on the SD source socket."

            notifs = capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=4.0)
            assert notifs, (
                f"ETS_120: No events received at {tester_ip}:{notif_port} from the endpoint option. "
                "Events must go to the endpoint option address and port."
            )
            _assert_initial_fields(notifs, "ETS_120")
        finally:
            sock.close()
            notif_sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @_xfail_no_initial_fields("ETS_155")
    def test_ets_155_resubscribe_after_stop(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_155: Subscribe, ACK, StopSubscribe, re-Subscribe gets a new ACK and the initial fields."""
        assert dut.poll() is None, "DUT is not running"

        sd_sock = open_sender_socket(tester_ip)
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port: int = notif_sock.getsockname()[1]

        try:

            def _subscribe() -> None:
                send_subscribe_eventgroup(
                    sd_sock,
                    (dut_ip, SD_PORT),
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_UDP_UNICAST,
                    MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=notif_port,
                )

            _subscribe()
            acks1 = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_subscribe,
            )
            assert any(e.eventgroup_id == EVENTGROUP_UDP_UNICAST and e.ttl > 0 for e in acks1), (
                "ETS_155: Prerequisite failed, no initial SubscribeAck received"
            )

            send_subscribe_eventgroup(
                sd_sock,
                (dut_ip, SD_PORT),
                SERVICE_ID,
                INSTANCE_ID,
                EVENTGROUP_UDP_UNICAST,
                MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=notif_port,
                ttl=0,
            )
            time.sleep(0.5)  # Allow DUT to process the StopSubscribe.
            capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=0.1)  # Drop stale events.

            _subscribe()
            acks2 = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_subscribe,
            )
            assert any(e.eventgroup_id == EVENTGROUP_UDP_UNICAST and e.ttl > 0 for e in acks2), (
                "ETS_155: No SubscribeAck received after re-subscribe following StopSubscribe"
            )
            notifs = capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=4.0)
            _assert_initial_fields(notifs, "ETS_155")
        finally:
            sd_sock.close()
            notif_sock.close()

    @add_test_properties(
        partially_verifies=["comp_req__tc8_conformance__sd_ttl_expiry"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_095_subscribe_ttl_expires_no_events(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_095: No NOTIFICATION messages are received after an unrenewed subscription TTL expires."""
        assert dut.poll() is None, "DUT is not running"

        # Use TTL=3 (same as TC8-SD-014); TTL=1 is too short to reliably
        # process the expiry before the next notify cycle.
        _TTL_SECS = 3

        sd_sock = open_sender_socket(tester_ip)
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port: int = notif_sock.getsockname()[1]

        try:

            def _send_sub_ttl() -> None:
                send_subscribe_eventgroup(
                    sd_sock,
                    (dut_ip, SD_PORT),
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_UDP_UNICAST,
                    MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=notif_port,
                    ttl=_TTL_SECS,
                )

            _send_sub_ttl()
            acks = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_send_sub_ttl,
            )
            assert any(e.eventgroup_id == EVENTGROUP_UDP_UNICAST and e.ttl > 0 for e in acks), (
                "ETS_095: Prerequisite failed, no SubscribeEventgroupAck received"
            )

            # Drain notifications sent during the active subscription period
            # so they don't contaminate the post-expiry observation window.
            pre_expiry = capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=4.0)
            assert pre_expiry, "ETS_095: No notifications received before TTL expiry"

            # Wait for TTL to expire (TTL + 2 s margin); do NOT renew.
            time.sleep(_TTL_SECS + 2)

            # Verify no notifications arrive after TTL expiry.
            post_expiry = capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=3.0)
            assert not post_expiry, (
                f"ETS_095: {len(post_expiry)} NOTIFICATION(s) received after "
                f"subscription TTL expired (TTL={_TTL_SECS} s + 2 s margin). "
                "Server must cease sending events once the subscription TTL elapses."
            )
        finally:
            sd_sock.close()
            notif_sock.close()


class TestSDFindServiceAdvanced:
    """ETS_091/101/121/128/130 and one project specific test: FindService and offer lifecycle scenarios."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_091_session_id_increments(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """ETS_091: Successive SD messages have monotonically incrementing session_id (with 16-bit wrap-around)."""
        assert dut.poll() is None, "DUT is not running"

        mc_sock = open_multicast_socket(host_ip)
        try:
            session_ids: list[int] = []
            deadline = time.monotonic() + 8.0
            while time.monotonic() < deadline and len(session_ids) < 3:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                mc_sock.settimeout(min(remaining, 1.0))
                try:
                    data, _ = mc_sock.recvfrom(65535)
                except socket.timeout:
                    continue
                try:
                    someip_msg, _ = SOMEIPHeader.parse(data)
                    if someip_msg.service_id != 0xFFFF:
                        continue
                    sd_hdr, _ = SOMEIPSDHeader.parse(someip_msg.payload)
                    has_offer = any(
                        e.sd_type == SOMEIPSDEntryType.OfferService and e.service_id == SERVICE_ID
                        for e in sd_hdr.entries
                    )
                    if has_offer:
                        session_ids.append(someip_msg.session_id)
                except Exception:  # noqa: BLE001
                    continue
        finally:
            mc_sock.close()

        assert len(session_ids) >= 2, (
            f"ETS_091: Only {len(session_ids)} OfferService packet(s) captured "
            "(need at least 2 to check session_id monotonicity)"
        )
        for i in range(len(session_ids) - 1):
            current = session_ids[i]
            nxt = session_ids[i + 1]
            # Allow wrap-around at 0xFFFF (session_id is 16-bit).
            is_increment = nxt == (current % 0xFFFF) + 1
            assert is_increment, f"ETS_091: session_id did not increment monotonically: {current:#06x} -> {nxt:#06x}"

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @_xfail_no_initial_fields("ETS_121")
    def test_ets_121_events_sent_after_subscribe_ack(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_121 (SD_Initial_Events_after_SubscribeEventgroup): the initial fields arrive after the ACK."""
        assert dut.poll() is None, "DUT is not running"

        sd_sock = open_sender_socket(tester_ip)
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port: int = notif_sock.getsockname()[1]

        try:

            def _subscribe() -> None:
                send_subscribe_eventgroup(
                    sd_sock,
                    (dut_ip, SD_PORT),
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_TCP_RELIABLE,
                    MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=notif_port,
                )

            _subscribe()
            acks = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_subscribe,
            )
            assert any(e.eventgroup_id == EVENTGROUP_TCP_RELIABLE and e.ttl > 0 for e in acks), (
                "ETS_121: Prerequisite failed, no SubscribeAck received"
            )

            notifs = capture_some_ip_messages(notif_sock, SERVICE_ID, timeout_secs=5.0)
            _assert_initial_fields(notifs, "ETS_121")
        finally:
            sd_sock.close()
            notif_sock.close()

    def test_ets_101_stop_offer_ceases_client_events(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_101: not applicable, the DUT has no ETS client mode (clientServiceActivate)."""
        pytest.skip("The DUT has no ETS client mode (clientServiceActivate).")

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=False,
        reason=(
            "OA ETS_128: a multicast FindService with Any Major Version or Any Minor Version is "
            "expected to be answered with a unicast OfferService. "
            "Observed with the current DUT stack: no unicast OfferService is received"
        ),
    )
    def test_ets_128_multicast_findservice_version_any(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_128: Multicast FindService with Any Major Version (0xFF), then with Any Minor Version
        (0xFFFFFFFF), is answered with a unicast OfferService."""
        assert dut.poll() is None, "DUT is not running"

        phases = (
            ("Any Major Version (0xFF)", {"major_version": 0xFF, "minor_version": MINOR_VERSION}),
            ("Any Minor Version (0xFFFFFFFF)", {"major_version": MAJOR_VERSION, "minor_version": 0xFFFFFFFF}),
        )
        sock = open_sender_socket(tester_ip)
        try:
            for name, version_fields in phases:
                for _ in range(10):
                    send_find_service(
                        sock,
                        (SD_MULTICAST_ADDR, SD_PORT),
                        service_id=SERVICE_ID,
                        instance_id=INSTANCE_ID,
                        **version_fields,
                    )
                    time.sleep(0.1)
                entries = capture_unicast_sd_entries(
                    sock,
                    filter_types=(SOMEIPSDEntryType.OfferService,),
                    timeout_secs=3.0,
                )
                assert any(e.service_id == SERVICE_ID for e in entries), (
                    f"ETS_128: No unicast OfferService received for multicast FindService with {name}"
                )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=False,
        reason=(
            "OA ETS_130: a multicast FindService with the Unicast Flag cleared is expected to be "
            "answered with a unicast OfferService. "
            "Observed with the current DUT stack: no unicast OfferService is received"
        ),
    )
    def test_ets_130_multicast_findservice_unicast_flag_clear(
        self,
        dut: subprocess.Popen[bytes],
        host_ip: str,
        dut_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_130: Multicast FindService with the Unicast Flag cleared is answered with a unicast OfferService."""
        assert dut.poll() is None, "DUT is not running"

        from helpers.sd_malformed import build_raw_sd_packet, _find_service_entry_bytes  # noqa: PLC0415

        # SD flags: reboot bit (0x80) set, unicast bit (0x40) clear, resulting byte = 0x80.
        pkt = build_raw_sd_packet(
            flags=0x80,
            entries_bytes=_find_service_entry_bytes(
                service_id=SERVICE_ID,
                instance_id=INSTANCE_ID,
                major_version=MAJOR_VERSION,
                minor_version=MINOR_VERSION,
            ),
        )
        sock = open_sender_socket(tester_ip)
        try:

            def _send() -> None:
                sock.sendto(pkt, (SD_MULTICAST_ADDR, SD_PORT))

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.OfferService,),
                timeout_secs=5.0,
                resend=_send,
            )
            assert any(e.service_id == SERVICE_ID for e in entries), (
                "ETS_130: No unicast OfferService received after multicast FindService "
                "with the Unicast Flag cleared. The DUT must ignore the flag."
            )
        finally:
            sock.close()
