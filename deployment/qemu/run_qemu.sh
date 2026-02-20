#!/bin/bash
# *******************************************************************************
# Copyright (c) 2025 Contributors to the Eclipse Foundation
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
# Launch a QNX x86_64 IFS image under QEMU with bridge networking.
#
# Usage (via Bazel):
#   bazel run //deployment/qemu:run_qemu --config=x86_64-qnx
#
# Environment variables:
#   QEMU_INSTANCE_ID  - Instance ID (1 or 2) for multi-instance setups (default: 1)
#
# Network layout (bridge mode):
#   Bridge:        virbr0 (192.168.87.1/24)
#   Instance 1:    192.168.87.2 (MAC: 52:54:00:12:34:01)
#   Instance 2:    192.168.87.3 (MAC: 52:54:00:12:34:02)
#   Gateway:       192.168.87.1 (host)
#

set -euo pipefail

IFS_IMAGE=$1
INSTANCE_ID=${2:-${QEMU_INSTANCE_ID:-1}}

# Bridge configuration
BRIDGE_NAME="virbr0"

# Calculate guest IP and MAC based on instance ID
GUEST_IP="192.168.87.$((1 + INSTANCE_ID))"  # Instance 1: .2, Instance 2: .3
GUEST_MAC="52:54:00:12:34:0${INSTANCE_ID}"

echo "========================================"
echo "  QNX SOME/IP Gateway — QEMU (x86_64)"
echo "========================================"
echo "  IFS image:    ${IFS_IMAGE}"
echo "  Instance ID:  ${INSTANCE_ID}"
echo "  Network mode: bridge"
echo "  Bridge:       ${BRIDGE_NAME}"
echo "  Guest IP:     ${GUEST_IP}"
echo "  Guest MAC:    ${GUEST_MAC}"
echo "  Gateway:      192.168.87.1"

# Verify bridge exists
if ! ip link show "${BRIDGE_NAME}" &>/dev/null; then
    echo ""
    echo "ERROR: Bridge ${BRIDGE_NAME} does not exist!"
    exit 1
fi

echo "========================================"
echo ""

# Bridge networking - single NIC connected to virbr0
# Both QEMU instances are on the same L2 network for direct communication
NET_ARGS="-netdev bridge,id=net0,br=${BRIDGE_NAME} -device virtio-net-pci,netdev=net0,mac=${GUEST_MAC}"

# Pass instance ID to guest via kernel command line (QNX ignores unknown params)
exec qemu-system-x86_64 \
    -enable-kvm \
    -smp 2 \
    -cpu host \
    -m 1G \
    -pidfile "/tmp/qemu-someip-gateway-${INSTANCE_ID}.pid" \
    -nographic \
    -kernel "${IFS_IMAGE}" \
    -append "instance_id=${INSTANCE_ID}" \
    -chardev stdio,id=char0,signal=on,mux=on \
    -mon chardev=char0,mode=readline \
    -serial chardev:char0 \
    -object rng-random,filename=/dev/urandom,id=rng0 \
    -device virtio-rng-pci,rng=rng0 \
    ${NET_ARGS}
