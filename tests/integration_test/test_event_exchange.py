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

"""Feature integration test: bidirectional mw::com event exchange.

Verifies feat_req__some_ip_gateway__event_forwarding:

* TX direction (guest -> wire): the guest C++ ``event_exchange_app`` offers a
  local mw::com skeleton for ``/event_exchange/tx``. gatewayd subscribes via
  mw::com, forwards samples through someipd onto the SOME/IP network. The
  host peer subscribes on the wire and asserts byte-for-byte payload match.
* RX direction (wire -> guest): the host peer offers an RX service on the
  wire and sends notifications. someipd receives them, gatewayd publishes
  them to its local mw::com skeleton, and the guest app's proxy consumes
  them and exits 0 once at least ``--rx-count`` samples have been validated
  byte-for-byte.

Both directions are exercised concurrently in one process on each side.
"""

import logging
import os

from attribute_plugin import add_test_properties
from util import ShellProcess

from event_exchange.someip_peer import PeerConfig, SomeIpHostPeer


# The DUT's unicast IP comes from config/vsomeip.json. The host IP is picked
# from the integration-test environment so both ends share the tap network.
_HOST_IP = os.environ.get("EVENT_EXCHANGE_HOST_IP", "169.254.21.88")
_DUT_IP = os.environ.get("EVENT_EXCHANGE_DUT_IP", "169.254.158.190")

_READY_FILE = "/tmp/event_exchange_ready"
_GO_FILE = "/tmp/event_exchange_go"

_SAMPLE_COUNT = 20

# Linux QEMU under TSAN/ASAN is slow; keep the app's deadline inside the
# target timeout but generous enough to absorb SD backoff + sanitizer cost.
_APP_TIMEOUT_MS = 60000
_WAIT_FOR_APP_EXIT_S = 90


@add_test_properties(
    partially_verifies=[
        "feat_req__some_ip_gateway__event_forwarding",
        "comp_req__gatewayd__forwards_local_events",
        "comp_req__gatewayd__forwards_remote_events",
    ],
    test_type="requirements-based",
    derivation_technique="requirements-analysis",
)
def test_event_exchange(gatewayd_with_someipd):
    """End-to-end bidirectional event exchange across gatewayd + someipd."""
    target = gatewayd_with_someipd

    cleanup_rc, cleanup_output = target.execute(f"rm -f {_READY_FILE} {_GO_FILE}")
    assert cleanup_rc == 0, f"could not clear guest sync files: {cleanup_output!r}"

    peer_cfg = PeerConfig(
        host_ip=_HOST_IP,
        dut_ip=_DUT_IP,
        tx_expected_samples=_SAMPLE_COUNT,
        rx_notification_count=_SAMPLE_COUNT,
    )

    with SomeIpHostPeer(peer_cfg) as peer:
        # Start the guest app under test. It offers the TX skeleton, subscribes
        # the RX proxy, writes the ready-file, then blocks for the go-file.
        with ShellProcess(
            target,
            "/opt/event_exchange_app",
            args=[
                "--service_instance_manifest",
                "/opt/app_mw_com_config.json",
                "--tx-count",
                str(_SAMPLE_COUNT),
                "--rx-count",
                str(_SAMPLE_COUNT),
                "--timeout-ms",
                str(_APP_TIMEOUT_MS),
                "--ready-file",
                _READY_FILE,
                "--go-file",
                _GO_FILE,
            ],
        ) as app:
            assert peer.wait_for_subscribe_ack(timeout_s=35.0), (
                "peer never received SubscribeAck from DUT for the TX direction"
            )
            assert peer.wait_for_rx_subscription(timeout_s=35.0), (
                "DUT never subscribed to the host's RX service on the wire"
            )
            ready_rc, ready_output = target.execute(
                f'i=0; while [ ! -e {_READY_FILE} ] && [ "$i" -lt 200 ]; do '
                "sleep 0.1; i=$((i + 1)); "
                f"done; test -e {_READY_FILE}"
            )
            assert ready_rc == 0, f"guest RX proxy did not become ready: {ready_output!r}"

            signal_rc, signal_output = target.execute(f"touch {_GO_FILE}")
            assert signal_rc == 0, f"could not signal guest event app: {signal_output!r}"
            peer.release_rx_notifications()

            exit_code = app.wait(timeout_s=_WAIT_FOR_APP_EXIT_S)
            assert exit_code == 0, f"event_exchange_app exit {exit_code}. See stdout/stderr."

        assert peer.wait_for_dut_to_host_complete(timeout_s=35.0), (
            "peer did not finish collecting DUT notifications"
        )

    logging.info("app stdout:\n%s", app.get_output())
    logging.info(
        "peer dut_to_host: subscribed=%s ack=%s valid_samples=%s",
        peer.dut_to_host_subscribed,
        peer.dut_to_host_subscribe_ack,
        peer.dut_to_host_valid_samples,
    )
    logging.info(
        "peer host_to_dut: subscribed=%s sent=%d errors=%s",
        peer.host_to_dut_subscribed,
        peer.host_to_dut_sent,
        peer.errors,
    )

    assert peer.dut_to_host_subscribe_ack, "peer never received SubscribeAck from DUT for the TX direction"
    expected_tx_counters = list(range(peer_cfg.tx_expected_samples))
    assert peer.dut_to_host_valid_samples == expected_tx_counters, (
        f"peer received TX counters {peer.dut_to_host_valid_samples} (expected {expected_tx_counters})"
    )
    assert peer.host_to_dut_subscribed, "DUT never subscribed to the host's RX service on the wire"
    assert not peer.errors, f"peer reported errors: {peer.errors}"
