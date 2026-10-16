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
"""SOME/IP and SOME/IP-SD types for TC8 conformance helpers.

Provides classes for building and parsing SOME/IP / SOME/IP-SD packets using
scapy as the serializer/parser backend.  All socket I/O stays in the calling
helper modules.
"""

from __future__ import annotations

import dataclasses
import enum
import ipaddress
import struct
import typing

from scapy.contrib.automotive.someip import (
    SD,
    SOMEIP,
    SDEntry_EventGroup,
    SDEntry_Service,
    SDOption_IP4_EndPoint,
    SDOption_IP4_Multicast,
)
from scapy.packet import Raw

T = typing.TypeVar("T", bound="SOMEIPSDOption")

SD_SERVICE: int = SD.SOMEIP_MSGID_SRVID
SD_METHOD: int = SD.SOMEIP_MSGID_SUBID
SD_INTERFACE_VERSION: int = SD.SOMEIP_IFACE_VER


# ---------------------------------------------------------------------------
# SOME/IP message header
# ---------------------------------------------------------------------------


class SOMEIPMessageType(enum.IntEnum):
    REQUEST = 0
    REQUEST_NO_RETURN = 1
    NOTIFICATION = 2
    REQUEST_ACK = 0x40
    REQUEST_NO_RETURN_ACK = 0x41
    NOTIFICATION_ACK = 0x42
    RESPONSE = 0x80
    ERROR = 0x81
    RESPONSE_ACK = 0xC0
    ERROR_ACK = 0xC1


class SOMEIPReturnCode(enum.IntEnum):
    E_OK = 0
    E_NOT_OK = 1
    E_UNKNOWN_SERVICE = 2
    E_UNKNOWN_METHOD = 3
    E_NOT_READY = 4
    E_NOT_REACHABLE = 5
    E_TIMEOUT = 6
    E_WRONG_PROTOCOL_VERSION = 7
    E_WRONG_INTERFACE_VERSION = 8
    E_MALFORMED_MESSAGE = 9
    E_WRONG_MESSAGE_TYPE = 10


@dataclasses.dataclass(frozen=True)
class SOMEIPHeader:
    """Represents a top-level SOME/IP packet (header and payload)."""

    service_id: int
    method_id: int
    client_id: int
    session_id: int
    interface_version: int
    message_type: SOMEIPMessageType
    protocol_version: int = 1
    return_code: SOMEIPReturnCode = SOMEIPReturnCode.E_OK
    payload: bytes = b""

    def build(self) -> bytes:
        """Build the byte representation of this SOME/IP packet."""
        pkt = SOMEIP(
            srv_id=self.service_id,
            sub_id=self.method_id,
            client_id=self.client_id,
            session_id=self.session_id,
            proto_ver=self.protocol_version,
            iface_ver=self.interface_version,
            msg_type=int(self.message_type),
            retcode=int(self.return_code),
        )
        pkt = pkt / Raw(load=self.payload)
        return bytes(pkt)

    @classmethod
    def parse(cls, buf: bytes) -> typing.Tuple["SOMEIPHeader", bytes]:
        """Parse one SOME/IP packet from the front of *buf*.

        :param buf: buffer containing (at least) one SOME/IP packet
        :raises ValueError: if *buf* is too short, or the header contains an
            invalid protocol version, message type, or return code
        :return: tuple ``(header, buf_rest)``, the parsed header and the
            unparsed remainder of *buf* (empty if *buf* held exactly one
            message)
        """
        if len(buf) < 8:
            raise ValueError(f"can not parse SOMEIPHeader, got only {len(buf)} bytes")
        size = struct.unpack("!I", buf[4:8])[0]
        if size < 8:
            raise ValueError("SOMEIP length must be at least 8")
        total_size = 8 + size
        if len(buf) < total_size:
            raise ValueError(f"packet too short, expected {total_size}, got {len(buf)}")

        # Slice exactly one message so scapy does not chain into subsequent
        # messages in multi-message UDP datagrams.
        exact = bytes(buf[:total_size])
        buf_rest = bytes(buf[total_size:])

        pkt = SOMEIP(exact)
        if pkt.proto_ver != 1:
            raise ValueError(f"bad someip protocol version 0x{pkt.proto_ver:02x}, expected 0x01")
        message_type = SOMEIPMessageType(pkt.msg_type)
        return_code = SOMEIPReturnCode(pkt.retcode)
        # scapy auto-dissects known service IDs (e.g. SD) into typed layers,
        # leaving pkt.data as None. For other service IDs the payload is Raw.
        if pkt.data:
            payload = bytes(pkt.data[0])
        elif pkt.payload:
            payload = bytes(pkt.payload)
        else:
            payload = b""

        parsed = cls(
            service_id=pkt.srv_id,
            method_id=pkt.sub_id,
            client_id=pkt.client_id,
            session_id=pkt.session_id,
            interface_version=pkt.iface_ver,
            message_type=message_type,
            protocol_version=pkt.proto_ver,
            return_code=return_code,
            payload=payload,
        )
        return parsed, buf_rest


