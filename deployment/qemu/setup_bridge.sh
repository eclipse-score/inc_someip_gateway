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

# Setup bridge networking for QEMU instances
# This script creates a bridge (virbr0) and configures it for QEMU networking.
#
# Usage:
#   sudo ./setup_bridge.sh          # Create bridge and configure it
#   sudo ./setup_bridge.sh teardown # Remove bridge and clean up
#
# Prerequisites:
#   - bridge-utils package (brctl command)
#   - iproute2 package (ip command)
#   - Linux kernel with bridging support
#   - Root privileges
#
# Network layout:
#   Bridge: virbr0
#   IP: 192.168.87.1/24
#   QEMU Instance 1: 192.168.87.2
#   QEMU Instance 2: 192.168.87.3
#

set -euo pipefail

BRIDGE_NAME="virbr0"
BRIDGE_IP="192.168.87.1"
BRIDGE_NETMASK="255.255.255.0"
BRIDGE_NETWORK="192.168.87.0/24"

# QEMU bridge helper path
QEMU_BRIDGE_HELPER="/usr/lib/qemu/qemu-bridge-helper"
QEMU_BRIDGE_ACL="/etc/qemu/bridge.conf"

print_usage() {
    echo "Usage: $0 [setup|teardown|status]"
    echo ""
    echo "Commands:"
    echo "  setup     - Create and configure bridge (default)"
    echo "  teardown  - Remove bridge and clean up"
    echo "  status    - Show bridge status"
    echo ""
    echo "Network Configuration:"
    echo "  Bridge:      ${BRIDGE_NAME}"
    echo "  Bridge IP:   ${BRIDGE_IP}/${BRIDGE_NETMASK}"
    echo "  QEMU 1:      192.168.87.2"
    echo "  QEMU 2:      192.168.87.3"
}

check_root() {
    if [[ $EUID -ne 0 ]]; then
        echo "ERROR: This script must be run as root (sudo)"
        exit 1
    fi
}

check_prerequisites() {
    local missing=0

    if ! command -v brctl &>/dev/null && ! command -v ip &>/dev/null; then
        echo "ERROR: Neither brctl nor ip command found. Install bridge-utils or iproute2."
        missing=1
    fi

    if ! command -v iptables &>/dev/null; then
        echo "WARNING: iptables not found. NAT may not work."
    fi

    return $missing
}

setup_bridge() {
    echo "=== Setting up bridge network: ${BRIDGE_NAME} ==="

    # Check if bridge already exists
    if ip link show "${BRIDGE_NAME}" &>/dev/null; then
        echo "[INFO] Bridge ${BRIDGE_NAME} already exists, skipping creation"
    else
        echo "[INFO] Creating bridge ${BRIDGE_NAME}..."
        ip link add name "${BRIDGE_NAME}" type bridge
        ip link set "${BRIDGE_NAME}" up
    fi

    # Configure bridge IP
    if ! ip addr show "${BRIDGE_NAME}" | grep -q "${BRIDGE_IP}"; then
        echo "[INFO] Configuring bridge IP: ${BRIDGE_IP}"
        ip addr add "${BRIDGE_IP}/${BRIDGE_NETMASK}" dev "${BRIDGE_NAME}"
    else
        echo "[INFO] Bridge IP ${BRIDGE_IP} already configured"
    fi

    # Enable IP forwarding
    echo "[INFO] Enabling IP forwarding..."
    echo 1 > /proc/sys/net/ipv4/ip_forward

    # Setup NAT for bridge network (optional, for internet access from guests)
    echo "[INFO] Setting up NAT for internet access..."

    # Get default route interface
    DEFAULT_IFACE=$(ip route | grep default | awk '{print $5}' | head -1)

    if [[ -n "${DEFAULT_IFACE}" ]]; then
        # Add masquerade rule if not exists
        if ! iptables -t nat -C POSTROUTING -s "${BRIDGE_NETWORK}" -o "${DEFAULT_IFACE}" -j MASQUERADE 2>/dev/null; then
            iptables -t nat -A POSTROUTING -s "${BRIDGE_NETWORK}" -o "${DEFAULT_IFACE}" -j MASQUERADE
            echo "[INFO] Added NAT rule for ${BRIDGE_NETWORK} via ${DEFAULT_IFACE}"
        else
            echo "[INFO] NAT rule already exists"
        fi

        # Allow forwarding to/from bridge
        if ! iptables -C FORWARD -i "${BRIDGE_NAME}" -o "${DEFAULT_IFACE}" -j ACCEPT 2>/dev/null; then
            iptables -A FORWARD -i "${BRIDGE_NAME}" -o "${DEFAULT_IFACE}" -j ACCEPT
            iptables -A FORWARD -i "${DEFAULT_IFACE}" -o "${BRIDGE_NAME}" -m state --state RELATED,ESTABLISHED -j ACCEPT
        fi
    else
        echo "[WARNING] No default route interface found, NAT not configured"
    fi

    # Allow bridge traffic
    if ! iptables -C FORWARD -i "${BRIDGE_NAME}" -o "${BRIDGE_NAME}" -j ACCEPT 2>/dev/null; then
        iptables -A FORWARD -i "${BRIDGE_NAME}" -o "${BRIDGE_NAME}" -j ACCEPT
    fi

    # Configure QEMU bridge helper ACL
    echo "[INFO] Configuring QEMU bridge helper..."
    mkdir -p "$(dirname "${QEMU_BRIDGE_ACL}")"
    if [[ ! -f "${QEMU_BRIDGE_ACL}" ]] || ! grep -q "allow ${BRIDGE_NAME}" "${QEMU_BRIDGE_ACL}" 2>/dev/null; then
        echo "allow ${BRIDGE_NAME}" >> "${QEMU_BRIDGE_ACL}"
        echo "[INFO] Added ${BRIDGE_NAME} to ${QEMU_BRIDGE_ACL}"
    fi

    # Set bridge helper permissions (needs setuid for non-root QEMU)
    if [[ -f "${QEMU_BRIDGE_HELPER}" ]]; then
        chmod u+s "${QEMU_BRIDGE_HELPER}"
        echo "[INFO] Set setuid on ${QEMU_BRIDGE_HELPER}"
    else
        echo "[WARNING] QEMU bridge helper not found at ${QEMU_BRIDGE_HELPER}"
        echo "         Install qemu-system-x86 package or run QEMU as root"
    fi

    # Configure multicast support for SOME/IP Service Discovery
    # Disable IGMP snooping to allow multicast flooding between guests
    echo "[INFO] Configuring multicast support..."
    echo 0 > /sys/devices/virtual/net/${BRIDGE_NAME}/bridge/multicast_snooping
    echo "[INFO] Disabled IGMP snooping on ${BRIDGE_NAME}"

    # Add multicast route through the bridge
    if ! ip route show | grep -q "224.0.0.0/4 dev ${BRIDGE_NAME}"; then
        ip route add 224.0.0.0/4 dev "${BRIDGE_NAME}" 2>/dev/null || true
        echo "[INFO] Added multicast route via ${BRIDGE_NAME}"
    else
        echo "[INFO] Multicast route already exists"
    fi

    echo ""
    echo "=== Bridge setup complete ==="
    show_status
}

