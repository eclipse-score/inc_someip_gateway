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
"""TC8-SDI-001 to SDI-004: initial fields on subscribe and client reboot detection.

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import socket
import subprocess
import time
from typing import List, Set

import pytest
from attribute_plugin import add_test_properties

from helpers.constants import (
    EVENT_FIELD_UINT8,
    EVENT_FIELD_UINT8_ARRAY,
    EVENT_INTERFACE_VERSION,
    EVENT_TEST_UINT8,
    EVENTGROUP_UNRELIABLE_EVENTS_AND_FIELDS,
    EVENTGROUP_ALL_EVENTS_AND_FIELDS,
    INSTANCE_ID,
    MAJOR_VERSION,
    SD_MULTICAST_ADDR,
    SD_PORT,
    SERVICE_ID,
)
from helpers.sd_sender import (
    capture_some_ip_messages,
    capture_unicast_sd_entries,
    open_sender_socket,
    send_find_service,
    send_subscribe_eventgroup,
)
from helpers.someip_types import SOMEIPSDEntryType

_INITIAL_FIELDS = (EVENT_INTERFACE_VERSION, EVENT_FIELD_UINT8, EVENT_FIELD_UINT8_ARRAY)
_ACK_SECS: float = 5.0
_EVENT_SECS: float = 4.0
_STEP_SECS: float = 2.0
_STOP_SETTLE_SECS: float = 0.5
_APP_SETTLE_SECS: float = 6.0
_SUBSCRIPTION_TTL_SECS: int = 3
_LONG_TTL: int = 0xFFFFFF


class _Tester:
    """Tester sockets: SD socket plus the endpoint that receives the events.

    Every SD message sets the reboot flag: the tester simulates a freshly started sender whose Session ID
    has not wrapped yet (OA SOMEIPSRV_FORMAT_07).
    """

    def __init__(self, dut_ip: str, tester_ip: str) -> None:
        self.dut_ip = dut_ip
        self.tester_ip = tester_ip
        self.sd_sock = open_sender_socket(tester_ip)
        self.event_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.event_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.event_sock.bind((tester_ip, 0))
        self.event_port: int = self.event_sock.getsockname()[1]
        self.subscribed: Set[int] = set()

    def close(self) -> None:
        """Stop the subscriptions, so that the next test subscribes anew."""
        for eventgroup_id in self.subscribed:
            self.subscribe(eventgroup_id, ttl=0)
        time.sleep(_STOP_SETTLE_SECS)
        self.sd_sock.close()
        self.event_sock.close()

    def find(self, session_id: int, multicast: bool = False) -> None:
        dest = SD_MULTICAST_ADDR if multicast else self.dut_ip
        send_find_service(
            self.sd_sock, (dest, SD_PORT), SERVICE_ID, INSTANCE_ID, session_id=session_id, flag_reboot=True
        )

    def subscribe(self, eventgroup_id: int, session_id: int = 0, ttl: int = _SUBSCRIPTION_TTL_SECS) -> None:
        send_subscribe_eventgroup(
            self.sd_sock,
            (self.dut_ip, SD_PORT),
            SERVICE_ID,
            INSTANCE_ID,
            eventgroup_id,
            MAJOR_VERSION,
            subscriber_ip=self.tester_ip,
            subscriber_port=self.event_port,
            ttl=ttl,
            session_id=session_id,
            flag_reboot=True,
        )

    def subscribe_for_ack(
        self, label: str, eventgroup_id: int, session_id: int = 0, ttl: int = _SUBSCRIPTION_TTL_SECS
    ) -> None:
        """Send one SubscribeEventgroup, never repeated (a repeat would reuse the Session ID), and assert the Ack."""
        self.subscribed.add(eventgroup_id)
        self.subscribe(eventgroup_id, session_id, ttl)
        entries = capture_unicast_sd_entries(
            self.sd_sock, filter_types=(SOMEIPSDEntryType.SubscribeAck,), timeout_secs=_ACK_SECS, max_results=1
        )
        assert any(e.eventgroup_id == eventgroup_id and e.ttl > 0 for e in entries), (
            f"{label}: No SubscribeEventgroupAck for eventgroup 0x{eventgroup_id:04x} within {_ACK_SECS:.0f} s"
        )

    def events(self, secs: float) -> Set[int]:
        """Return the Method IDs of the events of ``SERVICE_ID`` received within *secs*."""
        return {m.method_id for m in capture_some_ip_messages(self.event_sock, SERVICE_ID, secs)}


def _assert_initial_fields(label: str, received: Set[int]) -> None:
    missing = [f"0x{f:04x}" for f in _INITIAL_FIELDS if f not in received]
    assert not missing, (
        f"{label}: initial field(s) {missing} not received, got events {sorted(hex(m) for m in received)}"
    )


# One class per test: the DUT fixture is class scoped, so each test gets a freshly started DUT.
class TestEts086:
    @add_test_properties(
        partially_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason=(
            "TC8-SDI-001 / OA ETS_086: after SubscribeEventgroupAck the DUT sends the initial events 0x8005, 0x8006 and 0x8007 "
            "to the subscriber. Observed: they are not received when the subscription follows their one-time "
            "publication by the DUT application."
        ),
    )
    def test_ets_086_events_and_fields_all_2(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDI-001 (ETS_086): SubscribeEventgroupAck and the initial fields of eventgroup 0x0002 (field 0x8008 not checked)."""
        assert dut.poll() is None, "DUT is not running"
        tester = _Tester(dut_ip, tester_ip)
        try:
            time.sleep(_APP_SETTLE_SECS)  # the DUT application has published its field values by now
            tester.subscribe_for_ack("ETS_086", EVENTGROUP_ALL_EVENTS_AND_FIELDS)
            _assert_initial_fields("ETS_086", tester.events(_EVENT_SECS))
        finally:
            tester.close()


