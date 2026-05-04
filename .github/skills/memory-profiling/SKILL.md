---
name: memory-profiling
description: 'Memory profiling for C++ targets with release optimizations and no sanitizers. Use to profile heap usage, memory peaks, and performance with Valgrind Massif or GNU memusage.'
argument-hint: 'Target path (e.g., //src/gateway_ipc_binding/benchmark:gateway_ipc_binding_benchmark)'
---

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

# Memory Profiling

## When to Use

- Profile heap allocations and memory peaks
- Determine memory footprint of a component
- Analyze memory usage patterns (fragmentation, leaks)
- Validate memory efficiency after optimizations
- Benchmark memory performance (throughput, latency)

## Prerequisites

Ensure your system has the required tools installed:

```bash
# Valgrind Massif (heap profiler)
which valgrind

# GNU memusage (lightweight memory tracker)
which memusage

# Massif visualization
which ms_print
```

## Workflow

### 1. Identify Target and Build Configuration

Memory profiling requires **release-optimized binaries** with **no sanitizers** enabled. This ensures:
- Accurate performance metrics (no instrumentation overhead)
- No memory sanitizer interference
- Release-like behavior matches production

**Commands to build clean**:
```bash
# Build with -c opt (release), disable tsan
bazel build -c opt --features=-tsan //path/to:target

# Or use provided helper: see Section 3
```

### 2. Choose a Profiling Tool

| Tool | Best For | Output | Command |
|------|----------|--------|---------|
| **Valgrind Massif** | Precise heap snapshots, detailed analysis | Binary + text report | [See script](./scripts/run_massif_profile.sh) |
| **GNU memusage** | Quick overview, timeline view | Graph + text report | [See script](./scripts/run_memusage_profile.sh) |

### 3. Run Profiling

#### Option A: Valgrind Massif (Detailed)

Use the [Massif wrapper script](./scripts/run_massif_profile.sh):

```bash
# Profile a benchmark with custom flags
./.github/skills/memory-profiling/scripts/run_massif_profile.sh \
  "//src/gateway_ipc_binding/benchmark:gateway_ipc_binding_benchmark" \
  --benchmark_filter='benchmark_cached_read_only_lookup/32' \
  --benchmark_min_time=0.01s

# Output: massif.out (PID also creates massif.out.NNN)
```

**To visualize**:
```bash
ms_print massif.out | less
```

#### Option B: GNU memusage (Quick)

Use the [memusage wrapper script](./scripts/run_memusage_profile.sh):

```bash
# Profile a binary (lighter-weight)
./.github/skills/memory-profiling/scripts/run_memusage_profile.sh \
  "//src/gateway_ipc_binding/benchmark:gateway_ipc_binding_benchmark"

# Output: memusage.data, memusage.png (if gnuplot installed)
```

### 4. Interpret Results

See [INTERPRETATION.md](./references/INTERPRETATION.md) for understanding:
- Heap snapshots and memory peaks
- Allocation patterns and fragmentation
- Throughput impact of memory usage
- Common optimization opportunities

## Important Notes

**Release build required**: Both scripts enforce `-c opt --features=-tsan`. Do NOT run profiling with debug builds or sanitizers — results will be misleading.

**Valgrind Massif gotcha**: Programs that fork/exec need `--trace-children=yes` (the wrapper handles this automatically).

**Output location**: Both tools write results to the current working directory. Check for:
- `massif.out` (Massif binary)
- `memusage.data` (memusage binary)

## Troubleshooting

| Issue | Solution |
|-------|----------|
| `valgrind: command not found` | Install: `apt install valgrind-tools` |
| `memusage: command not found` | Install: `apt install libc-bin` (usually included) |
| No output file created | Ensure target completed successfully (check stderr) |
| Massif output too small | Increase workload: adjust benchmark filters or iteration counts |
| Out of memory during profiling | Reduce benchmark iteration counts or use memusage (lighter) instead |

## Example Workflow

```bash
# 1. Build release
bazel build -c opt --features=-tsan //src/gateway_ipc_binding/benchmark

# 2. Profile with Massif
./.github/skills/memory-profiling/scripts/run_massif_profile.sh \
  "//src/gateway_ipc_binding/benchmark:gateway_ipc_binding_benchmark"

# 3. Visualize
ms_print massif.out | head -100

# 4. Interpret (see INTERPRETATION.md)
```

## References

- [Understanding Massif Output](./references/INTERPRETATION.md)
- [Valgrind Massif Manual](https://valgrind.org/docs/manual/ms-manual.html)
- [GNU memusage Documentation](https://manpages.ubuntu.com/manpages/jammy/man1/memusage.1.html)

---

**Last updated**: 2026-04-17
**Related**: Project gateway_ipc_binding benchmarks, performance profiling workflow