teardown_bridge() {
    echo "=== Tearing down bridge network: ${BRIDGE_NAME} ==="

    # Remove multicast route
    ip route del 224.0.0.0/4 dev "${BRIDGE_NAME}" 2>/dev/null || true

    # Remove NAT rules
    DEFAULT_IFACE=$(ip route | grep default | awk '{print $5}' | head -1)
    if [[ -n "${DEFAULT_IFACE}" ]]; then
        iptables -t nat -D POSTROUTING -s "${BRIDGE_NETWORK}" -o "${DEFAULT_IFACE}" -j MASQUERADE 2>/dev/null || true
        iptables -D FORWARD -i "${BRIDGE_NAME}" -o "${DEFAULT_IFACE}" -j ACCEPT 2>/dev/null || true
        iptables -D FORWARD -i "${DEFAULT_IFACE}" -o "${BRIDGE_NAME}" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null || true
    fi
    iptables -D FORWARD -i "${BRIDGE_NAME}" -o "${BRIDGE_NAME}" -j ACCEPT 2>/dev/null || true

    # Remove bridge
    if ip link show "${BRIDGE_NAME}" &>/dev/null; then
        echo "[INFO] Removing bridge ${BRIDGE_NAME}..."
        ip link set "${BRIDGE_NAME}" down
        ip link delete "${BRIDGE_NAME}" type bridge
    else
        echo "[INFO] Bridge ${BRIDGE_NAME} does not exist"
    fi

    echo "=== Bridge teardown complete ==="
}

show_status() {
    echo ""
    echo "=== Bridge Status ==="

    if ip link show "${BRIDGE_NAME}" &>/dev/null; then
        echo "[OK] Bridge ${BRIDGE_NAME} exists"
        ip addr show "${BRIDGE_NAME}"
        echo ""
        echo "Connected interfaces:"
        brctl show "${BRIDGE_NAME}" 2>/dev/null || bridge link show master "${BRIDGE_NAME}" 2>/dev/null || echo "  (none)"
    else
        echo "[NOT FOUND] Bridge ${BRIDGE_NAME} does not exist"
        echo "Run: sudo $0 setup"
        return 1
    fi

    echo ""
    echo "NAT rules:"
    iptables -t nat -L POSTROUTING -n 2>/dev/null | grep -E "${BRIDGE_NETWORK}|MASQ" || echo "  (none)"

    echo ""
    echo "QEMU bridge helper:"
    if [[ -f "${QEMU_BRIDGE_ACL}" ]]; then
        echo "  ACL file: ${QEMU_BRIDGE_ACL}"
        cat "${QEMU_BRIDGE_ACL}"
    else
        echo "  [WARNING] ${QEMU_BRIDGE_ACL} not found"
    fi

    echo ""
    echo "To run QEMU with bridge networking:"
    echo "  ./run_qemu.sh <IFS_IMAGE> 1"
    echo "  ./run_qemu.sh <IFS_IMAGE> 2"
}

# Main
case "${1:-setup}" in
    setup)
        check_root
        check_prerequisites
        setup_bridge
        ;;
    teardown)
        check_root
        teardown_bridge
        ;;
    status)
        show_status
        ;;
    -h|--help|help)
        print_usage
        ;;
    *)
        echo "Unknown command: $1"
        print_usage
        exit 1
        ;;
esac
