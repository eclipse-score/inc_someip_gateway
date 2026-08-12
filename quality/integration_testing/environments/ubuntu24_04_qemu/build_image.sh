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

set -euxo pipefail

if [[ $# -ne 7 ]]; then
    echo "Error: Expected 7 arguments (working directory, source image, target image, cloud-init user-data, cloud-init meta-data, qemu config, user-data renderer)" >&2
    exit 1
fi

WORKING_DIR="$1"
IMAGE_SOURCE="$2"
IMAGE_TARGET="$3"
USER_DATA="$4"
META_DATA="$5"
QEMU_CONFIG="$6"
USER_DATA_RENDERER="$7"

IMAGE_SIZE="8G"
BOOT_TIMEOUT_SECONDS=1200

for tool in qemu-system-x86_64 qemu-img cloud-localds python3; do
    if ! command -v "${tool}" &>/dev/null; then
        echo "Error: ${tool} is not installed. Please install it to proceed." >&2
        exit 1
    fi
done

if [[ ! -f "${IMAGE_SOURCE}" ]]; then
    echo "Error: Image source is not a file: ${IMAGE_SOURCE}" >&2
    exit 1
fi

for f in "${USER_DATA}" "${META_DATA}"; do
    if [[ ! -f "${f}" ]]; then
        echo "Error: cloud-init file does not exist: ${f}" >&2
        exit 1
    fi
done

if [[ ! -f "${QEMU_CONFIG}" ]]; then
    echo "Error: QEMU config does not exist: ${QEMU_CONFIG}" >&2
    exit 1
fi

if [[ ! -f "${USER_DATA_RENDERER}" ]]; then
    echo "Error: user-data renderer does not exist: ${USER_DATA_RENDERER}" >&2
    exit 1
fi

mkdir -p "$(dirname "${IMAGE_TARGET}")"
rm -f "${IMAGE_TARGET}"
cp -L "${IMAGE_SOURCE}" "${IMAGE_TARGET}"

chmod +w "${IMAGE_TARGET}"
qemu-img resize "${IMAGE_TARGET}" "${IMAGE_SIZE}"

SEED_IMAGE="${WORKING_DIR}/score-itf-seed.img"
USER_DATA_RENDERED="${WORKING_DIR}/score-itf-user-data.yaml"
rm -f "${SEED_IMAGE}"

python3 "${USER_DATA_RENDERER}" "${QEMU_CONFIG}" "${USER_DATA}" "${USER_DATA_RENDERED}"

cloud-localds "${SEED_IMAGE}" "${USER_DATA_RENDERED}" "${META_DATA}"

QEMU_LOG="${WORKING_DIR}/qemu_customization.log"

timeout "${BOOT_TIMEOUT_SECONDS}" qemu-system-x86_64 \
    -accel kvm -accel tcg \
    -machine pc \
    -cpu Cascadelake-Server-v5 \
    -smp 2 \
    -m 2048 \
    -no-reboot \
    -device virtio-blk-pci,drive=vd0 -drive if=none,format=qcow2,file="${IMAGE_TARGET}",id=vd0 \
    -device virtio-blk-pci,drive=vd1 -drive if=none,format=raw,file="${SEED_IMAGE}",id=vd1,readonly=on \
    -netdev user,id=net0 \
    -device virtio-net-pci,netdev=net0 \
    -nographic \
    -serial mon:stdio </dev/null >"${QEMU_LOG}" 2>&1

rm -f "${SEED_IMAGE}"
rm -f "${USER_DATA_RENDERED}"

if ! grep -q "score ITF image customization finished" "${QEMU_LOG}"; then
    echo "Error: cloud-init did not complete the image customization." >&2
    echo "----- QEMU log -----" >&2
    cat "${QEMU_LOG}" >&2
    exit 1
fi
