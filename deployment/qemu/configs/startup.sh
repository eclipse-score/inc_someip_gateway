#!/bin/sh
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


# *******************************************************************************
# SOME/IP Gateway System Startup Script
# Executed during system initialization to start essential services
# *******************************************************************************

# Set PATH for system tools
export PATH=/proc/boot:/bin:/usr/bin:/sbin:/usr/sbin:$PATH

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

#!/bin/sh

# Usage: setup_ramdisk <device> <mount_point> <size> <cache> <vnodes> <capacity_id>
setup_ramdisk() {
    devb-ram ram capacity=$6 blk ramdisk=$3,cache=$4,vnode=$5 2>/dev/null
    waitfor $1
    mkqnx6fs -q $1
    mount -w $1 $2
}

echo "---> Initializing RAM disks"

# 1. Setup 10MB RAM disk for LoLa discovery
setup_ramdisk /dev/ram0 /tmp_discovery 10m 512k 256 1

# 2. Setup 5MB RAM disk for writable /var (preserving sshd_config)
cp /var/ssh/sshd_config /tmp/sshd_config_backup 2>/dev/null || true
setup_ramdisk /dev/ram1 /var 5m 256k 64 2

echo "---> Configuring SSH environment"
# Restore config and generate host key
mkdir -p /var/ssh /var/run
cp /tmp/sshd_config_backup /var/ssh/sshd_config 2>/dev/null || true
ssh-keygen -t rsa -f /var/ssh/ssh_host_rsa_key -N "" -q
chmod 400 /var/ssh/ssh_host_rsa_key

echo "---> Starting Network and PTY Manager"
/etc/network_setup.sh
devc-pty
waitfor /dev/ptyp0 5 2>/dev/null || true

echo "---> Starting SSH daemon"
/proc/boot/sshd -f /var/ssh/sshd_config

echo "---> SOME/IP Gateway system ready"
