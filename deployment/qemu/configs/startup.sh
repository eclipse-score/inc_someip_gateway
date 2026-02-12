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
# SOME/IP Gateway System Startup Script
# Executed during system initialization to start essential services
# *******************************************************************************
echo "---> Starting slogger2"
slogger2 &
waitfor /dev/slog2
pipe &
waitfor /dev/pipe

# Start File System Event Manager (required for inotify on QNX)
fsevmgr &

# Create a RAM-backed writable filesystem for LoLa service discovery.
# /dev/shmem does NOT support mkdir or inotify, (crashes or errors at startup )  idea is to use devb-ram + QNX6 FS.
devb-ram ram capacity=1 blk ramdisk=4m,cache=512k,vnode=256 &
waitfor /dev/ram0 5
ksh -c "echo y | mkqnx6fs /dev/ram0"
mount -w /dev/ram0 /tmp_discovery


# waitfor /dev/slog                      # Wait for system log device to become available

echo "---> Starting PCI Services"
pci-server --config=/proc/boot/pci_server.cfg  # Start PCI server with configuration file
waitfor /dev/pci                        # Wait for PCI device manager to initialize

echo "---> Starting Pipe"
pipe                                    # Start named pipe resource manager for IPC
waitfor /dev/pipe                       # Wait for pipe device to become available

echo "---> Starting Random"
random                                  # Start random number generator device
waitfor /dev/random                     # Wait for random device to become available

# echo "---> Starting fsevmgr"
# fsevmgr                                 # Start file system event manager for file notifications
# waitfor /dev/fsnotify                   # Wait for filesystem notification device

# echo "---> Starting devb-ram"
# devb-ram ram capacity=1 blk ramdisk=10m,cache=512k,vnode=256 2>/dev/null  # Create 10MB RAM disk with 512KB cache
# waitfor /dev/ram0                       # Wait for RAM disk device to be ready



echo "---> Configuring network"
/etc/network_setup.sh                   # Execute network configuration script

echo "---> Starting SSH daemon"
/proc/boot/sshd -f /var/ssh/sshd_config  # Start SSH daemon for remote access

echo "---> SOME/IP Gateway system ready"
echo "---> You can now start:"
echo "    /usr/bin/someipd"
echo "    /usr/bin/gatewayd -config_file /etc/gatewayd/gatewayd_config.bin --service_instance_manifest /etc/gatewayd/mw_com_config.json"
