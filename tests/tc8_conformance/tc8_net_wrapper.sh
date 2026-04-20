#!/usr/bin/env bash
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
# Creates a private network namespace with loopback multicast routing,
# then exec's the wrapped test command.
set -euo pipefail

# Try namespace isolation; fall back to direct execution.
# On locked-down systems, the test's own environment check (conftest.py)
# handles the skip with an actionable message.
unshare --user --net --map-root-user -- bash -c '
    ip link set lo up
    ip route add 224.0.0.0/4 dev lo
    exec "$@"
' -- "$@" && exit 0

# Fallback: unshare not available — run directly, let conftest.py handle it.
echo "WARNING: tc8_net_wrapper.sh: failed to create network namespace." >&2
exec "$@"
