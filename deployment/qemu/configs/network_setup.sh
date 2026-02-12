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
# Network Configuration Script for QEMU SLIRP (user-mode) networking
# Uses static IP since QEMU SLIRP has known network layout:
#   Guest IP:    10.0.2.15
#   Gateway:     10.0.2.2
#   DNS:         10.0.2.3
#   Netmask:     255.255.255.0
# *******************************************************************************

echo "---> Starting Networking with static IP (QEMU SLIRP mode)"
io-sock -m phy -m pci -d vtnet_pci    # Start network stack with PHY and PCI modules, load VirtIO network driver

echo "---> Waiting for socket device..."
RETRY=0
while [ $RETRY -lt 10 ] && [ ! -e /dev/socket ]; do
    sleep 1
    RETRY=$((RETRY + 1))
done

if [ ! -e /dev/socket ]; then
    echo "---> ERROR: /dev/socket not available after 10 seconds"
    exit 1
fi
echo "---> Socket device ready"

echo "---> Bringing up network interface"
if_up -p vtnet0                        # Bring up the vtnet0 network interface in promiscuous mode

echo "---> Configuring static IP: 10.0.2.15"
ifconfig vtnet0 10.0.2.15 netmask 255.255.255.0

echo "---> Adding default route via 10.0.2.2"
route add default 10.0.2.2

# Configure system network settings
sysctl -w net.inet.icmp.bmcastecho=1 > /dev/null 2>&1

echo "---> Network configuration completed"
echo "---> IP: 10.0.2.15, Gateway: 10.0.2.2"

# Save status
echo "STATIC_IP" > /tmp_ram/network_status 2>/dev/null || true
echo "IP: 10.0.2.15" >> /tmp_ram/network_status 2>/dev/null || true
