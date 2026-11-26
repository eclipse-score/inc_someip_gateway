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

"""Host-side SOME/IP + SD peer for the event_exchange integration test.

Runs on the test host alongside the guest (DUT) and drives both directions
of the feature integration test over real SOME/IP+SD on a tap interface.
Uses small local SOME/IP and SD codecs so this integration test remains
independent of the TC8 conformance helpers. Payload layout must match the
PingBody struct declared in service_interface.h.
"""

import queue
import logging
import socket
import struct
import threading
import time
from dataclasses import dataclass
from typing import List, Optional

_log = logging.getLogger(__name__)

SD_MULTICAST_ADDR = "224.244.224.245"
SD_PORT = 30490
SD_SERVICE_ID = 0xFFFF
SD_METHOD_ID = 0x8100
SD_MESSAGE_TYPE = 0x02
SOMEIP_NOTIFICATION = 0x02
SD_OFFER_SERVICE = 0x01
SD_SUBSCRIBE = 0x06
SD_SUBSCRIBE_ACK = 0x07
UDP_PROTOCOL = 17


# -----------------------------------------------------------------------------
# PingBody: layout must match tests/integration_test/event_exchange/service_interface.h
# -----------------------------------------------------------------------------

PAYLOAD_MARKER: int = 0x45584348_1700B1D6
PING_BODY_SIZE: int = 64
_PING_BODY_FIXED_LEN: int = 16  # marker + counter, little-endian


def build_ping_body(counter: int) -> bytes:
    """Mirror make_body(counter) from the C++ service_interface.h header."""
    marker_counter = struct.pack("<QQ", PAYLOAD_MARKER, counter)
    padding = bytes((counter + i) & 0xFF for i in range(PING_BODY_SIZE - _PING_BODY_FIXED_LEN))
    body = marker_counter + padding
    assert len(body) == PING_BODY_SIZE
    return body


def parse_ping_body(payload: bytes, expected_counter: int) -> Optional[int]:
    """Return expected_counter iff *payload* matches that exact sequence body.

    Comparing against the receiver's expected counter prevents a coherent
    rewrite to a different, otherwise valid PingBody from passing.
    """
    if len(payload) != PING_BODY_SIZE:
        return None
    marker, counter = struct.unpack_from("<QQ", payload, 0)
    if marker != PAYLOAD_MARKER:
        return None
    if counter != expected_counter:
        return None
    if payload != build_ping_body(expected_counter):
        return None
    return expected_counter


# -----------------------------------------------------------------------------
# Minimal SOME/IP + SD encoding for this test
# -----------------------------------------------------------------------------


@dataclass(frozen=True)
class _SdEntry:
    entry_type: int
    service_id: int
    instance_id: int
    major_version: int
    ttl: int
    eventgroup_id: int = 0
    eventgroup_counter: int = 0
    options: tuple[tuple[str, int, int], ...] = ()


def _someip_packet(service_id: int, method_id: int, payload: bytes, session_id: int = 1) -> bytes:
    return (
        struct.pack(
            "!HHIHHBBBB",
            service_id,
            method_id,
            len(payload) + 8,
            0,
            session_id,
            1,
            1,
            SOMEIP_NOTIFICATION,
            0,
        )
        + payload
    )


def _parse_someip(packet: bytes) -> tuple[int, int, int, bytes]:
    if len(packet) < 16:
        raise ValueError("short SOME/IP packet")
    service_id, method_id, length, _, _, protocol, interface_version, message_type, return_code = struct.unpack_from(
        "!HHIHHBBBB", packet
    )
    if (
        protocol != 1
        or interface_version != 1
        or return_code != 0
        or length < 8
        or len(packet) != length + 8
    ):
        raise ValueError("malformed SOME/IP packet")
    return service_id, method_id, message_type, packet[16:]


def _endpoint_option(address: str, port: int) -> bytes:
    return struct.pack("!HBB4sBBH", 9, 0x04, 0, socket.inet_aton(address), 0, UDP_PROTOCOL, port)


