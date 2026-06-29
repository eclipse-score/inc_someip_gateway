<!--
*******************************************************************************
Copyright (c) 2026 Contributors to the Eclipse Foundation

See the NOTICE file(s) distributed with this work for additional
information regarding copyright ownership.

This program and the accompanying materials are made available under the
terms of the Apache License Version 2.0 which is available at
https://www.apache.org/licenses/LICENSE-2.0

SPDX-License-Identifier: Apache-2.0
*******************************************************************************
-->

# Profiling Guide for gateway_ipc_binding

This guide provides instructions to regenerate memory and performance profiling data for the gateway IPC binding benchmark applications.

## Overview

Two benchmark applications are available:

- **`gateway_ipc_binding_benchmark`**: Google Benchmark suite with multiple test cases (performance profiling with perf)
- **`gateway_ipc_binding_memory`**: Dedicated memory profiling application running 1,000,000 iterations at 1 MB payload (memory profiling with memusage and Massif)

## Prerequisites

Ensure these tools are installed on your system:

```bash
# Memory profiling tools
sudo apt-get install valgrind linux-tools-common

# Performance profiling
sudo apt-get install linux-tools-generic

# Flamegraph generation (optional)
git clone https://github.com/brendangregg/FlameGraph.git
export PATH=$PATH:/path/to/FlameGraph
```

## Memory Profiling

Memory profiling uses the dedicated `gateway_ipc_binding_memory` application, which avoids fork/reexec issues that interfere with Valgrind Massif when using Google Benchmark.

### Build

```bash
cd /workspaces/score_inc_someip_gateway
bazel build //src/gateway_ipc_binding/benchmark:gateway_ipc_binding_memory -c opt --features=-tsan
```

**Note**: The `-c opt --features=-tsan` flags disable ThreadSanitizer instrumentation to get representative release build profiling.

### Run GNU memusage

Creates timeline graph and memory statistics:

```bash
# Create output directory
mkdir -p ../memory_profile

# Run memusage
/usr/bin/memusage \
  -d ../memory_profile/memusage.data \
  -p ../memory_profile/memusage.png \
  ./bazel-bin/src/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory \
  2>&1 | tee ../memory_profile/benchmark_run.log
```

**Output**:
- `memusage.data` - Binary profiling data
- `memusage.png` - Memory usage timeline graph
- `benchmark_run.log` - Execution output with statistics

### Run Valgrind Massif

Detailed heap profiler with full call-tree attribution:

```bash
# Run Massif (generates massif.out.1)
valgrind --tool=massif \
  --massif-out-file=../memory_profile/massif.out.1 \
  ./bazel-bin/src/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory \
  2>&1 | tee ../memory_profile/massif_run.log

# View profile (first 80 lines)
ms_print ../memory_profile/massif.out.1 | head -80

# Full profile
ms_print ../memory_profile/massif.out.1 | less
```

**Output**:
- `massif.out.1` - Valgrind Massif heap profile
- `massif_run.log` - Valgrind output log

### Memory Profiling Results

Expected results for a healthy build:

| Metric | Expected Value |
|--------|--------|
| Peak heap (memusage) | ~172 KB |
| Peak heap (Massif useful) | ~172 KB |
| Peak stack | ~11 KB |
| Total malloc calls | ~373 |
| Allocation pattern | All during initialization (zero per-iteration) |
| Heap growth across iterations | None (flat profile) |
| Memory leaks | None |

## Performance Profiling

Performance profiling uses the `gateway_ipc_binding_benchmark` Google Benchmark suite with perf for CPU flamegraphs.

### Build

```bash
cd /workspaces/score_inc_someip_gateway
bazel build //src/gateway_ipc_binding/benchmark:gateway_ipc_binding_benchmark -c opt --features=-tsan
```

### Run Benchmark

First, run the benchmark to establish a baseline and identify which test case to profile:

```bash
./bazel-bin/src/gateway_ipc_binding/benchmark/gateway_ipc_binding_benchmark \
  --benchmark_filter=benchmark_event_transmission_client_to_server \
  --benchmark_format=console \
  2>&1 | tee benchmark_results.log
```

This will show output similar to:

```
benchmark_event_transmission_client_to_server/1024/manual_time
                                    4382 ns         4210 ns       139426 iterations  222.851 MiB/s
```

