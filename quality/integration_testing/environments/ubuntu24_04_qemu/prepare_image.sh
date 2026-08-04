#!/usr/bin/env bash
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

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $0 <base-image> <output-image>" >&2
    exit 1
fi

base_image="$1"
output_image="$2"

tmp_dir="$(mktemp -d)"
cleanup() {
  rm -rf "${tmp_dir}"
}
trap cleanup EXIT

cp --reflink=auto "${base_image}" "${output_image}"
chmod u+w "${output_image}"

cat >"${tmp_dir}/user-data" <<'EOF'
#cloud-config
# Prepare base image: minimize boot overhead by disabling non-essential packages and services.

package_update: true
packages:
  - iputils-ping
  - libatomic1

# Remove unnecessary packages to reduce image size and boot time
bootcmd:
  # Disable non-essential services - be conservative to avoid breaking system
  # Disable update services (not needed for test environment)
  - systemctl mask apt-daily.timer apt-daily-upgrade.timer unattended-upgrades
  # Disable hardware-specific services not needed for QEMU
  - systemctl mask accounts-daemon multipathd e2scrub.timer e2scrub_all.timer
  # Disable bluetooth, printing, and mDNS services
  - systemctl mask bluetooth cups cups-browsed avahi-daemon
  # Disable unnecessary timer jobs
  - systemctl mask motd-news.timer

power_state:
  mode: poweroff
  timeout: 120
EOF

cat >"${tmp_dir}/meta-data" <<'EOF'
instance-id: ubuntu24-prepare-image
local-hostname: ubuntu24-prepare-image
EOF

cloud-localds "${tmp_dir}/seed.img" "${tmp_dir}/user-data" "${tmp_dir}/meta-data"

# Boot once with user-mode networking (NAT) to provide internet access for apt.
# Cloud-init installs iputils-ping and powers the VM off when complete.
timeout 25m qemu-system-x86_64 \
    -machine accel=kvm:tcg \
    -cpu max \
    -smp 2 \
    -m 2048 \
    -drive if=virtio,format=qcow2,file="${output_image}" \
    -drive if=virtio,format=raw,file="${tmp_dir}/seed.img" \
    -nic user,model=virtio-net-pci \
    -nographic \
    -display none \
    -serial none \
    -monitor none \
    -no-reboot

qemu-img info "${output_image}" >/dev/null
