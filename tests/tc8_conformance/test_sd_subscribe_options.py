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
"""TC8-SDO-001 to SDO-008: endpoint option content in SubscribeEventgroup messages.

See ``docs/architecture/tc8_conformance_testing.rst`` for the test architecture.
"""

import ipaddress
import socket
import subprocess

import pytest
from attribute_plugin import add_test_properties

from helpers.constants import EVENTGROUP_ALL_EVENTS_AND_FIELDS, INSTANCE_ID, MAJOR_VERSION, SD_PORT, SERVICE_ID
from helpers.sd_malformed import (
    _endpoint_option_bytes,
    _subscribe_entry_bytes,
    build_raw_sd_packet,
    send_sd_subscribe_wrong_l4proto,
)
from helpers.sd_helpers import assert_nack, capture_subscribe_answers
from helpers.sd_sender import open_sender_socket, send_sd_entries, send_subscribe_eventgroup
from helpers.someip_types import IPv4EndpointOption, L4Protocols, SOMEIPSDEntry, SOMEIPSDEntryType

_UNEXISTING_IP: str = "169.254.200.200"
_UNALLOWED_IP: str = "111.111.111.111"
_NO_IPV4_ADDRESS: str = "32.0.0.0"
_CONFIGURATION_OPTION: bytes = bytes([0x00, 0x02, 0x01, 0x00, 0x00])


