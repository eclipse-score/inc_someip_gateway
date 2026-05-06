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
# Adds a multicast route for SOME/IP-SD inside the network namespace
# created by Bazel's linux-sandbox (--sandbox_default_allow_network=false).
# linux-sandbox brings up the loopback interface automatically (-N flag);
# we only add the multicast route that it does not provide.
# The test target must carry the "requires-fakeroot" tag so that
# linux-sandbox maps uid 0 (CAP_NET_ADMIN) inside the user namespace.
# The --spawn_strategy=worker,linux-sandbox in .bazelrc guarantees that
# linux-sandbox is used; Bazel fails explicitly if it is unavailable.
set -euo pipefail

ip route add 224.0.0.0/4 dev lo 2>/dev/null || true
exec "$@"
