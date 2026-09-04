#!/bin/bash
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
#
# Prepares the network for the end-to-end performance test and then execs the test.
#
# Used as --run_under for //tests/benchmarks:e2e_benchmarks (see --config=perf-tests). It
# creates an
# isolated user and network namespace, which gives the test CAP_NET_ADMIN without requiring
# privileged Bazel sandbox execution.

set -ueo pipefail

BENCH_IP="127.0.0.2"
ECHO_IP="127.0.0.3"
SD_MULTICAST_ADDRESS="224.244.224.245"

# vsomeip only offers services once it has seen the interface owning its unicast address and a
# route for the SD multicast address, so both have to exist even on loopback.
setup_network() {
    ip link set lo up

    for address in "${BENCH_IP}" "${ECHO_IP}"; do
        ip address add "${address}/8" dev lo
    done

    ip route add "${SD_MULTICAST_ADDRESS}/32" dev lo

    exec "$@"
}

# First: enter new network and user namespaces, then reexec this script
if [[ "${E2E_PERF_NETWORK_SETUP:-}" != "1" ]]; then
    exec env E2E_PERF_NETWORK_SETUP=1 unshare --user --net --map-root-user --fork -- /bin/bash "$0" "$@"
fi

# Second: configure the network and exec the test
setup_network "$@"
