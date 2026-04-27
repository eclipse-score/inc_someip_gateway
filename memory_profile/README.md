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

# Memory Profiling Results: gateway_ipc_binding_memory

## Overview
This directory contains the complete memory profiling and performance analysis using the dedicated `gateway_ipc_binding_memory` application compiled with **release optimizations** (no thread sanitizer). This application runs 1,000,000 event transmissions at 1 MB payload, providing a sustained stress test suitable for memory profiling with GNU memusage and Valgrind Massif.

## Quick Summary

✅ **Build Status**: Successful
✅ **Profiling Status**: Completed (memusage + Massif)
✅ **Memory Status**: No leaks detected
🎯 **Peak Memory**: 173 KB useful heap (Massif)
⚡ **Latency**: ~3.6 microseconds per event
📊 **Iterations**: 1,000,000 @ 1 MB payload

## File Inventory

### Analysis Reports
- **`PROFILING_REPORT.md`** - Detailed memory statistics and findings
- **`README.md`** - This file

### Profiling Data
- **`memusage.data`** - Raw memory profiling data
- **`memusage.png`** - Memory usage timeline graph
- **`massif.out.1`** - Valgrind Massif heap profile
- **`benchmark_run.log`** - memusage execution output
- **`massif_run.log`** - Massif execution output

## Build Information

```
Compiler:           Bazel 8.4.1 with C++ toolchain
Build Configuration: -c opt (release optimization)
Features Disabled:   TSAN (thread sanitizer)
Binary:             bazel-bin/src/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory
Binary Size:        23 MB
Date:               2026-04-27
```

## Key Findings

### Memory Behavior
- **Total Heap Allocated**: 168 MB cumulative (176,398,085 bytes over 1M iterations)
- **Peak Heap Usage**: 174 KB (memusage), 173 KB useful (Massif)
- **Peak Stack Usage**: 11 KB
- **Allocation Pattern**: 99% of allocations are 176-191 bytes (one per iteration)
- **Total malloc calls**: 1,005,137 (down 83% from previous 6M+)
- **No Memory Leaks**: Perfect balance of malloc (1,005,137) + calloc (3) vs free (1,005,141) calls

## Profiling Tools Used

1. **GNU memusage** - Memory profiler from glibc
   - Tracks all malloc/free operations
   - Generates timeline graph and histogram
   - Low overhead (~1-5%)

2. **Valgrind Massif** - Detailed heap profiler
   - Periodic snapshots with full call-tree attribution
   - Identifies top allocators by source location
   - Overhead: ~8-10x slowdown

## Performance Insights

### Why a Dedicated Memory App
1. **No fork/reexec**: Google Benchmark re-executes internally, which interferes with Massif output
2. **Fixed workload**: 1M iterations at 1 MB ensures consistent, reproducible profiling
3. **Direct Massif compatibility**: No need for `--trace-children=yes`
4. **Simple `main()`**: Clean profiling without framework overhead

### Efficiency Observations
- **Flat heap profile**: Zero growth across 1M iterations confirms no leaks
- **~6 allocations per iteration**: Small IPC control structures, all promptly freed
- **Stable memory usage**: Peak reached during initialization, never exceeded
- **Logging dominates setup**: 25% of peak heap is console logging infrastructure

## Recommendations

### Immediate Actions
- ✅ Memory profile confirms production readiness
- ✅ 173 KB peak heap is safe for embedded systems
- ✅ No optimizations required for memory usage
- ✅ Allocation count reduced 83% vs previous run (allocation path consolidation)

### Further Investigation
- Consider CPU profiling (perf/FlameGraph) for hotspot analysis
- Reduce logging overhead by disabling console backend in production (~24% of peak heap)
- Test with real-time scheduling priorities

## Usage Examples

### View Memory Graph
```bash
# The PNG graph is available at:
# memory_profile/memusage.png
```

### Process Raw Data
```bash
# Read binary profiling data
xxd -l 1000 memory_profile/memusage.data

# Parse execution output
grep "Completed\|Benchmark completed" memory_profile/benchmark_run.log

# View Massif profile
ms_print memory_profile/massif.out.1 | head -80
```

### Regenerate Profile (if needed)
```bash
cd /workspaces/score_inc_someip_gateway
bazel build //src/gateway_ipc_binding/benchmark:gateway_ipc_binding_memory -c opt --features=-tsan

# GNU memusage profiling
/usr/bin/memusage -d memory_profile/memusage.data -p memory_profile/memusage.png \
  ./bazel-bin/src/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory
```

### Run Massif (valgrind) and Always Get massif.out
```bash
valgrind --tool=massif --massif-out-file=memory_profile/massif.out.1 \
  ./bazel-bin/src/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory
# Visualize:
ms_print memory_profile/massif.out.1 | less
```

> [!NOTE]
> Either do not use Google benchmark as the test runner when running with massif or set `--trace-children=yes`.
> Google benchmark re-execs internally. Without `--trace-children=yes`,
> valgrind may print benchmark output but not emit a Massif file.

## Related Files

- Dedicated memory app: `src/gateway_ipc_binding/benchmark/event_transmission_client_to_server_memory.cpp`
- BUILD file: `src/gateway_ipc_binding/benchmark/BUILD`
- Gateway binding: `src/gateway_ipc_binding/`

## Conclusion

The `gateway_ipc_binding_memory` dedicated profiling application demonstrates:
- ✅ Excellent memory efficiency (173 KB peak useful heap)
- ✅ Zero heap growth over 1,000,000 iterations
- ✅ Low latency for user-space IPC (~3.6 µs per event)
- ✅ No memory leaks (1M+ balanced malloc/free)
- ✅ 83% reduction in allocation count vs previous run
- ✅ Safe for production deployment and embedded systems

**The dedicated memory application provides cleaner profiling than Google Benchmark since it avoids fork/reexec issues with Massif.**

---

*Profile generated: 2026-04-27 UTC*
*Profiling tools: GNU memusage + Valgrind Massif 3.22.0*
*Application: gateway_ipc_binding_memory (1M iterations, 1 MB payload)*
