#!/bin/sh
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


# *******************************************************************************
# Network Configuration Script for QEMU Bridge Networking
#
# This script configures the network interface for bridge mode.
# Both QEMU instances are on the same L2 network (virbr0 bridge).
#
# Network layout:
#   Bridge (host): virbr0 - 192.168.122.1/24
#   vtnet0:
#     Instance 1:  192.168.122.2 (MAC: 52:54:00:12:34:01)
#     Instance 2:  192.168.122.3 (MAC: 52:54:00:12:34:02)
#     Gateway:     192.168.122.1
#     Netmask:     255.255.255.0
#
# Both instances can communicate directly on the same L2 network,
# including SOME/IP multicast traffic.
# *******************************************************************************

# Detect instance ID from MAC address
# QEMU sets unique MACs: Instance 1: 52:54:00:12:34:01, Instance 2: 52:54:00:12:34:02
INSTANCE_ID=1

echo "---> Starting Networking"
io-sock -m phy -m pci -d vtnet_pci    # Start network stack with PHY and PCI modules, load VirtIO network driver
echo "---> Waiting for socket device..."
waitfor /dev/socket


# Bring up vtnet0
echo "---> Bringing up vtnet0"
if_up -p vtnet0

# Detect instance ID from vtnet0 MAC address
VTNET0_MAC=$(ifconfig vtnet0 2>/dev/null | grep ether | awk '{print $2}')
case "$VTNET0_MAC" in
    *:01) INSTANCE_ID=1 ;;
    *:02) INSTANCE_ID=2 ;;
    *:03) INSTANCE_ID=3 ;;
esac
echo "---> Detected instance ${INSTANCE_ID} from MAC: ${VTNET0_MAC}"

# Calculate IP address: 192.168.87.(1 + INSTANCE_ID)
# Instance 1: 192.168.87.2, Instance 2: 192.168.87.3
GUEST_IP="192.168.87.$((1 + INSTANCE_ID))"
GATEWAY_IP="192.168.87.1"

echo "---> Configuring vtnet0: ${GUEST_IP}"
ifconfig vtnet0 ${GUEST_IP} netmask 255.255.255.0

echo "---> Adding default route via ${GATEWAY_IP}"
route add default ${GATEWAY_IP}

# Enable multicast support for SOME/IP Service Discovery
echo "---> Enabling multicast support"
ifconfig vtnet0 multicast 2>/dev/null || true
ifconfig vtnet0 allmulti 2>/dev/null || true

# Add multicast route for SOME/IP Service Discovery (224.0.0.0/4)
echo "---> Adding multicast route via vtnet0"
route add 224.0.0.0 -netmask 240.0.0.0 -iface vtnet0 2>/dev/null || true

# Configure system network settings for multicast support
sysctl -w net.inet.icmp.bmcastecho=1 > /dev/null 2>&1
sysctl -w net.inet.ip.mforwarding=1 > /dev/null 2>&1
sysctl -w net.inet.udp.recvspace=262144 > /dev/null 2>&1

echo "---> Network configuration completed"
echo "---> Instance: ${INSTANCE_ID}"
echo "---> vtnet0: ${GUEST_IP}, Gateway: ${GATEWAY_IP}"

# Save status
echo "INSTANCE_ID=${INSTANCE_ID}" > /tmp_discovery/network_status 2>/dev/null || true
echo "VTNET0_IP=${GUEST_IP}" >> /tmp_discovery/network_status 2>/dev/null || true
echo "GATEWAY_IP=${GATEWAY_IP}" >> /tmp_discovery/network_status 2>/dev/null || true
