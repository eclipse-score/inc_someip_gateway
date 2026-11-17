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

_TX_COUNT = 20  # samples the DUT publishes on TX (0..TX_COUNT-1)
_RX_COUNT = 5  # distinct RX samples the DUT must observe
_RX_SENDER_COUNT = 20  # samples the peer publishes on RX (0..RX_SENDER_COUNT-1)

# Linux QEMU under TSAN/ASAN is slow; keep the app's deadline inside the
# target timeout but generous enough to absorb SD backoff + sanitizer cost.
_APP_TIMEOUT_MS = 60000
_WAIT_FOR_APP_EXIT_S = 90


@add_test_properties(
    fully_verifies=["feat_req__some_ip_gateway__event_forwarding"],
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
        tx_expected_samples=3,
        rx_notification_count=_RX_SENDER_COUNT,
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
                str(_TX_COUNT),
                "--rx-count",
                str(_RX_COUNT),
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
            signal_rc, signal_output = target.execute(f"touch {_GO_FILE}")
            assert signal_rc == 0, f"could not signal guest event app: {signal_output!r}"

            exit_code = app.wait(timeout_s=_WAIT_FOR_APP_EXIT_S)

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

            # #382 mitigation: the app Unsubscribes and StopOfferService itself
            # before exit; assert the daemons survived that teardown.
            assert exit_code == 0, f"event_exchange_app exit {exit_code}. See stdout/stderr."

    # Peer context-manager has joined threads by this point.
    assert peer.dut_to_host_subscribe_ack, "peer never received SubscribeAck from DUT for the TX direction"
    expected_tx_counters = list(range(peer_cfg.tx_expected_samples))
    assert peer.dut_to_host_valid_samples == expected_tx_counters, (
        f"peer received TX counters {peer.dut_to_host_valid_samples} (expected {expected_tx_counters})"
    )
    assert peer.host_to_dut_subscribed, "DUT never subscribed to the host's RX service on the wire"
    assert not peer.errors, f"peer reported errors: {peer.errors}"