# ---------------------------------------------------------------------------
# SOME/IP-SD options
# ---------------------------------------------------------------------------


class L4Protocols(enum.IntEnum):
    """Layer 4 protocol identifiers used in SD IPv4/IPv6 endpoint options."""

    TCP = 6
    UDP = 17


class SOMEIPSDOption:
    """Abstract base class for SD options (parity with ``someip.header``)."""

    def build(self) -> bytes:
        """Build the byte representation of this SD option."""
        return bytes(_option_to_scapy(self))


@dataclasses.dataclass(frozen=True)
class IPv4EndpointOption(SOMEIPSDOption):
    """SD IPv4 Endpoint Option (type 0x04)."""

    address: ipaddress.IPv4Address
    l4proto: typing.Union[L4Protocols, int]
    port: int

    def __str__(self) -> str:  # pragma: nocover
        name = self.l4proto.name if isinstance(self.l4proto, L4Protocols) else f"{self.l4proto:#x}"
        return f"{self.address}:{self.port} ({name})"


@dataclasses.dataclass(frozen=True)
class IPv4MulticastOption(SOMEIPSDOption):
    """SD IPv4 Multicast Option (type 0x14).

    Same fields as :class:`IPv4EndpointOption` but a distinct type.
    Do not use ``isinstance`` against :class:`IPv4EndpointOption` to match
    both, since they are not related by inheritance.
    """

    address: ipaddress.IPv4Address
    l4proto: typing.Union[L4Protocols, int]
    port: int

    def __str__(self) -> str:  # pragma: nocover
        name = self.l4proto.name if isinstance(self.l4proto, L4Protocols) else f"{self.l4proto:#x}"
        return f"{self.address}:{self.port} ({name})"


def _l4proto_from_int(val: int) -> typing.Union[L4Protocols, int]:
    try:
        return L4Protocols(val)
    except ValueError:
        return val


def _option_to_scapy(option: SOMEIPSDOption):
    """Convert an SD option object to its scapy wire representation."""
    if type(option) is IPv4EndpointOption:
        return SDOption_IP4_EndPoint(addr=str(option.address), l4_proto=int(option.l4proto), port=option.port)
    if type(option) is IPv4MulticastOption:
        return SDOption_IP4_Multicast(addr=str(option.address), l4_proto=int(option.l4proto), port=option.port)
    raise TypeError(f"cannot build unsupported SD option type: {type(option)!r}")


def _option_from_scapy(option) -> SOMEIPSDOption:
    """Convert a dissected scapy SD option into an SD option object."""
    if type(option) is SDOption_IP4_EndPoint:
        return IPv4EndpointOption(
            address=ipaddress.IPv4Address(option.addr),
            l4proto=_l4proto_from_int(int(option.l4_proto)),
            port=int(option.port),
        )
    if type(option) is SDOption_IP4_Multicast:
        return IPv4MulticastOption(
            address=ipaddress.IPv4Address(option.addr),
            l4proto=_l4proto_from_int(int(option.l4_proto)),
            port=int(option.port),
        )
    # Unrecognised option type: return the raw scapy object so parsing does
    # not crash for option types not used by the current helpers.
    return option


# ---------------------------------------------------------------------------
# SOME/IP-SD entries
# ---------------------------------------------------------------------------


class SOMEIPSDEntryType(enum.IntEnum):
    FindService = 0
    OfferService = 1
    Subscribe = 6
    SubscribeAck = 7


def _find(haystack, needle):
    """Return the index of *needle* in *haystack*, or ``None`` if absent."""
    h = len(haystack)
    n = len(needle)
    if n == 0:
        return None
    skip = {needle[i]: n - i - 1 for i in range(n - 1)}
    i = n - 1
    while i < h:
        for j in range(n):
            if haystack[i - j] != needle[-j - 1]:
                i += skip.get(haystack[i], n)
                break
        else:
            return i - n + 1
    return None


