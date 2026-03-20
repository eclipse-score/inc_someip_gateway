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
# Manual debug launcher for QEMU Instance 1 (192.168.87.2)
#
# Usage:
#   1. Build the IFS image first:
#        bazel build //deployment/qemu:someip_gateway_ifs --config=x86_64-qnx
#   2. Run QEMU only:
#        ./deployment/qemu/run_qemu1_manual.sh
#   3. Run QEMU and auto-start gatewayd + someipd:
#        ./deployment/qemu/run_qemu1_manual.sh --daemons

set -euo pipefail

IFS_IMAGE="bazel-bin/deployment/qemu/someip_gateway_x86_64.ifs"
GUEST_IP="192.168.87.2"
SSH_USER="root"
SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
START_SERVICES=false

if [[ "${1:-}" == "--daemons" ]]; then
    START_SERVICES=true
fi

if [[ ! -f "${IFS_IMAGE}" ]]; then
    echo "ERROR: IFS image not found at ${IFS_IMAGE}"
    echo "Build it first: bazel build //deployment/qemu:someip_gateway_ifs --config=x86_64-qnx"
    exit 1
fi

if [[ "${START_SERVICES}" == "true" ]]; then
    # Launch QEMU in background, wait for SSH, then start services
    deployment/qemu/run_qemu.sh "${IFS_IMAGE}" 1 &
    QEMU_PID=$!
    trap "kill ${QEMU_PID} 2>/dev/null" EXIT

    echo "Waiting for QEMU 1 to boot..."
    for i in $(seq 1 30); do
        if ssh ${SSH_OPTS} ${SSH_USER}@${GUEST_IP} "echo ready" &>/dev/null; then
            echo "QEMU 1 is up."
            break
        fi
        sleep 1
    done

    echo "Starting gatewayd..."
    ssh ${SSH_OPTS} -f ${SSH_USER}@${GUEST_IP} \
        "/usr/bin/gatewayd -config_file /etc/gatewayd/gatewayd_config.bin --service_instance_manifest /etc/gatewayd/mw_com_config.json > /dev/null 2>&1"
    sleep 3

    echo "Starting someipd..."
    ssh ${SSH_OPTS} -f ${SSH_USER}@${GUEST_IP} \
        "export VSOMEIP_CONFIGURATION=/etc/someipd/vsomeip.json; /usr/bin/someipd --service_instance_manifest /etc/someipd/mw_com_config.json > /dev/null 2>&1"
    sleep 2

    echo "Services started on QEMU 1 (${GUEST_IP}). Dropping into SSH shell..."
    echo "Type 'exit' to leave the shell. QEMU will be stopped automatically."
    echo ""
    ssh ${SSH_OPTS} -t ${SSH_USER}@${GUEST_IP} || true
    echo "SSH session ended. Stopping QEMU..."
    kill ${QEMU_PID} 2>/dev/null
    wait ${QEMU_PID} 2>/dev/null
else
    exec deployment/qemu/run_qemu.sh "${IFS_IMAGE}" 1
fi
