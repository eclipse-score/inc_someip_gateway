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
Tests for verifying the SOMEIP service discovery and event communication between two QEMU instances using tcpdump captures.
The tests cover both positive scenarios (services properly discovered and subscribed) and negative scenarios (no traffic when no services running, etc.).
The pcap analysis checks for expected SOME/IP-SD offers, finds, and subscriptions, as well as SOME/IP event notifications.
"""

from __future__ import annotations

import logging
import os
import signal
import subprocess
import time
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator


from tests.itf_updates.qemu_utils import (
    qemu_ifs_image,
    qemu_run_script,
    start_qemu,
)
from tests.integration.someip_network_analyzer import (
    analyze as analyze_pcap_file,
    analyze_events,
)

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

TCPDUMP_INTERFACE = "virbr0"
QEMU1_IP = "192.168.87.2"
QEMU2_IP = "192.168.87.3"
SERVICE_SETTLE_TIME = 5  # seconds to let SD exchange complete

SSH_OPTS = [
    "-o",
    "StrictHostKeyChecking=no",
    "-o",
    "UserKnownHostsFile=/dev/null",
    "-o",
    "LogLevel=ERROR",
]

# Service commands
GATEWAYD_CMD = (
    "/usr/bin/gatewayd -config_file /etc/gatewayd/gatewayd_config.bin "
    "--service_instance_manifest /etc/gatewayd/mw_com_config.json"
)
SOMEIPD_REMOTE_LOG = "/tmp/someipd.log"
SOMEIPD_CMD = (
    "export VSOMEIP_CONFIGURATION=/etc/someipd/vsomeip.json && "
    "/usr/bin/someipd -someipd_config /etc/someipd/someipd_config.json "
    "--service_instance_manifest /etc/someipd/mw_com_config.json "
    f"> {SOMEIPD_REMOTE_LOG} 2>&1"
)
SAMPLE_CLIENT_REMOTE_LOG = "/tmp/sample_client.log"
SAMPLE_CLIENT_CMD = (
    "export VSOMEIP_CONFIGURATION=/etc/sample_client/vsomeip.json && "
    "/usr/bin/sample_client "
    f"> {SAMPLE_CLIENT_REMOTE_LOG} 2>&1"
)

IPC_BENCHMARKS_CMD = (
    "/usr/bin/tests/ipc_benchmarks "
    "--service_instance_manifest /etc/benchmarks/benchmark_mw_com_config.json "
    "--benchmark_min_time=0.001s --benchmark_repetitions=1"  # minimal amount of time to see  event data qemu1 to qemu2, and qemu2 to qemu1
)


# ---------------------------------------------------------------------------
# Data Classes
# ---------------------------------------------------------------------------


@dataclass
class SDExpectation:
    """Expected SOME/IP-SD state for a host."""

    name: str
    offers: set[str]
    finds: set[str]
    subscribed: set[str]


@dataclass
class SDResult:
    """Parsed SOME/IP-SD result for a host."""

    offers: set[str]
    finds: set[str]
    subscribed: set[str]


@dataclass
class EventExpectation:
    """Expected SOME/IP event notification for a host."""

    name: str
    service_id: int
    event_id: int
    payload_size: int


@dataclass
class EventResult:
    """Observed SOME/IP event notification for a host."""

    service_id: int
    event_id: int
    payload_size: int


# Expected SOME/IP-SD state per host
EXPECTED_SD = {
    QEMU1_IP: SDExpectation(
        name="someipd",
        offers={"Service 0x1234.0x5678"},
        finds={"Service 0x4321.0x5678"},
        subscribed={"Service 0x4321.0x5678, eventgroup 0x4465"},
    ),
    QEMU2_IP: SDExpectation(
        name="sample_client",
        offers={"Service 0x4321.0x5678"},
        finds={"Service 0x1234.0x5678"},
        subscribed={"Service 0x1234.0x5678, eventgroup 0x4465"},
    ),
}

# Expected SOME/IP event notification per host
EXPECTED_EVENTS = {
    QEMU1_IP: EventExpectation(
        name="someipd", service_id=0x1234, event_id=0x8778, payload_size=32
    ),
    QEMU2_IP: EventExpectation(
        name="sample_client", service_id=0x4321, event_id=0x8778, payload_size=32
    ),
}


# ---------------------------------------------------------------------------
# Helper Functions
# ---------------------------------------------------------------------------


def get_outputs_dir() -> Path:
    """Get the Bazel undeclared outputs directory, fallback to /tmp."""
    # Bazel automatically archives everything written here into outputs.zip
    out_dir = os.environ.get("TEST_UNDECLARED_OUTPUTS_DIR")

    if out_dir:
        path = Path(out_dir)
        path.mkdir(parents=True, exist_ok=True)
        return path

    return Path("/tmp")


def ssh_run_bg(host: str, cmd: str) -> None:
    """Fire-and-forget a command on host via SSH."""
    subprocess.Popen(
        ["ssh", *SSH_OPTS, f"root@{host}", cmd],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )


def ssh_run(host: str, cmd: str) -> subprocess.CompletedProcess:
    """Run a command on host via SSH and return the result with captured output."""
    return subprocess.run(
        ["ssh", *SSH_OPTS, f"root@{host}", cmd],
        capture_output=True,
    )


def fetch_service_log(
    host: str, service: str, remote_log: str, tc_name: str, outputs_dir: Path
) -> None:
    """Fetch a service log from QNX via SSH and save as <service>_<tc_name>.log."""
    local_log = outputs_dir / f"{tc_name}_{service}.log"
    result = ssh_run(host, f"/proc/boot/cat {remote_log}")
    if result.returncode != 0:
        logger.warning(
            f"Failed to fetch {service} log from {host}: {result.stderr.decode()}"
        )
    else:
        local_log.write_bytes(result.stdout)
        logger.info(f"{service} log saved to {local_log}")


def kill_stale_qemu_instances() -> None:
    """Kill any stale QEMU instances from previous runs."""
    for instance_id in (1, 2):
        pid_file = Path(f"/tmp/qemu-someip-gateway-{instance_id}.pid")
        if pid_file.exists():
            try:
                old_pid = int(pid_file.read_text().strip())
                os.kill(old_pid, signal.SIGTERM)
                logger.info(f"Killed stale QEMU instance {instance_id} (PID {old_pid})")
                time.sleep(1)
                pid_file.unlink(missing_ok=True)
            except (ProcessLookupError, ValueError):
                pass
    subprocess.run(["pkill", "-f", "qemu-system"], capture_output=True)
    time.sleep(2)


def _format_service(
    svc: int, inst: int, eg: int | None = None, *, with_eg: bool = False
) -> str:
    """Format a service entry as the expected string.
    Ex: print(_format_service(255, 1)) ->Output: "Service 0x00ff.0x0001"
    Ex: print(_format_service(255, 1, 10)) ->Output: "Service 0x00ff.0x0001"
    Ex: print(_format_service(255, 1, 10, with_eg=True)) ->Output: "Service 0x00ff.0x0001, eventgroup 0x000a"
    """
    if with_eg and eg is not None:
        return f"Service 0x{svc:04x}.0x{inst:04x}, eventgroup 0x{eg:04x}"
    return f"Service 0x{svc:04x}.0x{inst:04x}"


def analyze_pcap(pcap_file: Path) -> dict[str, SDResult]:
    """Analyze pcap file and return parsed SD results per IP."""
    raw = analyze_pcap_file(str(pcap_file))
    result: dict[str, SDResult] = {}

    for ip, data in raw.items():
        offers = {_format_service(s, i) for s, i, _eg in data["offers"]}
        finds = {_format_service(s, i) for s, i in data["finds"]}
        subscribed = {
            _format_service(s, i, eg, with_eg=True) for s, i, eg in data["subscribes"]
        }
        result[ip] = SDResult(offers=offers, finds=finds, subscribed=subscribed)

    return result


def analyze_pcap_events(pcap_file: Path) -> dict[str, list[dict]]:
    """Analyze pcap file for SOME/IP event notification packets."""
    return analyze_events(str(pcap_file))


@contextmanager
def tcpdump_capture(pcap_file: Path, log_file: Path) -> Iterator[subprocess.Popen]:
    """Context manager for tcpdump packet capture."""
    with open(log_file, "w") as log_fh:
        proc = subprocess.Popen(
            [
                "tcpdump",
                "-i",
                TCPDUMP_INTERFACE,
                "-w",
                str(pcap_file),
                f"host {QEMU1_IP} or host {QEMU2_IP}",
            ],
            stdout=subprocess.DEVNULL,
            stderr=log_fh,
        )
        time.sleep(2)  # Give tcpdump time to initialize
        assert proc.poll() is None, f"tcpdump exited early: {log_file.read_text()}"

        try:
            yield proc
        finally:
            proc.send_signal(signal.SIGINT)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()


def start_qemu1_services(host: str) -> None:
    """Start gatewayd, someipd, and echo_server on QEMU 1."""
    ssh_run_bg(host, GATEWAYD_CMD)
    time.sleep(3)
    ssh_run_bg(host, SOMEIPD_CMD)
    time.sleep(2)


def start_ipc_benchmarks(host: str) -> None:
    """Start ipc_benchmarks on QEMU 1 to produce echo_request events."""
    ssh_run_bg(host, IPC_BENCHMARKS_CMD)


def start_qemu2_services(host: str) -> None:
    """Start sample_client on QEMU 2."""
    ssh_run_bg(host, SAMPLE_CLIENT_CMD)


def verify_sd_match(ip: str, expected: SDExpectation, actual: SDResult) -> None:
    """Verify SD offers/finds/subscriptions match expected values."""
    logger.info("-" * 80)
    logger.info(f"CHECKING: {expected.name} ({ip})")
    logger.info("-" * 80)

    for field in ("offers", "finds", "subscribed"):
        exp_val = getattr(expected, field)
        act_val = getattr(actual, field)
        label = "SUBSCRIBED SUCCESSFULLY TO" if field == "subscribed" else field.upper()
        logger.info(f"  {label}:")
        logger.info(f"    expected: {sorted(exp_val)}")
        logger.info(f"    actual:   {sorted(act_val)}")
        assert exp_val == act_val, f"[{expected.name}] {label} mismatch"
        logger.info("    -> OK")


def verify_partial_sd(ip: str, parsed: dict[str, SDResult], output: str) -> None:
    """Verify partial SD behavior (offers/finds present, no subscriptions)."""
    assert ip in parsed, f"No SOME/IP-SD data for {ip}.\nOutput:\n{output}"
    actual = parsed[ip]

    logger.info(f"  OFFERS: {sorted(actual.offers)}")
    assert actual.offers, f"Should have offers, got: {actual.offers}"
    logger.info("    -> OK (has offers)")

    logger.info(f"  FINDS: {sorted(actual.finds)}")
    assert actual.finds, f"Should have finds, got: {actual.finds}"
    logger.info("    -> OK (has finds)")

    logger.info(f"  SUBSCRIBED: {sorted(actual.subscribed)}")
    assert not actual.subscribed, (
        f"Should have no subscriptions, got: {actual.subscribed}"
    )
    logger.info("    -> OK (no subscriptions as expected)")


def verify_event_match(
    ip: str, expected: EventExpectation, events: dict[str, list[dict]]
) -> None:
    """Verify SOME/IP event notification matches expected event_id and payload_size."""
    logger.info("-" * 80)
    logger.info(f"CHECKING: {expected.name} ({ip})")
    logger.info("-" * 80)

    matching = [e for e in events.get(ip, []) if e["service_id"] == expected.service_id]
    assert matching, (
        f"[{expected.name}] No events for service 0x{expected.service_id:04x}"
    )

    actual = EventResult(
        service_id=matching[0]["service_id"],
        event_id=matching[0]["method_id"],
        payload_size=matching[0]["payload_size"],
    )

    logger.info(
        f"  event_id:     expected=0x{expected.event_id:04x}  actual=0x{actual.event_id:04x}"
    )
    assert actual.event_id == expected.event_id, (
        f"[{expected.name}] event_id mismatch: expected 0x{expected.event_id:04x}, got 0x{actual.event_id:04x}"
    )
    logger.info("    -> OK")

    logger.info(
        f"  payload_size: expected={expected.payload_size}B  actual={actual.payload_size}B"
    )
    assert actual.payload_size == expected.payload_size, (
        f"[{expected.name}] payload_size mismatch: expected {expected.payload_size}, got {actual.payload_size}"
    )
    logger.info("    -> OK")


class TestSomeIPSD:
    """Tests for SOME/IP Service Discovery between two QEMU instances.

    OFFERS: Services offered by the host's someipd configuration.
    FINDS: Services the host is looking for.
    SUBSCRIBED: Successful subscriptions (requires matching offer from remote).
    """

    def test_someip_sd_offers_finds_subscriptions(
        self, request, qemu_ifs_image, qemu_run_script
    ):
        """Verify SOME/IP-SD offers, finds, and subscriptions between QEMUs."""
        kill_stale_qemu_instances()
        tmp = get_outputs_dir()
        pcap = tmp / f"{request.node.name}_tcpdump.pcap"
        log = tmp / f"{request.node.name}_tcpdump.log"

        instance1 = start_qemu(qemu_ifs_image, qemu_run_script, instance_id=1)
        instance2 = start_qemu(qemu_ifs_image, qemu_run_script, instance_id=2)
        try:
            with tcpdump_capture(pcap, log):
                start_qemu1_services(instance1.ssh_host)
                start_qemu2_services(instance2.ssh_host)
                time.sleep(SERVICE_SETTLE_TIME)
        finally:
            fetch_service_log(
                instance1.ssh_host,
                "someipd",
                SOMEIPD_REMOTE_LOG,
                request.node.name,
                tmp,
            )
            fetch_service_log(
                instance2.ssh_host,
                "sample_client",
                SAMPLE_CLIENT_REMOTE_LOG,
                request.node.name,
                tmp,
            )
            instance1.stop()
            instance2.stop()

        assert pcap.exists(), f"pcap not created at {pcap}"
        parsed = analyze_pcap(pcap)

        for ip, expected in EXPECTED_SD.items():
            assert ip in parsed, f"No SD data for {expected.name} ({ip})"
            verify_sd_match(ip, expected, parsed[ip])

    def test_negative_no_qemu_only_tcpdump(self, request):
        """Negative: No QEMUs running - no traffic expected."""
        kill_stale_qemu_instances()

        tmp = get_outputs_dir()
        pcap = tmp / f"{request.node.name}_tcpdump.pcap"

        with tcpdump_capture(pcap, tmp / f"{request.node.name}_tcpdump.log"):
            time.sleep(5)

        parsed = analyze_pcap(pcap)
        assert not parsed, f"Expected no SD data, found: {list(parsed.keys())}"
        logger.info("PASS: No SOME/IP-SD traffic when no QEMU instances running")

    def test_negative_both_qemus_no_services(
        self, request, qemu_ifs_image, qemu_run_script
    ):
        """Negative: Both QEMUs running but no services - no traffic expected."""
        kill_stale_qemu_instances()
        tmp = get_outputs_dir()
        pcap = tmp / f"{request.node.name}_tcpdump.pcap"

        instance1 = start_qemu(qemu_ifs_image, qemu_run_script, instance_id=1)
        instance2 = start_qemu(qemu_ifs_image, qemu_run_script, instance_id=2)
        try:
            logger.info(f"QEMU 1: {instance1.ssh_host}, QEMU 2: {instance2.ssh_host}")
            logger.info("Waiting 5 seconds without starting services...")

            with tcpdump_capture(pcap, tmp / f"{request.node.name}_tcpdump.log"):
                time.sleep(5)
        finally:
            instance1.stop()
            instance2.stop()

        parsed = analyze_pcap(pcap)
        assert not parsed, f"Expected no SD data, found: {list(parsed.keys())}"
        logger.info("PASS: No SOME/IP-SD traffic when QEMUs running without services")

    def test_negative_only_qemu1_with_services(
        self, request, qemu_ifs_image, qemu_run_script
    ):
        """Negative: Only QEMU 1 with services - offers/finds but no subscriptions."""
        kill_stale_qemu_instances()
        tmp = get_outputs_dir()
        pcap = tmp / f"{request.node.name}_tcpdump.pcap"
        log = tmp / f"{request.node.name}_tcpdump.log"

        instance = start_qemu(qemu_ifs_image, qemu_run_script, instance_id=1)
        try:
            with tcpdump_capture(pcap, log):
                logger.info(f"Starting services on QEMU 1 ({instance.ssh_host})...")
                start_qemu1_services(instance.ssh_host)
                logger.info(f"Waiting {SERVICE_SETTLE_TIME}s for SD exchanges...")
                time.sleep(SERVICE_SETTLE_TIME)
        finally:
            fetch_service_log(
                instance.ssh_host, "someipd", SOMEIPD_REMOTE_LOG, request.node.name, tmp
            )
            instance.stop()

        parsed = analyze_pcap(pcap)

        logger.info("-" * 80)
        logger.info("CHECKING: Only QEMU 1 running with services")
        logger.info("-" * 80)

        verify_partial_sd(QEMU1_IP, parsed, str(pcap))
        assert QEMU2_IP not in parsed, "QEMU 2 should not be present"
        logger.info("  QEMU 2: Not present (as expected)")
        logger.info("PASS: Only QEMU 1 - offers/finds present, no subscriptions")

    def test_negative_only_qemu2_with_services(
        self, request, qemu_ifs_image, qemu_run_script
    ):
        """Negative: Only QEMU 2 with services - offers/finds but no subscriptions."""
        kill_stale_qemu_instances()
        tmp = get_outputs_dir()
        pcap = tmp / f"{request.node.name}_tcpdump.pcap"
        log = tmp / f"{request.node.name}_tcpdump.log"

        instance = start_qemu(qemu_ifs_image, qemu_run_script, instance_id=2)
        try:
            with tcpdump_capture(pcap, log):
                logger.info(
                    f"Starting sample_client on QEMU 2 ({instance.ssh_host})..."
                )
                start_qemu2_services(instance.ssh_host)
                logger.info(f"Waiting {SERVICE_SETTLE_TIME}s for SD exchanges...")
                time.sleep(SERVICE_SETTLE_TIME)
        finally:
            fetch_service_log(
                instance.ssh_host,
                "sample_client",
                SAMPLE_CLIENT_REMOTE_LOG,
                request.node.name,
                tmp,
            )
            instance.stop()

        parsed = analyze_pcap(pcap)

        logger.info("-" * 80)
        logger.info("CHECKING: Only QEMU 2 running with services")
        logger.info("-" * 80)

        verify_partial_sd(QEMU2_IP, parsed, str(pcap))
        assert QEMU1_IP not in parsed, "QEMU 1 should not be present"
        logger.info("  QEMU 1: Not present (as expected)")
        logger.info("PASS: Only QEMU 2 - offers/finds present, no subscriptions")

    def test_someip_event_data_transfer(self, request, qemu_ifs_image, qemu_run_script):
        """Verify SOME/IP event data flows between QEMU 1 and QEMU 2."""
        kill_stale_qemu_instances()
        tmp = get_outputs_dir()
        pcap = tmp / f"{request.node.name}_tcpdump.pcap"

        instance1 = start_qemu(qemu_ifs_image, qemu_run_script, instance_id=1)
        instance2 = start_qemu(qemu_ifs_image, qemu_run_script, instance_id=2)
        try:
            with tcpdump_capture(pcap, tmp / f"{request.node.name}_tcpdump.log"):
                start_qemu1_services(instance1.ssh_host)
                start_qemu2_services(instance2.ssh_host)
                time.sleep(SERVICE_SETTLE_TIME)
                start_ipc_benchmarks(instance1.ssh_host)
                time.sleep(10)
        finally:
            fetch_service_log(
                instance1.ssh_host,
                "someipd",
                SOMEIPD_REMOTE_LOG,
                request.node.name,
                tmp,
            )
            fetch_service_log(
                instance2.ssh_host,
                "sample_client",
                SAMPLE_CLIENT_REMOTE_LOG,
                request.node.name,
                tmp,
            )
            instance1.stop()
            instance2.stop()

        events = analyze_pcap_events(pcap)

        for ip, expected in EXPECTED_EVENTS.items():
            verify_event_match(ip, expected, events)