@dataclasses.dataclass(frozen=True)
class SOMEIPSDEntry:
    """Represents an Entry in a SOME/IP-SD packet."""

    sd_type: SOMEIPSDEntryType
    service_id: int
    instance_id: int
    major_version: int
    ttl: int
    minver_or_counter: int

    options_1: typing.Tuple[SOMEIPSDOption, ...] = ()
    options_2: typing.Tuple[SOMEIPSDOption, ...] = ()

    option_index_1: typing.Optional[int] = None
    option_index_2: typing.Optional[int] = None
    num_options_1: typing.Optional[int] = None
    num_options_2: typing.Optional[int] = None

    @property
    def options(self) -> typing.Tuple[SOMEIPSDOption, ...]:
        """Convenience wrapper combining :attr:`options_1` and :attr:`options_2`."""
        return self.options_1 + self.options_2

    @property
    def options_resolved(self) -> bool:
        """Indicates if the options on this instance are resolved."""
        return (
            self.option_index_1 is None
            or self.option_index_2 is None
            or self.num_options_1 is None
            or self.num_options_2 is None
        )

    def resolve_options(self, options: typing.Tuple[SOMEIPSDOption, ...]) -> "SOMEIPSDEntry":
        """Resolve this entry's options against the containing header's option list.

        :return: a new :class:`SOMEIPSDEntry` with :attr:`options_1` /
            :attr:`options_2` populated and the index/count fields cleared
        """
        if self.options_resolved:
            raise ValueError("options already resolved")

        oi1 = typing.cast(int, self.option_index_1)
        oi2 = typing.cast(int, self.option_index_2)
        no1 = typing.cast(int, self.num_options_1)
        no2 = typing.cast(int, self.num_options_2)

        return dataclasses.replace(
            self,
            options_1=options[oi1 : oi1 + no1],
            options_2=options[oi2 : oi2 + no2],
            option_index_1=None,
            option_index_2=None,
            num_options_1=None,
            num_options_2=None,
        )

    @staticmethod
    def _assign_option(entry_options, hdr_options) -> typing.Tuple[int, int]:
        if not entry_options:
            return (0, 0)

        no = len(entry_options)
        oi = _find(hdr_options, entry_options)
        if oi is None:
            oi = len(hdr_options)
            hdr_options.extend(entry_options)
        return oi, no

    def assign_option_index(self, options: typing.List[SOMEIPSDOption]) -> "SOMEIPSDEntry":
        """Assign option indexes, appending to *options* if not already present.

        Deduplicates: entries sharing the same option run reuse the same index
        range instead of appending duplicates.
        """
        if not self.options_resolved:
            return dataclasses.replace(self)  # pragma: nocover

        oi1, no1 = self._assign_option(self.options_1, options)
        oi2, no2 = self._assign_option(self.options_2, options)
        return dataclasses.replace(
            self,
            option_index_1=oi1,
            option_index_2=oi2,
            num_options_1=no1,
            num_options_2=no2,
            options_1=(),
            options_2=(),
        )

    def build(self) -> bytes:
        """Build the byte representation of this SD entry.

        Requires option indexes to be assigned first (see
        :meth:`assign_option_index`).
        """
        return bytes(_entry_to_scapy(self))

    @property
    def service_minor_version(self) -> int:
        """The service minor version (FindService / OfferService entries only)."""
        if self.sd_type not in (SOMEIPSDEntryType.FindService, SOMEIPSDEntryType.OfferService):
            raise TypeError(f"SD entry is type {self.sd_type}, does not have service_minor_version")
        return self.minver_or_counter

    @property
    def eventgroup_counter(self) -> int:
        """The eventgroup counter (Subscribe / SubscribeAck entries only)."""
        if self.sd_type not in (SOMEIPSDEntryType.Subscribe, SOMEIPSDEntryType.SubscribeAck):
            raise TypeError(f"SD entry is type {self.sd_type}, does not have eventgroup_counter")
        return (self.minver_or_counter >> 16) & 0x0F

    @property
    def eventgroup_id(self) -> int:
        """The eventgroup id (Subscribe / SubscribeAck entries only)."""
        if self.sd_type not in (SOMEIPSDEntryType.Subscribe, SOMEIPSDEntryType.SubscribeAck):
            raise TypeError(f"SD entry is type {self.sd_type}, does not have eventgroup_id")
        return self.minver_or_counter & 0xFFFF


