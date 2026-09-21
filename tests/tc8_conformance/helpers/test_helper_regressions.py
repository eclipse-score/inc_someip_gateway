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
"""Host-only regression tests for TC8 helper behavior fixed in PR-4 review round 2.

Covers: SD session-id wraparound (item 7), the UDP-before-TCP endpoint option
filter (item 9), and the multi-handle ``_TargetProcess.poll()`` fix (item 5).
No DUT/QEMU is involved; everything here runs against fakes/pure functions.
"""

import ipaddress

import helpers.sd_sender as sd_sender
from helpers.dut_lifecycle import _TargetProcess
from helpers.someip_assertions import assert_offer_has_ipv4_endpoint_option
from helpers.someip_types import (
    IPv4EndpointOption,
    L4Protocols,
    SOMEIPSDEntry,
    SOMEIPSDEntryType,
)


class TestSdSessionIdWraparound:
    """Regression for item 7: session-id must wrap from 0xFFFF to 1, never emit 0x0000."""

    def test_no_consecutive_duplicate_across_wraparound(self) -> None:
        sd_sender._last_session_id = 0xFFFE  # noqa: SLF001
        sequence = [sd_sender._next_session_id() for _ in range(5)]  # noqa: SLF001
        assert sequence == [0xFFFF, 1, 2, 3, 4]
        # The original bug re-emitted 1 twice in a row across the wraparound boundary.
        for a, b in zip(sequence, sequence[1:]):
            assert a != b

    def test_reserved_zero_never_emitted(self) -> None:
        sd_sender._last_session_id = 0  # noqa: SLF001
        sequence = [sd_sender._next_session_id() for _ in range(0x10000 + 2)]  # noqa: SLF001
        assert 0 not in sequence


class TestAssertOfferHasIpv4EndpointOptionMixedProtocol:
    """Regression for item 9: must pick the UDP option, not just the first one."""

    def test_tcp_before_udp_still_selects_udp(self) -> None:
        # TCP option listed first in options_1, UDP option second: a naive
        # "take options_1[0]" implementation would incorrectly pick the TCP one.
        tcp_option = IPv4EndpointOption(
            address=ipaddress.IPv4Address("192.168.1.50"),
            l4proto=L4Protocols.TCP,
            port=30511,
        )
        udp_option = IPv4EndpointOption(
            address=ipaddress.IPv4Address("192.168.1.50"),
            l4proto=L4Protocols.UDP,
            port=30510,
        )
        entry = SOMEIPSDEntry(
            sd_type=SOMEIPSDEntryType.OfferService,
            service_id=0x1234,
            instance_id=0x0001,
            major_version=1,
            ttl=3,
            minver_or_counter=0,
            options_1=(tcp_option, udp_option),
        )
        # Must not raise, and must have matched the UDP endpoint, not the TCP one.
        assert_offer_has_ipv4_endpoint_option(entry, "192.168.1.50", 30510)


class TestTargetProcessPollAllHandles:
    """Regression for item 5: poll() must check all three tracked process handles."""

    class _FakeAsyncProcess:
        def __init__(self, running: bool) -> None:
            self._running = running

        def is_running(self) -> bool:
            return self._running

    def test_stub_handle_stopped_is_detected(self) -> None:
        # Only the THIRD handle (stub_proc) is stopped; primary and secondary
        # are still running. A poll() that only checked the first handle would
        # miss this and incorrectly report the process group as still alive.
        proc = _TargetProcess(
            proc=self._FakeAsyncProcess(running=True),
            secondary_proc=self._FakeAsyncProcess(running=True),
            stub_proc=self._FakeAsyncProcess(running=False),
        )
        assert proc.poll() == 0

    def test_secondary_handle_stopped_is_detected(self) -> None:
        proc = _TargetProcess(
            proc=self._FakeAsyncProcess(running=True),
            secondary_proc=self._FakeAsyncProcess(running=False),
            stub_proc=self._FakeAsyncProcess(running=True),
        )
        assert proc.poll() == 0

    def test_all_handles_running_reports_none(self) -> None:
        proc = _TargetProcess(
            proc=self._FakeAsyncProcess(running=True),
            secondary_proc=self._FakeAsyncProcess(running=True),
            stub_proc=self._FakeAsyncProcess(running=True),
        )
        assert proc.poll() is None
