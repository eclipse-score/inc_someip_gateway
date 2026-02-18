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

# This script starts QEMU 2 and runs the SOME/IP sample client

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
# Bridge mode: SSH directly to guest IP on bridge network
GUEST_IP="192.168.87.3"
SSH_HOST="${GUEST_IP}"
SSH_USER="root"

echo "=== QEMU Instance 2 Setup (${GUEST_IP}) ==="
echo ""

# Check if bridge exists
if ! ip link show virbr0 &>/dev/null; then
    echo "[ERROR] Bridge virbr0 does not exist!"
    exit 1
fi

# Check if QEMU is already running
if [[ -f "/tmp/qemu-someip-gateway-2.pid" ]] && kill -0 "$(cat /tmp/qemu-someip-gateway-2.pid)" 2>/dev/null; then
    echo "[INFO] QEMU instance 2 already running"
else
    echo "[INFO] Starting QEMU instance 2..."
    cd "${WORKSPACE_ROOT}"
    bazel run //deployment/qemu:run_qemu_2 --config=x86_64-qnx &
    QEMU_PID=$!
    echo "[INFO] QEMU started with PID ${QEMU_PID}"
fi

# Wait for SSH to become available
echo "[INFO] Waiting for SSH on ${GUEST_IP}..."
RETRY=0
MAX_RETRIES=10
while [ $RETRY -lt $MAX_RETRIES ]; do
    if ssh ${SSH_OPTS} ${SSH_USER}@${SSH_HOST} "echo ready" 2>/dev/null; then
        echo "[INFO] SSH is ready"
        break
    fi
    RETRY=$((RETRY + 1))
    sleep 2
done

if [ $RETRY -eq $MAX_RETRIES ]; then
    echo "[ERROR] Timeout waiting for SSH"
    exit 1
fi

# Run sample_client interactively (foreground)
ssh ${SSH_OPTS} ${SSH_USER}@${SSH_HOST} \
    'export VSOMEIP_CONFIGURATION=/etc/sample_client/vsomeip.json && /usr/bin/sample_client'