def _parse_sd_datagram(packet: bytes) -> list[_SdEntry]:
    try:
        service_id, method_id, message_type, payload = _parse_someip(packet)
        if (
            service_id != SD_SERVICE_ID
            or method_id != SD_METHOD_ID
            or message_type != SD_MESSAGE_TYPE
            or len(payload) < 12
        ):
            return []

        entries_length = struct.unpack_from("!I", payload, 4)[0]
        entries_start = 8
        entries_end = entries_start + entries_length
        if entries_end + 4 > len(payload):
            return []
        options_length = struct.unpack_from("!I", payload, entries_end)[0]
        options_start = entries_end + 4
        if options_start + options_length != len(payload):
            return []

        options: list[tuple[str, int, int] | None] = []
        offset = options_start
        while offset < len(payload):
            option_length = struct.unpack_from("!H", payload, offset)[0]
            option_end = offset + option_length + 3
            if option_end > len(payload) or option_length < 1:
                return []
            option_type = payload[offset + 2]
            parsed_option = None
            if option_type == 0x04 and option_length == 9:
                address = socket.inet_ntoa(payload[offset + 4 : offset + 8])
                protocol = payload[offset + 9]
                port = struct.unpack_from("!H", payload, offset + 10)[0]
                parsed_option = (address, protocol, port)
            options.append(parsed_option)
            offset = option_end

        result = []
        if entries_length % 16 != 0:
            return []
        for offset in range(entries_start, entries_end, 16):
            entry_type, index_1, index_2, counts = struct.unpack_from("!BBBB", payload, offset)
            service_id, instance_id = struct.unpack_from("!HH", payload, offset + 4)
            version_ttl = struct.unpack_from("!I", payload, offset + 8)[0]
            major_version = version_ttl >> 24
            ttl = version_ttl & 0xFFFFFF
            option_indexes = list(range(index_1, index_1 + (counts >> 4)))
            option_indexes.extend(range(index_2, index_2 + (counts & 0x0F)))
            if any(index >= len(options) for index in option_indexes):
                return []
            endpoints = tuple(options[index] for index in option_indexes if options[index] is not None)
            eventgroup_id = 0
            eventgroup_counter = 0
            if entry_type in (SD_SUBSCRIBE, SD_SUBSCRIBE_ACK):
                reserved_and_counter = struct.unpack_from("!H", payload, offset + 12)[0]
                if reserved_and_counter & 0xFFF0:
                    return []
                eventgroup_counter = reserved_and_counter & 0x0F
                eventgroup_id = struct.unpack_from("!H", payload, offset + 14)[0]
            result.append(
                _SdEntry(
                    entry_type,
                    service_id,
                    instance_id,
                    major_version,
                    ttl,
                    eventgroup_id,
                    eventgroup_counter,
                    endpoints,
                )
            )
        return result
    except (OSError, struct.error, ValueError):
        return []


def _build_sd_entry(
    entry_type: int,
    service_id: int,
    instance_id: int,
    major_version: int,
    ttl: int,
    eventgroup_id: int = 0,
    has_endpoint_option: bool = False,
    eventgroup_counter: int = 0,
) -> bytes:
    option_counts = 0x10 if has_endpoint_option else 0
    entry = struct.pack(
        "!BBBBHHB",
        entry_type,
        0,
        0,
        option_counts,
        service_id,
        instance_id,
        major_version,
    )
    entry += ttl.to_bytes(3, "big")
    if entry_type == SD_OFFER_SERVICE:
        entry += struct.pack("!I", 0)
    else:
        entry += struct.pack("!HH", eventgroup_counter & 0x0F, eventgroup_id)
    return entry


def _multicast_socket(local_ip: str) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", SD_PORT))
    sock.setsockopt(
        socket.IPPROTO_IP,
        socket.IP_ADD_MEMBERSHIP,
        struct.pack("4s4s", socket.inet_aton(SD_MULTICAST_ADDR), socket.inet_aton(local_ip)),
    )
    return sock


def _unicast_sd_socket(local_ip: str) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton(local_ip))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
    sock.bind((local_ip, SD_PORT))
    return sock


# -----------------------------------------------------------------------------
# Host peer
# -----------------------------------------------------------------------------


@dataclass
class PeerConfig:
    host_ip: str
    dut_ip: str
    major_version: int = 1
    # DUT offers tx_service; peer subscribes and asserts received samples.
    tx_service_id: int = 0x1234
    tx_instance_id: int = 0x0001
    tx_eventgroup_id: int = 0x8001
    tx_event_id: int = 0x8001
    tx_expected_samples: int = 3
    # Peer offers rx_service; DUT subscribes and the peer sends notifications.
    rx_service_id: int = 0x1235
    rx_instance_id: int = 0x0001
    rx_eventgroup_id: int = 0x8002
    rx_event_id: int = 0x8002
    rx_notification_count: int = 20
    rx_notification_interval_s: float = 0.05
    # SD cadence and listening ports.
    offer_cycle_s: float = 1.0
    rx_notification_port: int = 31334
    tx_notification_port: int = 31335


