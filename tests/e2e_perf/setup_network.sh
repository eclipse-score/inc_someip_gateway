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
# Used as --run_under for //tests/e2e_perf:e2e_perf (see --config=perf-tests), so it runs
# inside the linux-sandbox network namespace, which only provides a loopback interface.
# The two nodes therefore talk over loopback addresses, which need no configuration; only
# non-loopback addresses (E2E_PERF_NODE_A_IP / E2E_PERF_NODE_B_IP) have to be added to an
# interface, which requires CAP_NET_ADMIN in the namespace.

set -u

NODE_A_IP="${E2E_PERF_NODE_A_IP:-127.0.0.2}"
NODE_B_IP="${E2E_PERF_NODE_B_IP:-127.0.0.3}"
NETMASK="${E2E_PERF_NETMASK:-255.0.0.0}"
DEVICE="${E2E_PERF_DEVICE:-lo}"
SD_MULTICAST_ADDRESS="224.244.224.245"

log() {
    echo "[setup_network] $*" >&2
}

# vsomeip only offers services once it has seen the interface owning its unicast address and a
# route for the SD multicast address, so both have to exist even on loopback.
prefix_length() {
    local netmask="$1" length=0 octet
    for octet in ${netmask//./ }; do
        while ((octet & 128)); do
            length=$((length + 1))
            octet=$((octet << 1 & 255))
        done
    done
    echo "${length}"
}

if ! command -v ip >/dev/null 2>&1; then
    log "'ip' not found, leaving the network as it is"
else
    ip link set "${DEVICE}" up 2>/dev/null || log "could not bring ${DEVICE} up"

    for address in "${NODE_A_IP}" "${NODE_B_IP}"; do
        address_info="$(ip -brief address show to "${address}/32")"
        if [[ -n "${address_info}" ]]; then
            continue
        fi
        address_prefix="$(prefix_length "${NETMASK}")"
        ip address add "${address}/${address_prefix}" dev "${DEVICE}" ||
            log "could not add ${address} to ${DEVICE}, the test will skip"
    done

    routes="$(ip route show)"
    if ! grep -q "^${SD_MULTICAST_ADDRESS}" <<<"${routes}"; then
        ip route add "${SD_MULTICAST_ADDRESS}/32" dev "${DEVICE}" ||
            log "could not add a route for ${SD_MULTICAST_ADDRESS}, the test will skip"
    fi
fi

export E2E_PERF_NODE_A_IP="${NODE_A_IP}"
export E2E_PERF_NODE_B_IP="${NODE_B_IP}"
export E2E_PERF_NETMASK="${NETMASK}"

exec "$@"
