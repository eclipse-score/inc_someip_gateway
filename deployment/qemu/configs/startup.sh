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
slogger2 -s 4096                       # Start system logger with 4KB buffer size for log messages
waitfor /dev/slog                      # Wait for system log device to become available

echo "---> Starting PCI Services"
pci-server --config=/proc/boot/pci_server.cfg  # Start PCI server with configuration file
waitfor /dev/pci                        # Wait for PCI device manager to initialize

echo "---> Starting Pipe"
pipe                                    # Start named pipe resource manager for IPC
waitfor /dev/pipe                       # Wait for pipe device to become available

echo "---> Starting Random"
random                                  # Start random number generator device
waitfor /dev/random                     # Wait for random device to become available

echo "---> Starting fsevmgr"
fsevmgr                                 # Start file system event manager for file notifications
waitfor /dev/fsnotify                   # Wait for filesystem notification device

echo "---> Starting devb-ram"
devb-ram ram capacity=1 blk ramdisk=10m,cache=512k,vnode=256 2>/dev/null  # Create 10MB RAM disk with 512KB cache
waitfor /dev/ram0                       # Wait for RAM disk device to be ready

echo "---> mounting ram disk"
mkqnx6fs -q /dev/ram0                   # Create QNX6 filesystem on RAM disk (quiet mode)
waitfor /dev/ram0                       # Wait for filesystem creation to complete
mount -w /dev/ram0 /tmp_discovery        # Mount writable RAM disk at /tmp_discovery (required by LoLa)

echo "---> Setting up writable /var for vsomeip"
# Save SSH configs before mounting over /var (IFS /var is read-only)
mkdir -p /tmp/var_backup
cp -r /var/ssh /tmp/var_backup/ 2>/dev/null || true
# Mount RAM disk for writable /var
devb-ram ram capacity=2 blk ramdisk=5m,cache=256k,vnode=64 2>/dev/null
waitfor /dev/ram1 2
mkqnx6fs -q /dev/ram1
mount -w /dev/ram1 /var
# Restore SSH configs
cp -r /tmp/var_backup/ssh /var/ 2>/dev/null || true
mkdir -p /var/run

echo "---> Configuring network"
/etc/network_setup.sh                   # Execute network configuration script

echo "---> Starting pseudo-terminal manager"
devc-pty                                # Start PTY manager for SSH terminal sessions
waitfor /dev/ptyp0 5 2>/dev/null || true  # Wait up to 5 seconds for PTY device

echo "---> Starting SSH daemon"
/proc/boot/sshd -f /var/ssh/sshd_config  # Start SSH daemon for remote access

echo "---> Starting qconn (QNX target agent)"
qconn &                                 # Start QNX connection manager for IDE integration

echo "---> SOME/IP Gateway system ready"
echo "---> You can now start:"
echo "    /usr/bin/someipd --service_instance_manifest /etc/someipd/mw_com_config.json"
echo "    /usr/bin/gatewayd -config_file /etc/gatewayd/gatewayd_config.bin --service_instance_manifest /etc/gatewayd/mw_com_config.json"
echo ""
echo "---> On QEMU instance 2 (192.168.87.3), run sample_client:"
echo "    export VSOMEIP_CONFIGURATION=/etc/sample_client/vsomeip.json"
echo "    /usr/bin/sample_client"