### Collect CPU Profile with perf

Create profiling output directory and collect performance data:

```bash
# Create output directory
mkdir -p ../profiling

# Run with perf recording
perf record -F 999 -g --call-graph dwarf,16384 -o ../profiling/perf_event_c2s_1024.data \
  ./bazel-bin/src/gateway_ipc_binding/benchmark/gateway_ipc_binding_benchmark \
  --benchmark_filter=benchmark_event_transmission_client_to_server/1024 \
  --benchmark_time_unit=ns \
  2>&1 | tee ../profiling/benchmark_run.log
```

**Output**:
- `perf_event_c2s_1024.data` - Raw perf data (binary format)
- `benchmark_run.log` - Benchmark execution output

### Analyze perf Results

#### View report in perf:

```bash
cd ../profiling
perf report -i perf_event_c2s_1024.data
```

#### Generate folded stacks (for flamegraph):

```bash
perf script -i perf_event_c2s_1024.data | \
  stackcollapse-perf.pl > perf_event_c2s_1024.folded
```

#### Generate flamegraph SVG (requires FlameGraph repository):

```bash
flamegraph.pl perf_event_c2s_1024.folded > event_transmission_client_to_server_1024_flamegraph.svg

# View in browser
open event_transmission_client_to_server_1024_flamegraph.svg
```

#### Generate percent summary:

```bash
perf report -i perf_event_c2s_1024.data --stdio --no-call-graph > perf_event_c2s_1024_percent_summary.txt
```

### Performance Profiling Interpretation

Key metrics to look for:

| Component | Typical % | Notes |
|-----------|-----------|-------|
| Syscall/kernel time | ~74% | Normal for IPC workload |
| Poll overhead | ~26% | Poll loop waiting |
| recv operations | ~31% | Message reception |
| send operations | ~9% | Message transmission |
| Mutex/lock contention | ~10% | Synchronization overhead |
| Shared memory management | ~5% | Allocation lifecycle |

## Complete Profiling Workflow

To regenerate all profiling data in one workflow:

```bash
#!/bin/bash
set -e

cd /workspaces/score_inc_someip_gateway

# Build both targets
echo "Building memory profiling app..."
bazel build //src/gateway_ipc_binding/benchmark:gateway_ipc_binding_memory -c opt --features=-tsan

echo "Building benchmark suite..."
bazel build //src/gateway_ipc_binding/benchmark:gateway_ipc_binding_benchmark -c opt --features=-tsan

# Memory profiling
echo "Running memory profiling..."
mkdir -p src/gateway_ipc_binding/memory_profile
/usr/bin/memusage \
  -d src/gateway_ipc_binding/memory_profile/memusage.data \
  -p src/gateway_ipc_binding/memory_profile/memusage.png \
  ./bazel-bin/src/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory \
  2>&1 | tee src/gateway_ipc_binding/memory_profile/benchmark_run.log

echo "Running Massif profiling..."
valgrind --tool=massif \
  --massif-out-file=src/gateway_ipc_binding/memory_profile/massif.out.1 \
  ./bazel-bin/src/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory \
  2>&1 | tee src/gateway_ipc_binding/memory_profile/massif_run.log

# Performance profiling
echo "Running performance profiling..."
mkdir -p src/gateway_ipc_binding/profiling
perf record -F 999 -g --call-graph dwarf,16384 -o src/gateway_ipc_binding/profiling/perf_event_c2s_1024.data \
  ./bazel-bin/src/gateway_ipc_binding/benchmark/gateway_ipc_binding_benchmark \
  --benchmark_filter=benchmark_event_transmission_client_to_server/1024 \
  --benchmark_time_unit=ns \
  2>&1 | tee src/gateway_ipc_binding/profiling/benchmark_run.log

# Generate reports
cd src/gateway_ipc_binding/profiling
perf script -i perf_event_c2s_1024.data | stackcollapse-perf.pl > perf_event_c2s_1024.folded
flamegraph.pl perf_event_c2s_1024.folded > event_transmission_client_to_server_1024_flamegraph.svg
perf report -i perf_event_c2s_1024.data --stdio --no-call-graph > perf_event_c2s_1024_percent_summary.txt

echo "Profiling complete!"
```

---

**Last updated: 2026-06-29**
