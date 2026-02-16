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

# Setup QEMU Instance 1 with gatewayd and someipd
# This script starts QEMU 1 and launches the gateway services
#
# Prerequisites:
#   - Run 'sudo ./setup_bridge.sh' first to create the bridge network

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WORKSPACE_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
# Bridge mode: SSH directly to guest IP on bridge network
GUEST_IP="192.168.87.2"
SSH_HOST="${GUEST_IP}"
SSH_USER="root"

echo "=== QEMU Instance 1 Setup (${GUEST_IP}) ==="
echo ""

# Check if bridge exists
if ! ip link show virbr0 &>/dev/null; then
    echo "[ERROR] Bridge virbr0 does not exist!"
    echo "        Run: sudo ./setup_bridge.sh"
    exit 1
fi

# Check if QEMU is already running
if [[ -f "/tmp/qemu-someip-gateway-1.pid" ]] && kill -0 "$(cat /tmp/qemu-someip-gateway-1.pid)" 2>/dev/null; then
    echo "[INFO] QEMU instance 1 already running"
else
    echo "[INFO] Starting QEMU instance 1..."
    cd "${WORKSPACE_ROOT}"
    bazel run //deployment/qemu:run_qemu_1 --config=x86_64-qnx &
    QEMU_PID=$!
    echo "[INFO] QEMU started with PID ${QEMU_PID}"
fi

# Wait for SSH to become available
echo "[INFO] Waiting for SSH on ${GUEST_IP}..."
RETRY=0
MAX_RETRIES=60
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

echo ""
echo "[INFO] Starting gatewayd..."
ssh ${SSH_OPTS} ${SSH_USER}@${SSH_HOST} \
    '/usr/bin/gatewayd -config_file /etc/gatewayd/gatewayd_config.bin --service_instance_manifest /etc/gatewayd/mw_com_config.json &' &

sleep 3

echo "[INFO] Starting someipd..."
ssh ${SSH_OPTS} ${SSH_USER}@${SSH_HOST} \
    'export VSOMEIP_CONFIGURATION=/etc/someipd/vsomeip.json && /usr/bin/someipd --service_instance_manifest /etc/someipd/mw_com_config.json &' &

sleep 2

echo ""
echo "=== QEMU Instance 1 Ready ==="
echo "Services running on ${GUEST_IP}:"
echo "  - gatewayd"
echo "  - someipd"
echo ""
echo "To SSH manually: ssh ${SSH_OPTS} ${SSH_USER}@${SSH_HOST}"
echo ""
echo "Now run setup_qemu_2.sh to start the client on instance 2"
