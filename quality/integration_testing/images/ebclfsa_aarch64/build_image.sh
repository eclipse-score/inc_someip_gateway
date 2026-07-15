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

set -uoex pipefail

if [[ $# -lt 4 ]]; then
    echo "Error: Expected at least 4 arguments (working directory, image source location, image target location, and one or more tar files to deploy relative to the root of the image)" >&2
    exit 1
fi

# Check if sshpass is installed
if ! command -v sshpass &> /dev/null; then
    echo "Error: sshpass is not installed. Please install it to proceed." >&2
    exit 1
fi

WORKING_DIR="$1"
IMAGE_SOURCE="$2"
IMAGE_TARGET="$3"
shift 3
DEPLOY_SRCS=("$@")

if [[ ! -d "${IMAGE_SOURCE}" ]]; then
    echo "Error: Image source location is not a directory: ${IMAGE_SOURCE}" >&2
    exit 1
fi

if ! find "${IMAGE_SOURCE}" -maxdepth 1 \( -name "*.wic" -type f -o -name "*.wic" -type l \) | grep -q .; then
    echo "Error: No .wic file found in ${IMAGE_SOURCE}" >&2
    exit 1
fi

if ! find "${IMAGE_SOURCE}" -maxdepth 1 \( -name "*vmlinux" -type f -o -name "*vmlinux" -type l \) | grep -q .; then
    echo "Error: No vmlinux file found in ${IMAGE_SOURCE}" >&2
    exit 1
fi

if [[ ! -d "${IMAGE_TARGET}" ]]; then
    echo "Error: Image target location is not a directory: ${IMAGE_TARGET}" >&2
    exit 1
fi

for src in "${DEPLOY_SRCS[@]}"; do
    if [[ ! -f "${src}" ]]; then
        echo "Error: Deploy source file does not exist: ${src}" >&2
        exit 1
    fi
    if [[ ! "${src}" =~ \.tar(\.(gz|bz2|xz))?$ ]]; then
        echo "Error: Deploy source is not a tar file: ${src}" >&2
        exit 1
    fi
done

mkdir -p "${IMAGE_TARGET}"
rm -f "${IMAGE_TARGET:?}/"*
cp -L "${IMAGE_SOURCE}"/* "${IMAGE_TARGET}"/

IMAGE=$(find "${IMAGE_TARGET}" -maxdepth 1 \( -name "*.wic" -type f -o -name "*.wic" -type l \) -print -quit)
KERNEL=$(find "${IMAGE_TARGET}" -maxdepth 1 \( -name "*vmlinux" -type f -o -name "*vmlinux" -type l \) -print -quit)

# The image file must be writable for qemu overlay handling.
chmod +w "${IMAGE}"

qemu-system-aarch64 -m 2048 -cpu cortex-a53 -machine virt \
    -kernel "${KERNEL}" \
    -machine "virt,virtualization=true,gic-version=3" \
    -smp 8 \
    -device virtio-blk-device,drive=vd0 -drive if=none,format=raw,file="${IMAGE}",id=vd0 \
    -append "root=/dev/vda1 sdk_enable lisa_syscall_whitelist=2026 rw sharedmem.enable_sharedmem=0 init=/usr/bin/ebclfsa-cflinit" \
    -netdev user,id=net0,net=192.168.7.0/24,dhcpstart=192.168.7.2,dns=192.168.7.3,host=192.168.7.5,hostfwd=tcp::2222-:22,hostfwd=tcp::3333-:3333 -device virtio-net-device,netdev=net0 \
    -pidfile "${WORKING_DIR}/qemu.pid" \
    -nographic \
     > "${WORKING_DIR}/qemu_deployment.log" \
    &

for _ in {1..60}; do
    if sshpass -p linux ssh -o StrictHostKeyChecking=no -o ConnectTimeout=1 -p 2222 root@localhost true 2>/dev/null; then
        break
    fi
    sleep 1
done

for src in "${DEPLOY_SRCS[@]}"; do
    sshpass -p linux scp -rp -o StrictHostKeyChecking=no -P 2222 "${src}" root@localhost:/
    sshpass -p linux ssh -o StrictHostKeyChecking=no -p 2222 root@localhost "tar -xf /$(basename "${src}") -C /"
    sshpass -p linux ssh -o StrictHostKeyChecking=no -p 2222 root@localhost "rm -f /$(basename "${src}")"
done

sshpass -p linux ssh -o StrictHostKeyChecking=no -p 2222 root@localhost "printf 'PermitRootLogin yes\nPermitEmptyPasswords yes\nPasswordAuthentication yes\n' > /etc/ssh/sshd_config.d/99-integration-test.conf"
sshpass -p linux ssh -o StrictHostKeyChecking=no -p 2222 root@localhost "sed -i 's/^root:[^:]*:/root::/' /etc/shadow && sync && crinit-ctl poweroff" || true

if [[ -f "${WORKING_DIR}/qemu.pid" ]]; then
    wait "$(cat "${WORKING_DIR}/qemu.pid")" 2>/dev/null || true
    rm -f "${WORKING_DIR}/qemu.pid"
fi
