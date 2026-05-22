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
#
# run_memusage_profile.sh - GNU memusage profiling wrapper
#
# Usage: run_memusage_profile.sh <bazel_target> [program_args...]
#
# This script:
# 1. Builds the target with release optimizations (-c opt) and no sanitizers
# 2. Runs GNU memusage for lightweight memory profiling
# 3. Outputs memusage.data and optionally memusage.png
#
# Example:
#   ./run_memusage_profile.sh "//src/gateway_ipc_binding/benchmark:gateway_ipc_binding_benchmark"

set -euo pipefail

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

die() {
    echo -e "${RED}ERROR: $*${NC}" >&2
    exit 1
}

info() {
    echo -e "${BLUE}INFO: $*${NC}"
}

success() {
    echo -e "${GREEN}SUCCESS: $*${NC}"
}

warn() {
    echo -e "${YELLOW}WARN: $*${NC}"
}

# Verify arguments
[[ $# -ge 1 ]] || die "Usage: $0 <bazel_target> [program_args...]"

BAZEL_TARGET="$1"
shift || true

# Validate memusage is available
command -v memusage >/dev/null 2>&1 || die "memusage not found. Install: apt install libc-bin"

# Step 1: Build with release optimizations, no sanitizers
info "Building ${BAZEL_TARGET} with -c opt --features=-tsan..."
bazel build -c opt --features=-tsan "${BAZEL_TARGET}" || die "Bazel build failed"

# Step 2: Find the binary path
BINARY_PATH="bazel-bin/${BAZEL_TARGET#//#/}"
BINARY_PATH="${BINARY_PATH//:/\/}"

[[ -x "${BINARY_PATH}" ]] || die "Binary not found at ${BINARY_PATH} or not executable"

info "Binary ready: ${BINARY_PATH}"

# Step 3: Run memusage
# memusage outputs to memusage.data by default
info "Running GNU memusage..."
info "Output will be written to: memusage.data and memusage.out"

memusage "${BINARY_PATH}" "$@" || true

# Step 4: Check for gnuplot to generate graph
if command -v gnuplot >/dev/null 2>&1; then
    info "Generating PNG graph with gnuplot..."
    if [[ -f memusage.data ]]; then
        memusage -p memusage.png >/dev/null 2>&1 || warn "Failed to generate PNG (gnuplot may not be configured)"
    fi
fi

# Step 5: Verify outputs
if [[ -f memusage.out ]]; then
    success "memusage output created:"
    memusage_out_size=$(stat -c %s memusage.out)
    echo "  memusage.out (${memusage_out_size} bytes)"
else
    die "memusage output file not created"
fi

if [[ -f memusage.data ]]; then
    memusage_data_lines=$(wc -l < memusage.data)
    success "memusage data: memusage.data (${memusage_data_lines} lines)"
fi
[[ -f memusage.png ]] && success "memusage graph: memusage.png"

success "View summary: cat memusage.out"
success "View timeline: memusage -p memusage.png"
