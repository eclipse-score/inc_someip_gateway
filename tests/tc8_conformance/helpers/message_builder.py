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
"""SOME/IP message construction helpers for TC8 conformance tests.

Builds REQUEST, REQUEST_NO_RETURN, and intentionally malformed packets.
"""

from someip.header import (
    SOMEIPHeader,
    SOMEIPMessageType,
    SOMEIPReturnCode,
)


def build_request(
    service_id: int,
    method_id: int,
    client_id: int = 0x0001,
    session_id: int = 0x0001,
    interface_version: int = 0x00,
    payload: bytes = b"",
) -> bytes:
    """Build a SOME/IP REQUEST message."""
    return SOMEIPHeader(
        service_id=service_id,
        method_id=method_id,
        client_id=client_id,
        session_id=session_id,
        interface_version=interface_version,
        message_type=SOMEIPMessageType.REQUEST,
        return_code=SOMEIPReturnCode.E_OK,
        payload=payload,
    ).build()


def build_request_no_return(
    service_id: int,
    method_id: int,
    client_id: int = 0x0001,
    session_id: int = 0x0001,
    interface_version: int = 0x00,
    payload: bytes = b"",
) -> bytes:
    """Build a SOME/IP REQUEST_NO_RETURN (fire-and-forget) message."""
    return SOMEIPHeader(
        service_id=service_id,
        method_id=method_id,
        client_id=client_id,
        session_id=session_id,
        interface_version=interface_version,
        message_type=SOMEIPMessageType.REQUEST_NO_RETURN,
        return_code=SOMEIPReturnCode.E_OK,
        payload=payload,
    ).build()


# ---------------------------------------------------------------------------
# Malformed message builders — TC8-MSG-007
# ---------------------------------------------------------------------------


def build_truncated_message() -> bytes:
    """Return 7 raw bytes — one byte shorter than the minimum 8-byte SOME/IP header.

    The DUT must not crash when it receives this (TC8-MSG-007).
    """
    # service_id=0x1234, method_id=0x0421, length=0x000000 (3 bytes, truncated)
    return b"\x12\x34\x04\x21\x00\x00\x00"


def build_wrong_protocol_version_request(
    service_id: int,
    method_id: int,
    client_id: int = 0x0001,
    session_id: int = 0x0001,
    interface_version: int = 0x00,
) -> bytes:
    """Build a valid REQUEST but with protocol_version patched to 0xFF.

    SOME/IP wire layout: byte 12 is protocol_version.
    The DUT should reject or drop this message (TC8-MSG-007).
    """
    raw = build_request(
        service_id,
        method_id,
        client_id=client_id,
        session_id=session_id,
        interface_version=interface_version,
    )
    # Byte 12 = protocol_version; patch it to an invalid value.
    return raw[:12] + b"\xff" + raw[13:]


def build_oversized_message(
    service_id: int,
    method_id: int,
    client_id: int = 0x0001,
    session_id: int = 0x0001,
    interface_version: int = 0x00,
) -> bytes:
    """Build a 16-byte packet whose length field claims 0x7FF3 bytes of payload.

    The actual UDP payload is only 16 bytes so the DUT will receive a
    packet far shorter than advertised.  The DUT must not crash (TC8-MSG-007).
    """
    raw = build_request(
        service_id,
        method_id,
        client_id=client_id,
        session_id=session_id,
        interface_version=interface_version,
    )
    # SOME/IP length field = bytes 4–7; it counts bytes from byte 8 onward.
    # A claim of 0x7FF3 means the message body should be 32755 bytes but is only 8.
    return raw[:4] + b"\x00\x00\x7f\xf3" + raw[8:]


# ---------------------------------------------------------------------------
# Group 3 message builders — protocol behaviour tests
# ---------------------------------------------------------------------------


def build_notification_as_request(
    service_id: int,
    method_id: int,
    client_id: int = 0x0001,
    session_id: int = 0x0001,
    interface_version: int = 0x00,
    payload: bytes = b"",
) -> bytes:
    """Build a SOME/IP packet with message_type=NOTIFICATION (0x02).

    A NOTIFICATION sent in the client→server direction is invalid per the
    SOME/IP spec.  Used by ETS_075: the DUT must not send a RESPONSE.
    """
    return SOMEIPHeader(
        service_id=service_id,
        method_id=method_id,
        client_id=client_id,
        session_id=session_id,
        interface_version=interface_version,
        message_type=SOMEIPMessageType.NOTIFICATION,
        return_code=SOMEIPReturnCode.E_OK,
        payload=payload,
    ).build()


def build_request_with_return_code(
    service_id: int,
    method_id: int,
    return_code: int,
    client_id: int = 0x0001,
    session_id: int = 0x0001,
    interface_version: int = 0x00,
    payload: bytes = b"",
) -> bytes:
    """Build a REQUEST with an explicit return_code byte value.

    Per SOME/IP spec the return_code in a REQUEST must be E_OK (0x00).
    Setting it to a non-zero value tests DUT robustness (RPC_06/07/08).
    The return_code byte is byte 15 in the SOME/IP header wire layout.
    """
    raw = build_request(
        service_id,
        method_id,
        client_id=client_id,
        session_id=session_id,
        interface_version=interface_version,
        payload=payload,
    )
    # Byte 15 = return_code; patch directly since SOMEIPReturnCode enum
    # does not accept arbitrary integer values.
    return raw[:15] + bytes([return_code & 0xFF]) + raw[16:]
