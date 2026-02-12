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
# Launch a QNX x86_64 IFS image under QEMU with networking support.
#
# Usage (standalone):
#   ./run_qemu.sh <IFS_IMAGE>
#
# Usage (via Bazel):
#   bazel run //deployment/qemu:run_qemu --config=x86_64-qnx
#
# Network modes:
#   - Default: User-mode (SLIRP) networking - works everywhere including WSL2
#   - Bridge:  Set QEMU_NET_BRIDGE=1 to use virbr0 bridge (requires libvirt setup)
#
# Port forwarding (user-mode):
#   - Host port 2222 -> Guest port 22 (SSH)
#   - Host port 5555 -> Guest port 30490 (SOME/IP)
#

set -euo pipefail

IFS_IMAGE=$1

echo "========================================"
echo "  QNX SOME/IP Gateway — QEMU (x86_64)"
echo "========================================"
echo "  IFS image: ${IFS_IMAGE}"

# Check if bridge networking is requested and available
if [[ "${QEMU_NET_BRIDGE:-0}" == "1" ]] && [[ -f /etc/qemu/bridge.conf ]]; then
    echo "  Network:   virtio-net-pci on virbr0 (bridge)"
    NET_ARGS="-netdev bridge,id=net0,br=virbr0 -device virtio-net-pci,netdev=net0"
else
    echo "  Network:   user-mode (SLIRP)"
    echo "  SSH:       localhost:2222 -> guest:22"
    echo "  SOME/IP:   localhost:5555 -> guest:30490"
    NET_ARGS="-netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=udp::5555-:30490 -device virtio-net-pci,netdev=net0"
fi

echo "========================================"
echo ""

exec qemu-system-x86_64 \
    -enable-kvm \
    -smp 2 \
    -cpu host \
    -m 1G \
    -pidfile /tmp/qemu-someip-gateway.pid \
    -nographic \
    -kernel "${IFS_IMAGE}" \
    -chardev stdio,id=char0,signal=on,mux=on \
    -mon chardev=char0,mode=readline \
    -serial chardev:char0 \
    -object rng-random,filename=/dev/urandom,id=rng0 \
    -device virtio-rng-pci,rng=rng0 \
    ${NET_ARGS}
