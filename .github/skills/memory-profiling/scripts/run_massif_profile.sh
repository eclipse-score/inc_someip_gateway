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
# run_massif_profile.sh - Valgrind Massif profiling wrapper
#
# Usage: run_massif_profile.sh <bazel_target> [benchmark_args...]
#
# This script:
# 1. Builds the target with release optimizations (-c opt) and no sanitizers
# 2. Runs Valgrind Massif with correct flags to capture memory usage
# 3. Outputs massif.out for analysis
#
# Example:
#   ./run_massif_profile.sh "//src/gateway_ipc_binding/benchmark:gateway_ipc_binding_benchmark" \
#     --benchmark_filter='cached_read_only' --benchmark_min_time=0.01s

set -euo pipefail

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
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

# Verify arguments
[[ $# -ge 1 ]] || die "Usage: $0 <bazel_target> [benchmark_args...]"

BAZEL_TARGET="$1"
shift || true

# Validate Valgrind is available
command -v valgrind >/dev/null 2>&1 || die "valgrind not found. Install: apt install valgrind-tools"

# Step 1: Build with release optimizations, no sanitizers
info "Building ${BAZEL_TARGET} with -c opt --features=-tsan..."
bazel build -c opt --features=-tsan "${BAZEL_TARGET}" || die "Bazel build failed"

# Step 2: Find the binary path
# Bazel builds to bazel-bin/path/to/target
BINARY_PATH="bazel-bin/${BAZEL_TARGET#//#/}"
BINARY_PATH="${BINARY_PATH//:/\/}"

[[ -x "${BINARY_PATH}" ]] || die "Binary not found at ${BINARY_PATH} or not executable"

info "Binary ready: ${BINARY_PATH}"

# Step 3: Run Valgrind Massif
# Key flags:
#  --tool=massif : heap profiler
#  --trace-children=yes : needed for programs that fork/re-exec
#  --massif-out-file : specify output location
OUTPUT_FILE="massif.out"

info "Running Valgrind Massif..."
info "Output will be written to: ${OUTPUT_FILE}"

valgrind \
    --tool=massif \
    --trace-children=yes \
    --massif-out-file="${OUTPUT_FILE}" \
    "${BINARY_PATH}" "$@"

EXIT_CODE=$?

# Step 4: Verify output
massif_outputs=("${OUTPUT_FILE}"*)
if [[ -e "${massif_outputs[0]}" ]]; then
    success "Massif output created:"
    for output in "${massif_outputs[@]}"; do
        output_size=$(stat -c %s "${output}")
        echo "  ${output} (${output_size} bytes)"
    done
    success "Visualize with: ms_print ${OUTPUT_FILE}"
    success "More info: ms_print ${OUTPUT_FILE} | head -100"
else
    die "Massif output file not created despite successful execution"
fi

exit "${EXIT_CODE}"