def _entry_to_scapy(entry: SOMEIPSDEntry):
    """Convert an SD entry (with assigned option indexes) to a scapy object."""
    if entry.options_resolved:
        raise ValueError("option indexes must be assigned before building (see assign_option_index())")

    if entry.sd_type in (SOMEIPSDEntryType.FindService, SOMEIPSDEntryType.OfferService):
        return SDEntry_Service(
            type=int(entry.sd_type),
            index_1=entry.option_index_1,
            index_2=entry.option_index_2,
            n_opt_1=entry.num_options_1,
            n_opt_2=entry.num_options_2,
            srv_id=entry.service_id,
            inst_id=entry.instance_id,
            major_ver=entry.major_version,
            ttl=entry.ttl,
            minor_ver=entry.minver_or_counter,
        )

    cnt = (entry.minver_or_counter >> 16) & 0x0F
    eventgroup_id = entry.minver_or_counter & 0xFFFF
    return SDEntry_EventGroup(
        type=int(entry.sd_type),
        index_1=entry.option_index_1,
        index_2=entry.option_index_2,
        n_opt_1=entry.num_options_1,
        n_opt_2=entry.num_options_2,
        srv_id=entry.service_id,
        inst_id=entry.instance_id,
        major_ver=entry.major_version,
        ttl=entry.ttl,
        res=0,
        cnt=cnt,
        eventgroup_id=eventgroup_id,
    )


def _entry_from_scapy(entry) -> SOMEIPSDEntry:
    """Convert a dissected scapy SD entry into an SOMEIPSDEntry."""
    sd_type = SOMEIPSDEntryType(int(entry.type))
    if sd_type in (SOMEIPSDEntryType.FindService, SOMEIPSDEntryType.OfferService):
        minver_or_counter = int(entry.minor_ver)
    else:
        minver_or_counter = ((int(entry.cnt) & 0x0F) << 16) | (int(entry.eventgroup_id) & 0xFFFF)

    return SOMEIPSDEntry(
        sd_type=sd_type,
        service_id=int(entry.srv_id),
        instance_id=int(entry.inst_id),
        major_version=int(entry.major_ver),
        ttl=int(entry.ttl),
        minver_or_counter=minver_or_counter,
        option_index_1=int(entry.index_1),
        option_index_2=int(entry.index_2),
        num_options_1=int(entry.n_opt_1),
        num_options_2=int(entry.n_opt_2),
    )


# ---------------------------------------------------------------------------
# SOME/IP-SD header (top-level SD packet)
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class SOMEIPSDHeader:
    """Represents a SOME/IP-SD packet."""

    entries: typing.Tuple[SOMEIPSDEntry, ...]
    options: typing.Tuple[SOMEIPSDOption, ...] = ()
    flag_reboot: bool = False
    flag_unicast: bool = True
    flags_unknown: int = 0

    def resolve_options(self) -> "SOMEIPSDHeader":
        """Resolve all ``entries``' options from the ``options`` list.

        :return: a new :class:`SOMEIPSDHeader` with entries that have
            resolved ``options_1`` / ``options_2`` tuples
        """
        entries = [e.resolve_options(self.options) for e in self.entries]
        return dataclasses.replace(self, entries=tuple(entries))

    def assign_option_indexes(self) -> "SOMEIPSDHeader":
        """Assign option indexes to all ``entries``, building the ``options`` list."""
        options: typing.List[SOMEIPSDOption] = list(self.options)
        entries = [e.assign_option_index(options) for e in self.entries]
        return dataclasses.replace(self, entries=tuple(entries), options=tuple(options))

    def build(self) -> bytes:
        """Build the byte representation of this SOME/IP-SD packet."""
        flags = self.flags_unknown
        if self.flag_reboot:
            flags |= 0x80
        if self.flag_unicast:
            flags |= 0x40

        sd = SD(flags=flags)
        sd.set_entryArray([_entry_to_scapy(e) for e in self.entries])
        sd.set_optionArray([_option_to_scapy(o) for o in self.options])
        return bytes(sd)

    @classmethod
    def parse(cls, buf: bytes) -> typing.Tuple["SOMEIPSDHeader", bytes]:
        """Parse a SOME/IP-SD packet from *buf*.

        Entries are returned with ``options_1``/``options_2`` as empty tuples;
        callers must call :meth:`resolve_options` to populate them.

        :return: tuple ``(header, buf_rest)``
        """
        sd = SD(buf)
        entries = tuple(_entry_from_scapy(e) for e in sd.entry_array)
        options = tuple(_option_from_scapy(o) for o in sd.option_array)

        flags = int(sd.flags)
        flag_reboot = bool(flags & 0x80)
        flag_unicast = bool(flags & 0x40)
        flags_unknown = flags & ~0xC0

        parsed = cls(
            entries=entries,
            options=options,
            flag_reboot=flag_reboot,
            flag_unicast=flag_unicast,
            flags_unknown=flags_unknown,
        )
        buf_rest = bytes(sd.payload) if sd.payload else b""
        return parsed, buf_rest