class TestEts087:
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason=(
            "TC8-SDI-002 / OA ETS_087: after SubscribeEventgroupAck the DUT sends the initial events 0x8005, 0x8006 and 0x8007 "
            "to the subscriber. Observed: they are not received when the subscription follows their one-time "
            "publication by the DUT application."
        ),
    )
    def test_ets_087_events_and_fields_unreliable_5(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDI-002 (ETS_087): SubscribeEventgroupAck and the initial fields of eventgroup 0x0005."""
        assert dut.poll() is None, "DUT is not running"
        tester = _Tester(dut_ip, tester_ip)
        try:
            time.sleep(_APP_SETTLE_SECS)  # the DUT application has published its field values by now
            tester.subscribe_for_ack("ETS_087", EVENTGROUP_UNRELIABLE_EVENTS_AND_FIELDS)
            _assert_initial_fields("ETS_087", tester.events(_EVENT_SECS))
        finally:
            tester.close()


class TestEts093:
    # No application-only fix: the application cannot send initial events on subscription.
    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_reboot"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason=(
            "TC8-SDI-003 / OA ETS_093: the initial event 0x8005 is expected after a new subscription and again after the "
            "client reboot (Session ID 1). Observed: 0x8005 is received only after the first subscription of part A, "
            "not after Session ID 1 in part A, nor after Session IDs 4 and 1 in part B."
        ),
    )
    def test_ets_093_reboot_detection_multicast_and_unicast(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDI-003 (ETS_093): reboot detection for multicast and unicast messages, kept separate."""
        assert dut.poll() is None, "DUT is not running"
        tester = _Tester(dut_ip, tester_ip)
        label = "ETS_093"
        eg = EVENTGROUP_UNRELIABLE_EVENTS_AND_FIELDS
        deviations: List[str] = []
        try:

            def _step(session_id: int, find: bool) -> bool:
                """Subscribe with *session_id* (after a multicast FindService if *find*), return whether 0x8005 arrived."""
                if find:
                    tester.find(session_id, multicast=True)
                tester.subscribe_for_ack(label, eg, session_id)
                return EVENT_INTERFACE_VERSION in tester.events(_STEP_SECS)

            for part, find in (("A", True), ("B", False)):
                if part == "B":
                    time.sleep(_SUBSCRIPTION_TTL_SECS + 1)  # let the subscription of part A expire
                first, second, third = (_step(4, find), _step(5, find), _step(1, find))
                if not first:
                    deviations.append(f"{part}: no initial event 0x8005 after the first subscription (Session ID 4)")
                if second:
                    deviations.append(f"{part}: initial event 0x8005 sent again after Session ID 5")
                if not third:
                    deviations.append(f"{part}: no initial event 0x8005 after Session ID 1 (client reboot)")

            # C: rising Session IDs per kind, unicast lower than multicast, no reboot.
            tester.events(0.2)
            sent_initial = False
            for multicast_id, unicast_id in ((100, 10), (101, 11), (102, 12)):
                tester.find(multicast_id, multicast=True)
                tester.subscribe_for_ack(label + " C", eg, unicast_id)
                sent_initial |= EVENT_INTERFACE_VERSION in tester.events(_STEP_SECS)
            if sent_initial:
                deviations.append("C: initial event 0x8005 sent, DUT detected a reboot")
        finally:
            tester.close()
        assert not deviations, f"{label}: " + "; ".join(deviations)


class TestEts094:
    @add_test_properties(
        partially_verifies=["comp_req__tc8_conformance__sd_reboot"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_094_reboot_detection_server_side(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDI-004 (ETS_094): events stop after a FindService repeating Session ID 5 (reboot flag set).

        The DUT offers no TriggerEventUint8 method, so the cyclic event 0x8001 is observed instead.
        """
        assert dut.poll() is None, "DUT is not running"
        tester = _Tester(dut_ip, tester_ip)
        try:
            tester.subscribe_for_ack("ETS_094", EVENTGROUP_ALL_EVENTS_AND_FIELDS, session_id=1, ttl=_LONG_TTL)
            tester.events(_STEP_SECS)  # drop the initial events
            assert EVENT_TEST_UINT8 in tester.events(_EVENT_SECS), "ETS_094: no event 0x8001 after the subscription"

            tester.find(5)
            assert EVENT_TEST_UINT8 in tester.events(_EVENT_SECS), "ETS_094: events stopped after the first FindService"

            tester.find(5)
            tester.events(_STEP_SECS)  # drop events sent before the DUT processed the second FindService
            assert not tester.events(_EVENT_SECS), "ETS_094: events still received after the repeated Session ID 5"
        finally:
            tester.close()