class SomeIpHostPeer:
    """Context-managed bidirectional SOME/IP+SD peer.

    The SD dispatch thread routes unicast acknowledgements and subscriptions.
    ``dut_to_host`` subscribes to the DUT offer and collects notifications;
    ``host_to_dut`` offers the host service, acknowledges the DUT subscription,
    and sends notifications only after ``release_rx_notifications()``.
    """

    def __init__(self, cfg: PeerConfig):
        self._cfg = cfg
        self._stop = threading.Event()
        self._threads: List[threading.Thread] = []
        self._session_id = 0
        self._session_lock = threading.Lock()
        self._subscribe_acks: queue.Queue[_SdEntry] = queue.Queue()
        self._subscriptions: queue.Queue[_SdEntry] = queue.Queue()
        self._subscribe_ack_event = threading.Event()
        self._rx_subscription_event = threading.Event()
        self._dut_to_host_complete = threading.Event()
        self._start_rx_notifications = threading.Event()

        # Sockets (owned by the threads; closed in stop()).
        self._sd_mcast_sock: Optional[socket.socket] = None  # recv SD on multicast
        self._sd_unicast_sock: Optional[socket.socket] = None  # send/recv SD unicast
        self._rx_notif_sock: Optional[socket.socket] = None  # recv NOTIFs for DUT->host
        self._tx_notif_sock: Optional[socket.socket] = None  # send NOTIFs for host->DUT

        # Observations exposed to the test.
        self.dut_to_host_subscribed: bool = False
        self.dut_to_host_subscribe_ack: bool = False
        self.dut_to_host_valid_samples: List[int] = []
        self.host_to_dut_subscribed: bool = False
        self.host_to_dut_sent: int = 0
        self.errors: List[str] = []

    # ---------------------------------------------------------------- lifecycle

    def __enter__(self) -> "SomeIpHostPeer":
        self.start()
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        self.stop()

    def start(self) -> None:
        try:
            self._sd_mcast_sock = _multicast_socket(self._cfg.host_ip)
            self._sd_mcast_sock.settimeout(0.5)
            self._sd_unicast_sock = _unicast_sd_socket(self._cfg.host_ip)
            self._sd_unicast_sock.settimeout(0.5)

            self._rx_notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._rx_notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._rx_notif_sock.bind((self._cfg.host_ip, self._cfg.rx_notification_port))
            self._rx_notif_sock.settimeout(0.5)

            self._tx_notif_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._tx_notif_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._tx_notif_sock.bind((self._cfg.host_ip, self._cfg.tx_notification_port))

            for thread in (
                threading.Thread(target=self._sd_dispatch_loop, daemon=True),
                threading.Thread(target=self._dut_to_host_loop, daemon=True),
                threading.Thread(target=self._host_to_dut_loop, daemon=True),
            ):
                thread.start()
                self._threads.append(thread)
        except BaseException:
            self.stop()
            raise

    def stop(self) -> None:
        self._stop.set()
        self._start_rx_notifications.set()
        for thread in self._threads:
            if thread.is_alive():
                thread.join(timeout=5.0)
        for socket_name in ("_sd_mcast_sock", "_sd_unicast_sock", "_rx_notif_sock", "_tx_notif_sock"):
            sock = getattr(self, socket_name)
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
                finally:
                    setattr(self, socket_name, None)
        for note in self.errors:
            _log.warning("peer error: %s", note)

    def _next_session_id(self) -> int:
        with self._session_lock:
            self._session_id = self._session_id % 0xFFFF + 1
            return self._session_id

    def _build_sd_packet(self, entry: bytes, options: bytes = b"") -> bytes:
        payload = b"\x40\x00\x00\x00"
        payload += struct.pack("!I", len(entry)) + entry
        payload += struct.pack("!I", len(options)) + options
        return _someip_packet(SD_SERVICE_ID, SD_METHOD_ID, payload, self._next_session_id())

    def _sd_dispatch_loop(self) -> None:
        while not self._stop.is_set():
            try:
                packet, _ = self._sd_unicast_sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                return
            for entry in _parse_sd_datagram(packet):
                if (
                    entry.entry_type == SD_SUBSCRIBE_ACK
                    and entry.service_id == self._cfg.tx_service_id
                    and entry.instance_id == self._cfg.tx_instance_id
                ):
                    self._subscribe_acks.put(entry)
                elif (
                    entry.entry_type == SD_SUBSCRIBE
                    and entry.service_id == self._cfg.rx_service_id
                    and entry.instance_id == self._cfg.rx_instance_id
                ):
                    self._subscriptions.put(entry)

    # ------------------------------------------------------------- DUT -> host

    def _dut_to_host_loop(self) -> None:
        try:
            dut_endpoint = self._await_dut_offer()
            if dut_endpoint is None:
                self.errors.append("no DUT OfferService within deadline")
                return
            dut_ip, dut_port = dut_endpoint
            _log.info("peer: DUT TX offer at %s:%d, subscribing", dut_ip, dut_port)

            subscribe = _build_sd_entry(
                SD_SUBSCRIBE,
                self._cfg.tx_service_id,
                self._cfg.tx_instance_id,
                self._cfg.major_version,
                120,
                self._cfg.tx_eventgroup_id,
                has_endpoint_option=True,
            )
            subscribe_packet = self._build_sd_packet(
                subscribe,
                _endpoint_option(self._cfg.host_ip, self._cfg.rx_notification_port),
            )
            self.dut_to_host_subscribed = True

            if not self._await_subscribe_ack(subscribe_packet):
                self.errors.append("no SubscribeAck from DUT within deadline")
                return
            self.dut_to_host_subscribe_ack = True
            self._collect_dut_notifications()
        except Exception as exc:  # pragma: no cover - diagnostic only
            self.errors.append(f"dut_to_host_loop crashed: {exc}")
        finally:
            self._dut_to_host_complete.set()

    def _await_dut_offer(self) -> Optional[tuple]:
        """Capture SD datagrams until an OfferService for the DUT's TX service arrives."""
        deadline = time.monotonic() + 30.0
        while not self._stop.is_set() and time.monotonic() < deadline:
            try:
                data, _ = self._sd_mcast_sock.recvfrom(65535)
            except socket.timeout:
                continue
            for offer in _parse_sd_datagram(data):
                if (
                    offer.entry_type == SD_OFFER_SERVICE
                    and offer.service_id == self._cfg.tx_service_id
                    and offer.instance_id == self._cfg.tx_instance_id
                    and offer.major_version == self._cfg.major_version
                    and offer.ttl > 0
                ):
                    for address, protocol, port in offer.options:
                        if protocol == UDP_PROTOCOL:
                            return address, port
        return None

    def _await_subscribe_ack(self, subscribe_packet: bytes) -> bool:
        deadline = time.monotonic() + 30.0
        next_retry = time.monotonic()
        while not self._stop.is_set():
            now = time.monotonic()
            remaining = deadline - now
            if remaining <= 0:
                break
            if now >= next_retry:
                self._sd_unicast_sock.sendto(subscribe_packet, (self._cfg.dut_ip, SD_PORT))
                next_retry = now + 1.0
            try:
                entry = self._subscribe_acks.get(timeout=min(0.5, remaining))
            except queue.Empty:
                continue
            if (
                entry.eventgroup_id == self._cfg.tx_eventgroup_id
                and entry.major_version == self._cfg.major_version
                and entry.ttl > 0
            ):
                self._subscribe_ack_event.set()
                return True
        return False

    def wait_for_subscribe_ack(self, timeout_s: float) -> bool:
        """Wait until the DUT acknowledges the host's TX event subscription."""
        return self._subscribe_ack_event.wait(timeout_s)

    def wait_for_rx_subscription(self, timeout_s: float) -> bool:
        """Wait until the DUT subscribes to the host-offered RX service."""
        return self._rx_subscription_event.wait(timeout_s)

    def wait_for_dut_to_host_complete(self, timeout_s: float) -> bool:
        """Wait until collection of DUT-to-host notifications has finished."""
        return self._dut_to_host_complete.wait(timeout_s)

    def release_rx_notifications(self) -> None:
        """Allow host-to-DUT notifications after the guest app is ready."""
        self._start_rx_notifications.set()

    def _collect_dut_notifications(self) -> None:
        deadline = time.monotonic() + 60.0
        next_expected_counter = 0
        while (
            not self._stop.is_set()
            and time.monotonic() < deadline
            and len(self.dut_to_host_valid_samples) < self._cfg.tx_expected_samples
        ):
            try:
                data, _ = self._rx_notif_sock.recvfrom(65535)
            except socket.timeout:
                continue
            try:
                service_id, method_id, message_type, payload = _parse_someip(data)
            except ValueError as exc:
                self.errors.append(f"invalid DUT SOME/IP notification: {exc}")
                return
            if (
                service_id != self._cfg.tx_service_id
                or method_id != self._cfg.tx_event_id
                or message_type != SOMEIP_NOTIFICATION
            ):
                self.errors.append(
                    "unexpected DUT SOME/IP notification header: "
                    f"service=0x{service_id:04x}, method=0x{method_id:04x}, type={message_type}"
                )
                return
            counter = parse_ping_body(payload, next_expected_counter)
            if counter is None:
                self.errors.append(f"invalid DUT payload for counter {next_expected_counter}")
                return
            self.dut_to_host_valid_samples.append(counter)
            next_expected_counter += 1

        if (
            not self._stop.is_set()
            and len(self.dut_to_host_valid_samples) < self._cfg.tx_expected_samples
        ):
            self.errors.append(
                f"received {len(self.dut_to_host_valid_samples)}/"
                f"{self._cfg.tx_expected_samples} DUT notifications before deadline"
            )

    # ------------------------------------------------------------- host -> DUT

    def _host_to_dut_loop(self) -> None:
        try:
            dut_subscriber = self._offer_until_dut_subscribes()
            if dut_subscriber is None:
                self.errors.append("DUT never subscribed to host RX service")
                return
            self.host_to_dut_subscribed = True
            subscriber_ip, subscriber_port, eventgroup_counter = dut_subscriber
            self._send_subscribe_ack(eventgroup_counter)
            self._rx_subscription_event.set()
            self._start_rx_notifications.wait()
            if self._stop.is_set():
                return
            self._send_notifications_to_dut((subscriber_ip, subscriber_port))
        except Exception as exc:  # pragma: no cover - diagnostic only
            self.errors.append(f"host_to_dut_loop crashed: {exc}")

    def _send_offer(self) -> None:
        entry = _build_sd_entry(
            SD_OFFER_SERVICE,
            self._cfg.rx_service_id,
            self._cfg.rx_instance_id,
            self._cfg.major_version,
            120,
            has_endpoint_option=True,
        )
        packet = self._build_sd_packet(
            entry,
            _endpoint_option(self._cfg.host_ip, self._cfg.tx_notification_port),
        )
        self._sd_unicast_sock.sendto(packet, (SD_MULTICAST_ADDR, SD_PORT))

    def _offer_until_dut_subscribes(self) -> Optional[tuple[str, int, int]]:
        """Send OfferService on multicast every ``offer_cycle_s`` until the
        DUT replies with SubscribeEventgroup on our unicast SD socket."""
        deadline = time.monotonic() + 30.0
        next_offer = 0.0
        while not self._stop.is_set() and time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_offer:
                self._send_offer()
                next_offer = now + self._cfg.offer_cycle_s

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                break
            try:
                entry = self._subscriptions.get(timeout=min(0.5, remaining))
            except queue.Empty:
                continue
            if (
                entry.eventgroup_id != self._cfg.rx_eventgroup_id
                or entry.major_version != self._cfg.major_version
                or entry.ttl == 0
            ):
                continue
            for address, protocol, port in entry.options:
                if protocol == UDP_PROTOCOL:
                    return address, port, entry.eventgroup_counter
        return None

    def _send_subscribe_ack(self, eventgroup_counter: int) -> None:
        entry = _build_sd_entry(
            SD_SUBSCRIBE_ACK,
            self._cfg.rx_service_id,
            self._cfg.rx_instance_id,
            self._cfg.major_version,
            120,
            self._cfg.rx_eventgroup_id,
            eventgroup_counter=eventgroup_counter,
        )
        packet = self._build_sd_packet(entry)
        self._sd_unicast_sock.sendto(packet, (self._cfg.dut_ip, SD_PORT))

    def _send_notifications_to_dut(self, subscriber: tuple) -> None:
        subscriber_ip, subscriber_port = subscriber
        for i in range(self._cfg.rx_notification_count):
            if self._stop.is_set():
                return
            payload = build_ping_body(i)
            notification = _someip_packet(
                self._cfg.rx_service_id,
                self._cfg.rx_event_id,
                payload,
                session_id=(i + 1) & 0xFFFF or 1,
            )
            self._tx_notif_sock.sendto(notification, (subscriber_ip, subscriber_port))
            self.host_to_dut_sent += 1
            time.sleep(self._cfg.rx_notification_interval_s)


__all__ = ["PeerConfig", "SomeIpHostPeer", "build_ping_body", "parse_ping_body"]
