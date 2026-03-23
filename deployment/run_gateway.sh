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
#
# Launch script for the S-CORE SOME/IP Gateway.
#
# Start each daemon in a separate terminal, in this order:
#   1. ./run_gateway.sh gatewayd
#   2. ./run_gateway.sh someipd
#
# someipd requires VSOMEIP_CONFIGURATION to point to a vsomeip JSON config:
#   export VSOMEIP_CONFIGURATION=/path/to/vsomeip.json
#   ./run_gateway.sh someipd

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

usage() {
    cat <<EOF
S-CORE SOME/IP Gateway
======================

Usage: $0 {gatewayd|someipd}

  $0 gatewayd   -- ASIL-B bridge daemon (must start first)
  $0 someipd    -- SOME/IP stack daemon (start after gatewayd)

Set VSOMEIP_CONFIGURATION to your vsomeip config before running someipd.
EOF
    exit 1
}

[ "$#" -lt 1 ] && usage

case "$1" in
    gatewayd)
        echo "Starting gatewayd..."
        "${SCRIPT_DIR}/gatewayd" \
            --service_instance_manifest "${SCRIPT_DIR}/gatewayd_mw_com_config.json" \
            --gatewayd_config "${SCRIPT_DIR}/gatewayd_config.bin"
        ;;
    someipd)
        echo "Starting someipd..."
        # Locate vsomeip shared libraries inside the Bazel runfiles tree and
        # add them to LD_LIBRARY_PATH so the dynamic linker finds them.
        VSOMEIP_LIB_DIR=$(find "${SCRIPT_DIR}/someipd.runfiles" \
            -name "libvsomeip3.so*" -exec dirname {} \; 2>/dev/null | head -1)
        if [ -n "${VSOMEIP_LIB_DIR}" ]; then
            export LD_LIBRARY_PATH="${VSOMEIP_LIB_DIR}${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
        fi
        "${SCRIPT_DIR}/someipd" \
            --service_instance_manifest "${SCRIPT_DIR}/someipd_mw_com_config.json"
        ;;
    *)
        usage
        ;;
esac
