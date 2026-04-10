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
"""TC8 Service Discovery tests — TC8-SD-001 through TC8-SD-014.

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import ipaddress
import socket
import subprocess
import time
import pytest

from attribute_plugin import add_test_properties

from helpers.sd_helpers import capture_sd_offers, open_multicast_socket
from helpers.sd_sender import (
    SOMEIPSDEntryType,
    capture_some_ip_messages,
    capture_unicast_sd_entries,
    open_sender_socket,
    send_find_service,
    send_subscribe_eventgroup,
    send_subscribe_eventgroup_reserved_set,
)
from helpers.someip_assertions import (
    assert_offer_has_ipv4_endpoint_option,
    assert_sd_offer_entry,
)
from helpers.constants import DUT_UNRELIABLE_PORT, SD_MULTICAST_ADDR, SD_PORT
from helpers.timing import capture_sd_offers_with_timestamps
from someip.header import SOMEIPHeader, SOMEIPSDHeader

# ---------------------------------------------------------------------------
# Module-level configuration
# ---------------------------------------------------------------------------

#: SOME/IP stack config template used for all tests in this module.
SOMEIP_CONFIG: str = "tc8_someipd_sd.json"

#: Service and instance IDs declared in ``tc8_someipd_sd.json``.
_SERVICE_ID: int = 0x1234
_INSTANCE_ID: int = 0x5678
_EVENTGROUP_ID: int = 0x4455
_MULTICAST_EVENTGROUP_ID: int = 0x4465
_UNKNOWN_EVENTGROUP_ID: int = 0xBEEF
_UNKNOWN_SERVICE_ID: int = 0xBEEF

#: SD configuration values from ``tc8_someipd_sd.json``.
_CYCLIC_OFFER_DELAY_MS: float = 2000.0
_REQUEST_RESPONSE_DELAY_MS: float = 500.0

#: Unknown IDs used in negative tests.
_UNKNOWN_INSTANCE_ID: int = 0xBEEF

#: Defaults — no version configured in tc8_someipd_sd.json.
#: (0xFFFFFFFF is the FindService wildcard, not used in OfferService entries.)
_MAJOR_VERSION: int = 0x00
_MINOR_VERSION: int = 0x00000000


# ---------------------------------------------------------------------------
# TC8-SD-001 / TC8-SD-002 / TC8-SD-003 — offer format and cyclic timing
# ---------------------------------------------------------------------------


class TestSDOfferFormat:
    """TC8-SD-001/002/003: Offer presence, fields, and cyclic timing."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_001_multicast_offer_on_startup(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-SD-001: someipd sends at least one SD OfferService on multicast at startup."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        offers = capture_sd_offers(host_ip, min_count=1, timeout_secs=5.0)

        assert len(offers) >= 1, (
            "TC8-SD-001: No SD OfferService entries received within 5 s. "
            "Verify someipd is running and the SD multicast address/port matches config."
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_002_offer_entry_format(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-SD-002: OfferService entry has correct service ID, instance ID, version, and TTL."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        offers = capture_sd_offers(host_ip, min_count=1, timeout_secs=5.0)

        service_offers = [e for e in offers if e.service_id == _SERVICE_ID]
        assert service_offers, (
            f"TC8-SD-002: No OfferService entry found for service 0x{_SERVICE_ID:04x} "
            f"in {len(offers)} captured SD entries."
        )

        assert_sd_offer_entry(
            service_offers[0],
            expected_service_id=_SERVICE_ID,
            expected_instance_id=_INSTANCE_ID,
            expected_major_version=_MAJOR_VERSION,
            expected_minor_version=_MINOR_VERSION,
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_cyclic_timing"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_003_cyclic_offer_timing(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-SD-003: Offers repeat at cyclic_offer_delay ±20% in the main phase."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # Wait for main phase. Repetition phase ends ~1.5 s after first offer
        # (200+400+800 ms doubling, repetitions_max=3).  Add one cyclic gap
        # (2000 ms) to ensure we capture main-phase offers.
        time.sleep(3.5)

        # Capture enough offers to derive at least 2 inter-cycle gaps.
        # The DUT may send multiple SD packets per cycle (service + eventgroup),
        # so we capture extra and de-duplicate within a 500 ms window.
        timed = capture_sd_offers_with_timestamps(host_ip, count=5, timeout_secs=15.0)

        service_offers = [(ts, e) for ts, e in timed if e.service_id == _SERVICE_ID]
        assert len(service_offers) >= 2, (
            "TC8-SD-003: Not enough OfferService entries for timing analysis"
        )

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


# ---------------------------------------------------------------------------
# TC8-SD-004 / TC8-SD-005 — FindService response
# ---------------------------------------------------------------------------


class TestSDFindResponse:
    """TC8-SD-004/005: FindService response for known/unknown services."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_004_find_known_service_unicast_offer(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-004: FindService for offered service triggers a unicast OfferService."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # tester_ip differs from host_ip — both need SD_PORT (SD spec requirement).
        # Send FindService via unicast to the DUT (multicast delivery on loopback
        # between different 127.x addresses is unreliable in some environments).
        sock = open_sender_socket(tester_ip)
        try:

            def _send_find() -> None:
                send_find_service(
                    sock,
                    (host_ip, SD_PORT),
                    service_id=_SERVICE_ID,
                    instance_id=_INSTANCE_ID,
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
            service_offers = [e for e in entries if e.service_id == _SERVICE_ID]
            assert service_offers, (
                f"TC8-SD-004: No unicast OfferService received for service "
                f"0x{_SERVICE_ID:04x} within 5 s of FindService"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_005_find_unknown_service_no_response(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-005: FindService for unknown service does not trigger OfferService."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            send_find_service(
                sock,
                (SD_MULTICAST_ADDR, SD_PORT),
                service_id=_UNKNOWN_SERVICE_ID,
            )
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.OfferService,),
                timeout_secs=2.0,
            )
            unknown_offers = [e for e in entries if e.service_id == _UNKNOWN_SERVICE_ID]
            assert not unknown_offers, (
                f"TC8-SD-005: Unexpected OfferService received for unknown service "
                f"0x{_UNKNOWN_SERVICE_ID:04x}"
            )
        finally:
            sock.close()


# ---------------------------------------------------------------------------
# TC8-SD-006 / TC8-SD-007 / TC8-SD-008 — SubscribeEventgroup lifecycle
# ---------------------------------------------------------------------------


class TestSDSubscribeLifecycle:
    """TC8-SD-006/007/008: Subscribe Ack, Nack, and StopSubscribe."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_006_subscribe_valid_eventgroup_ack(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-006: Valid SubscribeEventgroup receives SubscribeEventgroupAck (TTL>0)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # DUT sends Ack back to the Subscribe source address.
        # DUT may defer the Ack until the next SD cycle (~2 s).
        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]  # = SD_PORT

            def _send_subscribe() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _EVENTGROUP_ID,
                    _MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            _send_subscribe()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_send_subscribe,
            )
            acks = [
                e for e in entries if e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0
            ]
            assert acks, (
                f"TC8-SD-006: No SubscribeEventgroupAck received for eventgroup "
                f"0x{_EVENTGROUP_ID:04x}"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_007_subscribe_unknown_eventgroup_nack(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-007: SubscribeEventgroup for unknown eventgroup receives Nack (TTL=0)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]  # = SD_PORT

            def _send_subscribe_unknown() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _UNKNOWN_EVENTGROUP_ID,
                    _MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            _send_subscribe_unknown()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_send_subscribe_unknown,
            )
            nacks = [
                e
                for e in entries
                if e.eventgroup_id == _UNKNOWN_EVENTGROUP_ID and e.ttl == 0
            ]
            assert nacks, (
                f"TC8-SD-007: No SubscribeEventgroupNack received for unknown eventgroup "
                f"0x{_UNKNOWN_EVENTGROUP_ID:04x} (expected SubscribeAck with TTL=0)"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_008_stop_subscribe_ceases_notifications(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-008: StopSubscribeEventgroup (TTL=0) ceases event notifications."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # sd_sock: SD sender at tester_ip:SD_PORT.
        # notif_sock: receives event notifications at tester_ip:<ephemeral>.
        sd_sock = open_sender_socket(tester_ip)
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port: int = notif_sock.getsockname()[1]

        try:
            # Subscribe — notifications will arrive on notif_sock.
            def _send_sub_008() -> None:
                send_subscribe_eventgroup(
                    sd_sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _EVENTGROUP_ID,
                    _MAJOR_VERSION,
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
            assert any(e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0 for e in acks), (
                "TC8-SD-008: Prerequisite failed — no SubscribeEventgroupAck received"
            )

            # Expect at least one notification (DUT fires notify() every 2 s).
            notifs = capture_some_ip_messages(notif_sock, _SERVICE_ID, timeout_secs=4.0)
            assert notifs, (
                "TC8-SD-008: No SOME/IP notifications received after subscribe"
            )

            # Stop subscription (TTL=0).
            send_subscribe_eventgroup(
                sd_sock,
                (host_ip, SD_PORT),
                _SERVICE_ID,
                _INSTANCE_ID,
                _EVENTGROUP_ID,
                _MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=notif_port,
                ttl=0,
            )

            # Verify no further notifications arrive within 4 s.
            post = capture_some_ip_messages(notif_sock, _SERVICE_ID, timeout_secs=4.0)
            assert not post, (
                f"TC8-SD-008: {len(post)} notification(s) received after StopSubscribeEventgroup"
            )
        finally:
            sd_sock.close()
            notif_sock.close()


# ---------------------------------------------------------------------------
# TC8-SD-011 — SD option format (IPv4 endpoint option)
# ---------------------------------------------------------------------------


class TestSDOptionFormat:
    """TC8-SD-011: OfferService entry carries a valid IPv4EndpointOption."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_endpoint_option"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_011_offer_ipv4_endpoint_option(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """TC8-SD-011: SD OFFER entry includes IPv4EndpointOption with correct address/port/proto."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # Capture raw SD packets so we can inspect options attached to entries.
        sock = open_multicast_socket(host_ip)
        try:
            deadline = time.monotonic() + 5.0
            found_entry = None
            while time.monotonic() < deadline and found_entry is None:
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
                    if someip_msg.service_id != 0xFFFF:
                        continue
                    sd_hdr, _ = SOMEIPSDHeader.parse(someip_msg.payload)
                    sd_hdr = (
                        sd_hdr.resolve_options()
                    )  # populate entry.options_1 from sd_hdr.options
                    for entry in sd_hdr.entries:
                        if (
                            entry.sd_type == SOMEIPSDEntryType.OfferService
                            and entry.service_id == _SERVICE_ID
                        ):
                            found_entry = entry
                            break
                except Exception:  # noqa: BLE001
                    continue
        finally:
            sock.close()

        assert found_entry is not None, (
            f"TC8-SD-011: No OfferService entry found for service 0x{_SERVICE_ID:04x} "
            "within 5 s"
        )
        assert_offer_has_ipv4_endpoint_option(
            found_entry,
            expected_ip=host_ip,
            expected_port=DUT_UNRELIABLE_PORT,
        )


# ---------------------------------------------------------------------------
# TC8-SD-012 — Reboot detection (isolated in test_sd_reboot.py)
# ---------------------------------------------------------------------------
# TC8-SD-012 tests are in tests/tc8_conformance/test_sd_reboot.py.
# They require their own someipd lifecycle (start/stop/restart) and cannot
# share the module-scoped someipd_dut fixture used by the other SD tests here.


# ---------------------------------------------------------------------------
# TC8-SD-013 — Multicast eventgroup option in SUBSCRIBE_ACK
# ---------------------------------------------------------------------------


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
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-013: Subscribing to multicast eventgroup 0x4465 yields a multicast endpoint option."""

        # On loopback interfaces, IP multicast routing is unavailable (127.x does not support
        # multicast). The DUT will not include a multicast endpoint option in SUBSCRIBE_ACK.
        # Require a real NIC.
        if ipaddress.ip_address(host_ip).is_loopback:
            pytest.skip(
                "TC8-SD-013: Multicast endpoint option in SUBSCRIBE_ACK requires a real NIC. "
                "Set TC8_HOST_IP to a non-loopback address (e.g. export TC8_HOST_IP=192.168.x.y)."
            )

        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]

            def _send_subscribe_multicast() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _MULTICAST_EVENTGROUP_ID,
                    _MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            _send_subscribe_multicast()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_send_subscribe_multicast,
            )
            acks = [
                e
                for e in entries
                if e.eventgroup_id == _MULTICAST_EVENTGROUP_ID and e.ttl > 0
            ]
            assert acks, (
                f"TC8-SD-013: No SubscribeEventgroupAck received for multicast "
                f"eventgroup 0x{_MULTICAST_EVENTGROUP_ID:04x}"
            )
            ack = acks[0]
            # The ACK for a multicast eventgroup must carry a multicast IPv4EndpointOption.
            from someip.header import (
                IPv4EndpointOption,
            )  # local import to keep module-level clean

            options = list(getattr(ack, "options_1", ())) + list(
                getattr(ack, "options_2", ())
            )
            multicast_opts = [
                o
                for o in options
                if isinstance(o, IPv4EndpointOption)
                and ipaddress.ip_address(str(o.address)).is_multicast
            ]
            assert multicast_opts, (
                f"TC8-SD-013: SUBSCRIBE_ACK for eventgroup 0x{_MULTICAST_EVENTGROUP_ID:04x} "
                f"does not carry a multicast IPv4EndpointOption. "
                f"Options found: {options}"
            )
        finally:
            sock.close()


# ---------------------------------------------------------------------------
# TC8-SD-014 — TTL expiry cleanup
# ---------------------------------------------------------------------------


class TestSDTTLExpiry:
    """TC8-SD-014: Subscription with finite TTL is cleaned up after expiry."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_tc8_sd_014_ttl_expiry_ceases_notifications(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """TC8-SD-014: No notifications after subscription TTL expires."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sd_sock = open_sender_socket(tester_ip)
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port: int = notif_sock.getsockname()[1]

        try:
            # Subscribe with a short TTL (3 seconds).
            _TTL_SECS = 3

            def _send_sub_ttl() -> None:
                send_subscribe_eventgroup(
                    sd_sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _EVENTGROUP_ID,
                    _MAJOR_VERSION,
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
            assert any(e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0 for e in acks), (
                "TC8-SD-014: Prerequisite failed — no SubscribeEventgroupAck received"
            )

            # Verify at least one notification arrives before TTL expiry.
            pre_expiry = capture_some_ip_messages(
                notif_sock, _SERVICE_ID, timeout_secs=4.0
            )
            assert pre_expiry, "TC8-SD-014: No notifications received before TTL expiry"

            # Wait for TTL to expire (TTL + 2 s margin).
            time.sleep(_TTL_SECS + 2)

            # Verify no further notifications arrive in a 3 s window.
            post_expiry = capture_some_ip_messages(
                notif_sock, _SERVICE_ID, timeout_secs=3.0
            )
            assert not post_expiry, (
                f"TC8-SD-014: {len(post_expiry)} notification(s) received after TTL expiry "
                f"({_TTL_SECS} s + 2 s margin)"
            )
        finally:
            sd_sock.close()
            notif_sock.close()


# ---------------------------------------------------------------------------
# SOMEIPSRV_SD_MESSAGE_01-06 — FindService version wildcard/specific matching
# ---------------------------------------------------------------------------


class TestSDVersionMatching:
    """SOMEIPSRV_SD_MESSAGE_01-06: FindService instance/version wildcard and specific matching."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_01_instance_wildcard(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_01: FindService with instance_id=0xFFFF returns specific instance."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:

            def _send() -> None:
                send_find_service(
                    sock,
                    (host_ip, SD_PORT),
                    service_id=_SERVICE_ID,
                    instance_id=0xFFFF,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.OfferService,),
                timeout_secs=5.0,
                resend=_send,
            )
            matching = [e for e in entries if e.service_id == _SERVICE_ID]
            assert matching, (
                "SOMEIPSRV_SD_MESSAGE_01: No OfferService received for instance_id=0xFFFF "
                "(wildcard) FindService"
            )
            assert matching[0].instance_id == _INSTANCE_ID, (
                f"SOMEIPSRV_SD_MESSAGE_01: OfferService instance_id "
                f"0x{matching[0].instance_id:04x} != 0x{_INSTANCE_ID:04x}"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_02_instance_specific(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_02: FindService with exact instance_id returns that instance."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:

            def _send() -> None:
                send_find_service(
                    sock,
                    (host_ip, SD_PORT),
                    service_id=_SERVICE_ID,
                    instance_id=_INSTANCE_ID,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.OfferService,),
                timeout_secs=5.0,
                resend=_send,
            )
            matching = [
                e
                for e in entries
                if e.service_id == _SERVICE_ID and e.instance_id == _INSTANCE_ID
            ]
            assert matching, (
                f"SOMEIPSRV_SD_MESSAGE_02: No OfferService received for specific "
                f"instance_id=0x{_INSTANCE_ID:04x}"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_03_major_version_wildcard(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_03: FindService with major_version=0xFF (any) returns service."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:

            def _send() -> None:
                send_find_service(
                    sock,
                    (host_ip, SD_PORT),
                    service_id=_SERVICE_ID,
                    instance_id=_INSTANCE_ID,
                    major_version=0xFF,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.OfferService,),
                timeout_secs=5.0,
                resend=_send,
            )
            matching = [e for e in entries if e.service_id == _SERVICE_ID]
            assert matching, (
                "SOMEIPSRV_SD_MESSAGE_03: No OfferService received for major_version=0xFF "
                "(wildcard) FindService"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_04_major_version_specific(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_04: FindService with exact major_version returns service."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:

            def _send() -> None:
                send_find_service(
                    sock,
                    (host_ip, SD_PORT),
                    service_id=_SERVICE_ID,
                    instance_id=_INSTANCE_ID,
                    major_version=_MAJOR_VERSION,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.OfferService,),
                timeout_secs=5.0,
                resend=_send,
            )
            matching = [
                e
                for e in entries
                if e.service_id == _SERVICE_ID and e.major_version == _MAJOR_VERSION
            ]
            assert matching, (
                f"SOMEIPSRV_SD_MESSAGE_04: No OfferService received for specific "
                f"major_version=0x{_MAJOR_VERSION:02x}"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_05_minor_version_wildcard(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_05: FindService with minor_version=0xFFFFFFFF (any) returns service."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:

            def _send() -> None:
                send_find_service(
                    sock,
                    (host_ip, SD_PORT),
                    service_id=_SERVICE_ID,
                    instance_id=_INSTANCE_ID,
                    major_version=_MAJOR_VERSION,
                    minor_version=0xFFFFFFFF,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.OfferService,),
                timeout_secs=5.0,
                resend=_send,
            )
            matching = [e for e in entries if e.service_id == _SERVICE_ID]
            assert matching, (
                "SOMEIPSRV_SD_MESSAGE_05: No OfferService received for minor_version=0xFFFFFFFF "
                "(wildcard) FindService"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_06_minor_version_specific(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_06: FindService with exact minor_version=0x00000000 returns service."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:

            def _send() -> None:
                send_find_service(
                    sock,
                    (host_ip, SD_PORT),
                    service_id=_SERVICE_ID,
                    instance_id=_INSTANCE_ID,
                    major_version=_MAJOR_VERSION,
                    minor_version=_MINOR_VERSION,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.OfferService,),
                timeout_secs=5.0,
                resend=_send,
            )
            matching = [e for e in entries if e.service_id == _SERVICE_ID]
            assert matching, (
                f"SOMEIPSRV_SD_MESSAGE_06: No OfferService received for specific "
                f"minor_version=0x{_MINOR_VERSION:08x}"
            )
        finally:
            sock.close()


# ---------------------------------------------------------------------------
# SOMEIPSRV_SD_MESSAGE_14-19 — SubscribeEventgroup NAck scenarios
# ---------------------------------------------------------------------------


class TestSDSubscribeNAck:
    """SOMEIPSRV_SD_MESSAGE_14-19: SubscribeEventgroup NAck scenarios.

    A SubscribeEventgroup NAck is a SubscribeAck SD entry (type 0x07) with TTL=0.
    """

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_14_wrong_major_version(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_14: Subscribe with wrong major_version receives NAck (TTL=0)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]

            def _send() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _EVENTGROUP_ID,
                    major_version=0xFF,  # DUT expects 0x00
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=3.0,
                resend=_send,
            )
            nacks = [
                e for e in entries if e.eventgroup_id == _EVENTGROUP_ID and e.ttl == 0
            ]
            assert nacks, (
                "SOMEIPSRV_SD_MESSAGE_14: No SubscribeEventgroupNAck (TTL=0) received "
                "for wrong major_version=0xFF"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_15_wrong_service_id(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_15: Subscribe to unknown service_id receives NAck (TTL=0).

        Per SOMEIPSRV_SD_MESSAGE_15 the DUT shall respond with a SubscribeEventgroupNAck
        (SubscribeAck entry with TTL=0).  The DUT sends a NAck for unknown
        service IDs — the response carries the same eventgroup_id as the request.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]

            def _send() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (host_ip, SD_PORT),
                    service_id=_UNKNOWN_SERVICE_ID,
                    instance_id=_INSTANCE_ID,
                    eventgroup_id=_EVENTGROUP_ID,
                    major_version=_MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=3.0,
                resend=_send,
            )
            nacks = [
                e for e in entries if e.eventgroup_id == _EVENTGROUP_ID and e.ttl == 0
            ]
            assert nacks, (
                f"SOMEIPSRV_SD_MESSAGE_15: No SubscribeEventgroupNAck (TTL=0) received "
                f"for unknown service_id=0x{_UNKNOWN_SERVICE_ID:04x}"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_16_wrong_instance_id(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_16: Subscribe to wrong instance_id receives NAck (TTL=0)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]

            def _send() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    instance_id=_UNKNOWN_INSTANCE_ID,
                    eventgroup_id=_EVENTGROUP_ID,
                    major_version=_MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=3.0,
                resend=_send,
            )
            nacks = [
                e for e in entries if e.eventgroup_id == _EVENTGROUP_ID and e.ttl == 0
            ]
            assert nacks, (
                "SOMEIPSRV_SD_MESSAGE_16: No SubscribeEventgroupNAck (TTL=0) received "
                f"for wrong instance_id=0x{_UNKNOWN_INSTANCE_ID:04x}"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_17_unknown_eventgroup_id(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_17: Subscribe to unknown eventgroup_id receives NAck (TTL=0)."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        # This reuses the same scenario as TC8-SD-007 with an explicit SD_MESSAGE_17 trace.
        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]

            def _send() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    eventgroup_id=_UNKNOWN_EVENTGROUP_ID,
                    major_version=_MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_send,
            )
            nacks = [
                e
                for e in entries
                if e.eventgroup_id == _UNKNOWN_EVENTGROUP_ID and e.ttl == 0
            ]
            assert nacks, (
                f"SOMEIPSRV_SD_MESSAGE_17: No SubscribeEventgroupNAck (TTL=0) received "
                f"for unknown eventgroup_id=0x{_UNKNOWN_EVENTGROUP_ID:04x}"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_message_18_ttl_zero_stop_subscribe(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_18: StopSubscribeEventgroup (TTL=0) produces no SubscribeAck."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]
            # Send a StopSubscribe directly (TTL=0) without a prior subscribe.
            # Per spec, a TTL=0 subscribe is a StopSubscribe and must not generate an Ack.
            send_subscribe_eventgroup(
                sock,
                (host_ip, SD_PORT),
                _SERVICE_ID,
                _INSTANCE_ID,
                _EVENTGROUP_ID,
                _MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=sender_port,
                ttl=0,
            )
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=2.0,
            )
            acks = [
                e for e in entries if e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0
            ]
            assert not acks, (
                f"SOMEIPSRV_SD_MESSAGE_18: Unexpected SubscribeAck(TTL>0) received "
                f"in response to StopSubscribeEventgroup (TTL=0). Got {len(acks)} entry/ies."
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason=(
            "vsomeip 3.6.1 limitation (SOMEIPSRV_SD_MESSAGE_19): DUT sends a positive "
            "SubscribeEventgroupAck (TTL > 0) even when reserved bits are set in the "
            "Subscribe entry; spec requires a NAck (TTL = 0). "
            "See docs/architecture/tc8_conformance_testing.rst "
            "§Known SOME/IP Stack Limitations."
        ),
    )
    def test_sd_message_19_reserved_field_set(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_MESSAGE_19: Subscribe with reserved field set receives NAck or is ignored."""
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]

            def _send() -> None:
                send_subscribe_eventgroup_reserved_set(
                    sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _EVENTGROUP_ID,
                    _MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                    reserved_value=0x0F,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=3.0,
                resend=_send,
            )
            # Accept either: a NAck (TTL=0) or no response at all (DUT silently ignores).
            # A positive Ack (TTL>0) would indicate the DUT accepted the malformed entry.
            acks = [
                e for e in entries if e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0
            ]
            assert not acks, (
                "SOMEIPSRV_SD_MESSAGE_19: DUT sent a positive SubscribeAck (TTL>0) for a "
                "SubscribeEventgroup entry with reserved bits set. Expected NAck or no response."
            )
        finally:
            sock.close()


# ---------------------------------------------------------------------------
# SOMEIPSRV_SD_BEHAVIOR_03-04 — FindService response timing
# ---------------------------------------------------------------------------


class TestSDFindServiceTiming:
    """SOMEIPSRV_SD_BEHAVIOR_03-04: FindService response timing constraints."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_cyclic_timing"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_behavior_03_unicast_findservice_timing(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_BEHAVIOR_03: Unicast FindService response arrives within request_response_delay * 1.5.

        The DUT is in its main phase (the module-scoped someipd_dut fixture has been
        running for the full test session).  Per spec the DUT must respond within
        ``request_response_delay`` (500 ms); we allow 1.5x = 750 ms per implementation
        tolerance.  If the cyclic offer fires within that window it also satisfies the
        test.  We resend every 600 ms so the measurement window starts fresh each send.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            # Resend interval < request_response_delay so each send-to-response window
            # is well-contained.  Measure individually: send, wait up to 750 ms,
            # record elapsed, assert.  Repeat up to 3 times to avoid a cyclic offer
            # coincidence that swamps the measurement.
            _max_allowed_ms = _REQUEST_RESPONSE_DELAY_MS * 1.5

            for _ in range(3):
                t0 = time.monotonic()
                send_find_service(
                    sock,
                    (host_ip, SD_PORT),
                    service_id=_SERVICE_ID,
                    instance_id=_INSTANCE_ID,
                    major_version=_MAJOR_VERSION,
                )
                # Wait only for the allowed window.
                entries = capture_unicast_sd_entries(
                    sock,
                    filter_types=(SOMEIPSDEntryType.OfferService,),
                    timeout_secs=_max_allowed_ms / 1000.0,
                    max_results=1,
                )
                t1 = time.monotonic()
                matching = [e for e in entries if e.service_id == _SERVICE_ID]
                if matching:
                    elapsed_ms = (t1 - t0) * 1000.0
                    assert elapsed_ms <= _max_allowed_ms, (
                        f"SOMEIPSRV_SD_BEHAVIOR_03: OfferService arrived in {elapsed_ms:.0f} ms, "
                        f"exceeds request_response_delay * 1.5 = {_max_allowed_ms:.0f} ms "
                        f"(configured request_response_delay={_REQUEST_RESPONSE_DELAY_MS:.0f} ms)"
                    )
                    return  # test passes on first successful attempt
                # No response in the tight window — the cyclic offer may have just fired.
                # Drain any pending offers and retry.
                capture_unicast_sd_entries(
                    sock,
                    filter_types=(SOMEIPSDEntryType.OfferService,),
                    timeout_secs=_CYCLIC_OFFER_DELAY_MS / 1000.0,
                    max_results=1,
                )

            pytest.fail(
                "SOMEIPSRV_SD_BEHAVIOR_03: No OfferService received within "
                f"request_response_delay * 1.5 = {_max_allowed_ms:.0f} ms "
                "in 3 consecutive attempts"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_cyclic_timing"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_sd_behavior_04_multicast_findservice_timing(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """SOMEIPSRV_SD_BEHAVIOR_04: Multicast FindService (Unicast flag=0) triggers a multicast OfferService.

        A FindService sent to the SD multicast group shall be answered with a multicast
        OfferService response.  This test captures the response on the SD multicast
        socket (not the unicast sender socket) and verifies it arrives within
        ``cyclic_offer_delay * 1.5`` of the FindService transmission.

        On a loopback interface the multicast response arrives on the socket that has
        joined the SD multicast group (``open_multicast_socket``).  Both the sender
        and listener sockets are opened so the FindService can be injected while the
        multicast socket is actively listening.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        send_sock = open_sender_socket(tester_ip)
        mc_sock = open_multicast_socket(host_ip)
        try:
            t0 = time.monotonic()
            send_find_service(
                send_sock,
                (SD_MULTICAST_ADDR, SD_PORT),
                service_id=_SERVICE_ID,
                instance_id=0xFFFF,
                major_version=0xFF,
            )

            # Listen on the multicast socket for the DUT's OfferService response.
            # The response may be the next scheduled cyclic offer or a triggered offer.
            # Allow up to cyclic_offer_delay * 1.5 for the first offer to arrive.
            _max_allowed_secs = (_CYCLIC_OFFER_DELAY_MS * 1.5) / 1000.0
            deadline = time.monotonic() + _max_allowed_secs
            matching = []
            while time.monotonic() < deadline and not matching:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                mc_sock.settimeout(min(remaining, 0.5))
                try:
                    data, _ = mc_sock.recvfrom(65535)
                except socket.timeout:
                    continue
                try:
                    someip_msg, _ = SOMEIPHeader.parse(data)
                    if someip_msg.service_id != 0xFFFF:
                        continue
                    sd_hdr, _ = SOMEIPSDHeader.parse(someip_msg.payload)
                    for entry in sd_hdr.entries:
                        if (
                            entry.sd_type == SOMEIPSDEntryType.OfferService
                            and entry.service_id == _SERVICE_ID
                        ):
                            matching.append(entry)
                except Exception:  # noqa: BLE001
                    continue
            t1 = time.monotonic()

            assert matching, (
                "SOMEIPSRV_SD_BEHAVIOR_04: No multicast OfferService received after "
                f"multicast FindService within {_max_allowed_secs:.1f} s "
                f"(cyclic_offer_delay * 1.5 = {_CYCLIC_OFFER_DELAY_MS * 1.5:.0f} ms)"
            )
            elapsed_ms = (t1 - t0) * 1000.0
            max_allowed_ms = _CYCLIC_OFFER_DELAY_MS * 1.5
            assert elapsed_ms <= max_allowed_ms, (
                f"SOMEIPSRV_SD_BEHAVIOR_04: OfferService arrived in {elapsed_ms:.0f} ms, "
                f"exceeds cyclic_offer_delay * 1.5 = {max_allowed_ms:.0f} ms"
            )
        finally:
            send_sock.close()
            mc_sock.close()


# ---------------------------------------------------------------------------
# ETS_088/092/098/107/120/122/155 — Multi-subscribe and lifecycle edge cases
# ---------------------------------------------------------------------------


class TestSDSubscribeLifecycleAdvanced:
    """ETS_088/092/098/107/120/122/155: Multi-subscribe and lifecycle edge cases."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_088_two_subscribes_same_session(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_088: Two SubscribeEventgroup entries (different eventgroups) both receive ACKs.

        The spec requires the DUT to process multiple subscribe entries even when
        sent in rapid succession.  We send two separate SD messages (one per
        eventgroup) and assert that both receive a SubscribeAck with TTL > 0.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]

            def _subscribe_eg1() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _EVENTGROUP_ID,
                    _MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            def _subscribe_eg2() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _MULTICAST_EVENTGROUP_ID,
                    _MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            _subscribe_eg1()
            _subscribe_eg2()

            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=6.0,
                resend=lambda: (_subscribe_eg1(), _subscribe_eg2()),  # type: ignore[func-returns-value]
            )
            acks_eg1 = [
                e for e in entries if e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0
            ]
            acks_eg2 = [
                e
                for e in entries
                if e.eventgroup_id == _MULTICAST_EVENTGROUP_ID and e.ttl > 0
            ]
            assert acks_eg1, (
                f"ETS_088: No SubscribeAck received for eventgroup 0x{_EVENTGROUP_ID:04x}"
            )
            assert acks_eg2, (
                f"ETS_088: No SubscribeAck received for eventgroup "
                f"0x{_MULTICAST_EVENTGROUP_ID:04x}"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_092_ttl_zero_stop_subscribe_no_nack(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_092: SubscribeEventgroup with TTL=0 is treated as StopSubscribe — no NAck sent.

        Per PRS_SOMEIPSD_00386 and PRS_SOMEIPSD_00387 a subscribe entry with TTL=0 is
        a StopSubscribeEventgroup. The DUT must not send a SubscribeAck (positive or
        negative) in response.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]
            send_subscribe_eventgroup(
                sock,
                (host_ip, SD_PORT),
                _SERVICE_ID,
                _INSTANCE_ID,
                _EVENTGROUP_ID,
                _MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=sender_port,
                ttl=0,
            )
            # No resend — a StopSubscribe should never produce an ACK.
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=2.0,
            )
            # Per spec a NAck (TTL=0 Ack) must NOT be sent for a stop-subscribe.
            nacks = [
                e for e in entries if e.eventgroup_id == _EVENTGROUP_ID and e.ttl == 0
            ]
            assert not nacks, (
                f"ETS_092: DUT sent NAck (TTL=0 SubscribeAck) in response to "
                f"StopSubscribeEventgroup (TTL=0). Got {len(nacks)} NAck(s). "
                "StopSubscribe must not trigger any acknowledgement."
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_098_subscribe_accepted_without_prior_rpc(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_098: SubscribeEventgroup is accepted without a prior method call.

        A server must not require the client to invoke a method before accepting
        an eventgroup subscription.  Verify a positive ACK (TTL > 0) is received.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]

            def _send() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _EVENTGROUP_ID,
                    _MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_send,
            )
            acks = [
                e for e in entries if e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0
            ]
            assert acks, (
                "ETS_098: No SubscribeAck (TTL>0) received without a prior method call. "
                "Server must accept subscriptions unconditionally."
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_107_find_service_and_subscribe_processed_independently(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_107: DUT processes SD entries independently of arrival order.

        Send a FindService immediately followed by a SubscribeEventgroup in rapid
        succession (two separate packets).  The DUT must process both entries
        independently regardless of their order in the stream.

        Verification strategy:
        - FindService response (OfferService) is captured on the SD multicast
          socket because the DUT (server) responds to incoming FindService messages on multicast.
        - SubscribeAck arrives on the unicast sender socket.
        Both arriving confirms the DUT processed both entries.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sd_sock = open_sender_socket(tester_ip)
        mc_sock = open_multicast_socket(host_ip)
        try:
            sender_port = sd_sock.getsockname()[1]

            def _send_both() -> None:
                send_find_service(
                    sd_sock,
                    (SD_MULTICAST_ADDR, SD_PORT),
                    service_id=_SERVICE_ID,
                    instance_id=_INSTANCE_ID,
                )
                send_subscribe_eventgroup(
                    sd_sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _EVENTGROUP_ID,
                    _MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            _send_both()

            # Capture SubscribeAck on the unicast sender socket.
            acks = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=6.0,
                resend=_send_both,
            )
            acks_valid = [
                e for e in acks if e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0
            ]

            # Capture OfferService on the multicast socket (FindService response).
            _max_secs = (_CYCLIC_OFFER_DELAY_MS * 1.5) / 1000.0
            deadline = time.monotonic() + _max_secs
            offers: list = []
            while time.monotonic() < deadline and not offers:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                mc_sock.settimeout(min(remaining, 0.5))
                try:
                    data, _ = mc_sock.recvfrom(65535)
                except socket.timeout:
                    continue
                try:
                    someip_msg, _ = SOMEIPHeader.parse(data)
                    if someip_msg.service_id != 0xFFFF:
                        continue
                    sd_hdr, _ = SOMEIPSDHeader.parse(someip_msg.payload)
                    for entry in sd_hdr.entries:
                        if (
                            entry.sd_type == SOMEIPSDEntryType.OfferService
                            and entry.service_id == _SERVICE_ID
                        ):
                            offers.append(entry)
                except Exception:  # noqa: BLE001
                    continue

            assert offers, (
                "ETS_107: No OfferService received on multicast after FindService burst. "
                "DUT must process FindService independently of co-arriving SD entries."
            )
            assert acks_valid, (
                "ETS_107: No SubscribeAck received after SubscribeEventgroup burst. "
                "DUT must process SubscribeEventgroup independently of co-arriving SD entries."
            )
        finally:
            sd_sock.close()
            mc_sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_120_subscribe_endpoint_ip_matches_tester(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_120: SubscribeEventgroup with explicit subscriber IP receives OfferService ACK.

        The subscribe endpoint carries tester_ip as the subscriber address.
        The DUT must send the ACK to that IP.  Verifying the ACK arrives on the
        tester socket confirms the DUT correctly used the subscriber_ip field.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            sender_port = sock.getsockname()[1]

            def _send() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _EVENTGROUP_ID,
                    _MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=sender_port,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_send,
            )
            acks = [
                e for e in entries if e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0
            ]
            assert acks, (
                f"ETS_120: No SubscribeAck received at tester_ip={tester_ip} "
                f"for subscribe with explicit subscriber_ip."
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_122_sd_interface_version_is_one(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """ETS_122: SOME/IP-SD messages carry interface_version = 0x01.

        Per PRS_SOMEIPSD_00357 and PRS_SOMEIPSD_00360 the interface_version field
        in the SOME/IP outer header of SD messages must be fixed at 0x01.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        mc_sock = open_multicast_socket(host_ip)
        try:
            deadline = time.monotonic() + 6.0
            found: list = []
            while time.monotonic() < deadline and not found:
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
                    found.append(someip_msg)
                except Exception:  # noqa: BLE001
                    continue
        finally:
            mc_sock.close()

        assert found, "ETS_122: No SOME/IP-SD messages captured within 6 s"
        sd_msg = found[0]
        assert sd_msg.interface_version == 1, (
            f"ETS_122: SOME/IP-SD interface_version = {sd_msg.interface_version}, "
            "expected 0x01 per PRS_SOMEIPSD_00357, PRS_SOMEIPSD_00360"
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_155_resubscribe_after_stop(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_155: Re-subscribe after StopSubscribe receives a new ACK and resumes events.

        Lifecycle: Subscribe → ACK → StopSubscribe (TTL=0) → Subscribe → ACK.
        The DUT must accept the second subscription and resume event delivery.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sd_sock = open_sender_socket(tester_ip)
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port: int = notif_sock.getsockname()[1]

        try:

            def _subscribe() -> None:
                send_subscribe_eventgroup(
                    sd_sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _EVENTGROUP_ID,
                    _MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=notif_port,
                )

            # Step 1: Initial subscribe.
            _subscribe()
            acks1 = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_subscribe,
            )
            assert any(
                e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0 for e in acks1
            ), "ETS_155: Prerequisite failed — no initial SubscribeAck received"

            # Step 2: Stop subscribe.
            send_subscribe_eventgroup(
                sd_sock,
                (host_ip, SD_PORT),
                _SERVICE_ID,
                _INSTANCE_ID,
                _EVENTGROUP_ID,
                _MAJOR_VERSION,
                subscriber_ip=tester_ip,
                subscriber_port=notif_port,
                ttl=0,
            )
            time.sleep(0.5)  # Allow DUT to process the StopSubscribe.

            # Step 3: Re-subscribe — must be accepted again.
            _subscribe()
            acks2 = capture_unicast_sd_entries(
                sd_sock,
                filter_types=(SOMEIPSDEntryType.SubscribeAck,),
                timeout_secs=5.0,
                resend=_subscribe,
            )
            assert any(
                e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0 for e in acks2
            ), (
                "ETS_155: No SubscribeAck received after re-subscribe following StopSubscribe"
            )
        finally:
            sd_sock.close()
            notif_sock.close()


# ---------------------------------------------------------------------------
# ETS_091/099/100/128/130 — FindService and offer lifecycle advanced
# ---------------------------------------------------------------------------


class TestSDFindServiceAdvanced:
    """ETS_091/099/100/128/130: FindService and offer lifecycle advanced scenarios."""

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_091_session_id_increments(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
    ) -> None:
        """ETS_091: Successive SD messages have monotonically incrementing session_id.

        Capture at least 2 OfferService packets from the DUT and verify that
        each subsequent packet's session_id is greater than the previous one.
        The DUT emits cyclic offers every 2000 ms; allow up to 8 s to capture 3.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

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
                        e.sd_type == SOMEIPSDEntryType.OfferService
                        and e.service_id == _SERVICE_ID
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
            assert is_increment, (
                f"ETS_091: session_id did not increment monotonically: "
                f"{current:#06x} -> {nxt:#06x}"
            )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_099_initial_event_sent_after_subscribe(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_099: DUT sends initial field value as event after SubscribeEventgroup ACK.

        The eventgroup 0x4455 carries a field event (update-cycle=2000ms in config).
        After subscribing, the first notification must arrive within the cycle window.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sd_sock = open_sender_socket(tester_ip)
        notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        notif_sock.bind((tester_ip, 0))
        notif_port: int = notif_sock.getsockname()[1]

        try:

            def _subscribe() -> None:
                send_subscribe_eventgroup(
                    sd_sock,
                    (host_ip, SD_PORT),
                    _SERVICE_ID,
                    _INSTANCE_ID,
                    _EVENTGROUP_ID,
                    _MAJOR_VERSION,
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
            assert any(e.eventgroup_id == _EVENTGROUP_ID and e.ttl > 0 for e in acks), (
                "ETS_099: Prerequisite failed — no SubscribeAck received"
            )

            # Expect at least one notification (field sends initial value + cyclic updates).
            notifs = capture_some_ip_messages(notif_sock, _SERVICE_ID, timeout_secs=5.0)
            assert notifs, (
                "ETS_099: No SOME/IP notifications received after subscribe. "
                "DUT must send initial field value on subscription."
            )
        finally:
            sd_sock.close()
            notif_sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_offer_format"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_100_no_findservice_emitted_by_server(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_100: DUT (server) must not emit FindService entries in main phase.

        A SOME/IP server that has offered its service must not transmit
        FindService SD entries.  Capture unicast SD entries on the tester
        socket (which is bound at SD_PORT) for 5 s and assert none are
        FindService type from the DUT.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.FindService,),
                timeout_secs=5.0,
            )
            assert not entries, (
                f"ETS_100: DUT emitted {len(entries)} FindService SD entry/ies. "
                "A server in main phase must not send FindService."
            )
        finally:
            sock.close()

    def test_ets_101_stop_offer_ceases_client_events(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_101: DUT is server-only; client StopSubscribe reaction to server StopOfferService is not applicable."""
        pytest.skip(
            "DUT is server-only; client StopSubscribe reaction to server StopOfferService is not applicable."
        )

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_128_multicast_findservice_version_wildcard(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_128: Multicast FindService with major=0xFF/minor=0xFFFFFFFF triggers OfferService.

        Sending a FindService with wildcard version to the SD multicast address
        must cause the DUT to respond with an OfferService.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        sock = open_sender_socket(tester_ip)
        try:

            def _send() -> None:
                send_find_service(
                    sock,
                    (SD_MULTICAST_ADDR, SD_PORT),
                    service_id=_SERVICE_ID,
                    instance_id=0xFFFF,
                    major_version=0xFF,
                    minor_version=0xFFFFFFFF,
                )

            _send()
            entries = capture_unicast_sd_entries(
                sock,
                filter_types=(SOMEIPSDEntryType.OfferService,),
                timeout_secs=5.0,
                resend=_send,
            )
            matching = [e for e in entries if e.service_id == _SERVICE_ID]
            assert matching, (
                f"ETS_128: No OfferService received for multicast FindService "
                f"with wildcard version (major=0xFF, minor=0xFFFFFFFF)"
            )
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_find_response"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_130_multicast_findservice_unicast_flag_clear(
        self,
        someipd_dut: subprocess.Popen[bytes],
        host_ip: str,
        tester_ip: str,
    ) -> None:
        """ETS_130: FindService with unicast_flag=0 (flags byte bit 6 clear) is processed.

        Per SOME/IP-SD spec, the unicast flag (bit 6 of the SD flags byte) signals
        whether the sender supports unicast responses.  With the flag clear (0) the
        DUT may respond on multicast.  At minimum it must process the FindService
        and we capture any resulting offer on either socket.
        """
        assert someipd_dut.poll() is None, "someipd DUT is not running"

        from helpers.sd_malformed import build_raw_sd_packet, _find_service_entry_bytes  # noqa: PLC0415

        sock = open_sender_socket(tester_ip)
        mc_sock = open_multicast_socket(host_ip)
        try:
            # SD flags: reboot bit (0x80) set, unicast bit (0x40) clear → 0x80
            entry_bytes = _find_service_entry_bytes(
                service_id=_SERVICE_ID,
                instance_id=0xFFFF,
                major_version=0xFF,
                minor_version=0xFFFFFFFF,
            )
            pkt = build_raw_sd_packet(flags=0x80, entries_bytes=entry_bytes)
            sock.sendto(pkt, (SD_MULTICAST_ADDR, SD_PORT))

            # Capture on multicast socket (DUT may respond on multicast when flag=0).
            deadline = time.monotonic() + 5.0
            found: list = []
            while time.monotonic() < deadline and not found:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                mc_sock.settimeout(min(remaining, 0.5))
                try:
                    data, _ = mc_sock.recvfrom(65535)
                except socket.timeout:
                    continue
                try:
                    someip_msg, _ = SOMEIPHeader.parse(data)
                    if someip_msg.service_id != 0xFFFF:
                        continue
                    sd_hdr, _ = SOMEIPSDHeader.parse(someip_msg.payload)
                    for entry in sd_hdr.entries:
                        if (
                            entry.sd_type == SOMEIPSDEntryType.OfferService
                            and entry.service_id == _SERVICE_ID
                        ):
                            found.append(entry)
                except Exception:  # noqa: BLE001
                    continue

            # Also drain any unicast OfferService on the sender socket.
            if not found:
                unicast_entries = capture_unicast_sd_entries(
                    sock,
                    filter_types=(SOMEIPSDEntryType.OfferService,),
                    timeout_secs=0.5,
                )
                found.extend(e for e in unicast_entries if e.service_id == _SERVICE_ID)

            assert found, (
                "ETS_130: No OfferService received after multicast FindService "
                "with unicast_flag=0. DUT must still process the FindService entry."
            )
        finally:
            sock.close()
            mc_sock.close()
