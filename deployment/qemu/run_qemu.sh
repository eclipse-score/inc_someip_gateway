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
# Launch a QNX x86_64 IFS image under QEMU.
#
# Usage (standalone):
#   ./run_qemu.sh <IFS_IMAGE>
#
# Usage (via Bazel):
#   bazel run //deployment/qemu:run_qemu --config=x86_64-qnx
#

set -euo pipefail

IFS_IMAGE=$1

echo "========================================"
echo "  QNX SOME/IP Gateway — QEMU (x86_64)"
echo "========================================"
echo "  IFS image: ${IFS_IMAGE}"
echo "========================================"
echo ""

exec qemu-system-x86_64 \
    -smp 2 \
    -m 1024 \
    -nographic \
    -serial mon:stdio \
    -kernel "${IFS_IMAGE}" \
    -object rng-random,filename=/dev/urandom,id=rng0 \
    -device virtio-rng-pci,rng=rng0 \
    -accel kvm \
    -cpu host
