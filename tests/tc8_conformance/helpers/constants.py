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
