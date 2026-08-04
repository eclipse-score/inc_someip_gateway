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
"""Shared network constants for TC8 conformance tests.

Single source of truth for port numbers and multicast addresses used
across all TC8 test modules and helpers.  Import from here instead of
hardcoding literals in individual files.

Port isolation for parallel Bazel execution
-------------------------------------------
Each Bazel TC8 target runs in its own OS process and receives unique port
values via the Bazel ``env`` attribute.  The three port constants below read
from environment variables at **module import time**, which means every
helper that ``from helpers.constants import SD_PORT`` gets the correct
per-process value with no function-signature changes.

Defaults reproduce the historical static values so that local developer
runs (without Bazel, no env vars set) continue to work unchanged.
"""

import os

#: SOME/IP Service Discovery port (UDP).  Both DUT and tester must bind
#: to this port.  The SOME/IP-SD stack drops SD packets arriving from any
#: source port other than the configured SD port.  Read from ``TC8_SD_PORT``
#: env var; defaults to 30490 (the well-known SOME/IP-SD port) for local
#: development.
SD_PORT: int = int(os.environ.get("TC8_SD_PORT", "30490"))

#: SOME/IP-SD multicast group address (all SOME/IP nodes join this group).
SD_MULTICAST_ADDR: str = "224.244.224.245"

#: DUT unreliable (UDP) service port — matches the ``unreliable`` port in
#: the DUT's ``tc8_someipd_*.json`` configuration templates.  Read from
#: ``TC8_SVC_PORT`` env var; defaults to 30509 for local development.
DUT_UNRELIABLE_PORT: int = int(os.environ.get("TC8_SVC_PORT", "30509"))

#: DUT reliable (TCP) service port — matches the ``reliable`` port in
#: the DUT's ``tc8_someipd_*.json`` configuration templates.  Read from
#: ``TC8_SVC_TCP_PORT`` env var; defaults to 30510 for local development.
DUT_RELIABLE_PORT: int = int(os.environ.get("TC8_SVC_TCP_PORT", "30510"))

# ---------------------------------------------------------------------------
# ETS Sec. 5 service identity
# ---------------------------------------------------------------------------
SERVICE_ID: int = int(os.environ.get("TC8_SERVICE_ID", "0x1234"), 16)
INSTANCE_ID: int = int(os.environ.get("TC8_INSTANCE_ID", "0x5678"), 16)
MAJOR_VERSION: int = 1
MINOR_VERSION: int = 0

# ---------------------------------------------------------------------------
# ETS Sec. 5.2 event and field notification IDs
# ---------------------------------------------------------------------------
EVENT_TEST_UINT8: int = 0x8001
EVENT_TEST_UINT8_ARRAY: int = 0x8002
EVENT_TEST_UINT8_RELIABLE: int = 0x8003
EVENT_TEST_UINT8_E2E: int = 0x8004
EVENT_INTERFACE_VERSION: int = 0x8005
EVENT_FIELD_UINT8: int = 0x8006
EVENT_FIELD_UINT8_ARRAY: int = 0x8007
EVENT_FIELD_UINT8_RELIABLE: int = 0x8008
EVENT_TEST_UINT8_MULTICAST: int = 0x800B

# ---------------------------------------------------------------------------
# ETS Sec. 5.1 method IDs
# ---------------------------------------------------------------------------
METHOD_ECHO_UINT8: int = 0x08
METHOD_ECHO_UINT8_ARRAY: int = 0x09
METHOD_ECHO_UINT8_RELIABLE: int = 0x0A
METHOD_INTERFACE_VERSION_GET: int = 0x25
METHOD_FIELD_UINT8_GET: int = 0x26
METHOD_FIELD_UINT8_SET: int = 0x27
METHOD_FIELD_UINT8_ARRAY_GET: int = 0x28
METHOD_FIELD_UINT8_ARRAY_SET: int = 0x29
METHOD_FIELD_UINT8_RELIABLE_GET: int = 0x2A
METHOD_FIELD_UINT8_RELIABLE_SET: int = 0x2B

# ---------------------------------------------------------------------------
# ETS Sec. 5.2 eventgroup IDs
# ---------------------------------------------------------------------------
EVENTGROUP_UDP_UNICAST: int = 0x0002
EVENTGROUP_TCP_RELIABLE: int = 0x0005
EVENTGROUP_UDP_MULTICAST: int = 0x0006

# ---------------------------------------------------------------------------
# Multicast transport
# ---------------------------------------------------------------------------
MULTICAST_ADDR: str = "239.0.0.1"
MULTICAST_EVENT_PORT: int = 40490