class TestSDSubscribeEndpointOptions:
    """SubscribeEventgroup messages with invalid or unusual option content."""

    def _nack_for_endpoint(self, ets: str, dut_ip: str, tester_ip: str, option_ip: str, option_port: int = 0) -> None:
        sock = open_sender_socket(tester_ip)
        try:
            port = option_port or sock.getsockname()[1]

            def _send() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (dut_ip, SD_PORT),
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_ALL_EVENTS_AND_FIELDS,
                    MAJOR_VERSION,
                    subscriber_ip=option_ip,
                    subscriber_port=port,
                )

            assert_nack(ets, capture_subscribe_answers(sock, _send))
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_109_no_port_in_endpoint_option(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDO-001 (ETS_109): Subscribe with port 0 in the Endpoint Option is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"
        sock = open_sender_socket(tester_ip)
        try:

            def _send() -> None:
                send_subscribe_eventgroup(
                    sock,
                    (dut_ip, SD_PORT),
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_ALL_EVENTS_AND_FIELDS,
                    MAJOR_VERSION,
                    subscriber_ip=tester_ip,
                    subscriber_port=0,
                )

            assert_nack("ETS_109", capture_subscribe_answers(sock, _send))
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_110_no_ipv4_address_in_endpoint_option(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDO-002 (ETS_110): Subscribe with IP address 32.0.0.0 in the Endpoint Option is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"
        self._nack_for_endpoint("ETS_110", dut_ip, tester_ip, _NO_IPV4_ADDRESS)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_119_wrong_l4proto(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDO-003 (ETS_119): Subscribe whose Endpoint Option has l4proto neither UDP nor TCP is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"
        sock = open_sender_socket(tester_ip)
        try:
            port = sock.getsockname()[1]

            def _send() -> None:
                send_sd_subscribe_wrong_l4proto(
                    sock,
                    (dut_ip, SD_PORT),
                    SERVICE_ID,
                    INSTANCE_ID,
                    EVENTGROUP_ALL_EVENTS_AND_FIELDS,
                    MAJOR_VERSION,
                    tester_ip,
                    port,
                )

            assert_nack("ETS_119", capture_subscribe_answers(sock, _send))
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
            "TC8-SDO-004 / OA ETS_154: a SubscribeEventgroup with an unexisting IPv4 address in the "
            "Endpoint Option is expected to be answered with a SubscribeEventgroupNAck. "
            "Observed: no NAck is received"
        ),
    )
    def test_ets_154_unexisting_ipv4_address(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDO-004 (ETS_154): Subscribe with an unexisting IPv4 address in the Endpoint Option is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"
        self._nack_for_endpoint("ETS_154", dut_ip, tester_ip, _UNEXISTING_IP)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_162_unallowed_option_ip(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDO-005 (ETS_162): Subscribe with the DUT IP address in the Endpoint Option is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"
        self._nack_for_endpoint("ETS_162", dut_ip, tester_ip, dut_ip)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_164_unallowed_option_ip_2(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDO-006 (ETS_164): Subscribe with IP address 111.111.111.111 in the Endpoint Option is answered with a NAck."""
        assert dut.poll() is None, "DUT is not running"
        self._nack_for_endpoint("ETS_164", dut_ip, tester_ip, _UNALLOWED_IP)

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    @pytest.mark.xfail(
        strict=True,
        reason=(
            "TC8-SDO-007 / OA ETS_172: unicast SubscribeEventgroup messages with the option settings "
            "(index1stOptions 0, index2ndOptions 1, numOfOptions1 1, numOfOptions2 1) and "
            "(0, 0, 2, 0) are each expected to be answered with a unicast "
            "SubscribeEventgroupAck. Observed: no Ack is received for either"
        ),
    )
    def test_ets_172_unicast_subscribe_option_indexes(
        self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str
    ) -> None:
        """TC8-SDO-007 (ETS_172, ETS_173): unicast Subscribe with two option run layouts is answered with an Ack each."""
        assert dut.poll() is None, "DUT is not running"
        sock = open_sender_socket(tester_ip)
        try:
            port = sock.getsockname()[1]
            options = tuple(
                IPv4EndpointOption(address=ipaddress.IPv4Address(tester_ip), l4proto=proto, port=port)
                for proto in (L4Protocols.UDP, L4Protocols.TCP)
            )

            def _entry(options_1: tuple, options_2: tuple) -> SOMEIPSDEntry:
                return SOMEIPSDEntry(
                    sd_type=SOMEIPSDEntryType.Subscribe,
                    service_id=SERVICE_ID,
                    instance_id=INSTANCE_ID,
                    major_version=MAJOR_VERSION,
                    ttl=3,
                    minver_or_counter=EVENTGROUP_ALL_EVENTS_AND_FIELDS,
                    options_1=options_1,
                    options_2=options_2,
                )

            layouts = (
                ("index1=0, index2=1, num1=1, num2=1", _entry(options[:1], options[1:])),
                ("index1=0, index2=0, num1=2, num2=0", _entry(options, ())),
            )
            missing = []
            for label, entry in layouts:
                answers = capture_subscribe_answers(
                    sock, lambda e=entry: send_sd_entries(sock, (dut_ip, SD_PORT), (e,)), max_results=1
                )
                if not any(a.ttl > 0 for a in answers):
                    missing.append(label)
            assert not missing, f"ETS_172: No SubscribeEventgroupAck for option layout(s): {missing}"
        finally:
            sock.close()

    @add_test_properties(
        fully_verifies=["comp_req__tc8_conformance__sd_sub_lifecycle"],
        test_type="requirements-based",
        derivation_technique="requirements-analysis",
    )
    def test_ets_175_unreferenced_option(self, dut: subprocess.Popen[bytes], dut_ip: str, tester_ip: str) -> None:
        """TC8-SDO-008 (ETS_175): Subscribe carrying an unreferenced Configuration Option is answered with an Ack."""
        assert dut.poll() is None, "DUT is not running"
        sock = open_sender_socket(tester_ip)
        try:
            port = sock.getsockname()[1]
            entry = _subscribe_entry_bytes(
                SERVICE_ID, INSTANCE_ID, EVENTGROUP_ALL_EVENTS_AND_FIELDS, MAJOR_VERSION, num_opts=1
            )
            options = _endpoint_option_bytes(tester_ip, port) + _CONFIGURATION_OPTION

            def _send() -> None:
                sock.sendto(build_raw_sd_packet(entries_bytes=entry, options_bytes=options), (dut_ip, SD_PORT))

            answers = capture_subscribe_answers(sock, _send, max_results=1)
            assert any(a.ttl > 0 for a in answers), "ETS_175: No SubscribeEventgroupAck received"
        finally:
            sock.close()
