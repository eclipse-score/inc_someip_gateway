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

# Memory Profiling Report: gateway_ipc_binding_memory

## Build Configuration
- **Build Type**: Release (-c opt)
- **Compilation Optimizations**: Enabled
- **Thread Sanitizer**: Disabled (removed -tsan feature)
- **Binary**: `gateway_ipc_binding_memory` (dedicated memory profiling application)
- **Binary Path**: `bazel-bin/src/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory`
- **Binary Size**: ~23 MB
- **Bazel Version**: 8.4.1
- **Build Date**: 2026-04-27

## Profiling Setup
- **Tools**: GNU memusage + Valgrind Massif 3.22.0
- **Workload**: 1,000,000 iterations, 1 MB payload
- **Test Duration**: ~5.7 seconds (memusage), ~53 seconds (Massif)
- **Profiling Data**: `memusage.data`, `memusage.png`, `massif.out.1`

## Memory Usage Summary

### GNU memusage Statistics
- **Total Heap Allocated**: 176,398,085 bytes (~168 MB cumulative over 1M iterations)
- **Peak Heap Usage**: 178,362 bytes (~174 KB)
- **Peak Stack Usage**: 10,960 bytes (~11 KB)

### Allocation Operations
- **Total malloc calls**: 1,005,137
- **Total realloc calls**: 0
- **Total calloc calls**: 3
- **Total free calls**: 1,005,141
- **Failed allocations**: 0

### Valgrind Massif Statistics
- **Peak Heap Usage (useful)**: 176,746 bytes (~173 KB)
- **Peak Heap Usage (total with overhead)**: 180,008 bytes (~176 KB)
- **Number of Snapshots**: 78
- **Heap Profile Shape**: Flat/stable across entire run (no growth)

### Allocation Pattern (Histogram)
Dominant allocation sizes:
- **176-191 bytes**: 1,000,080 blocks (99%) - Most prevalent
- **16-31 bytes**: 1,937 blocks (<1%)
- **64-79 bytes**: 1,889 blocks (<1%)
- **32-47 bytes**: 868 blocks (<1%)

## Execution Performance
(Dedicated memory profiling application, 1 MB payload, 1M iterations)

### Event Transmission (1 MB payload)
- **Total benchmark time**: 3,572 ms
- **Total execution time**: 5,657 ms (including setup/teardown)
- **Latency per iteration**: ~3.6 µs
- **Iterations**: 1,000,000

### Per-Iteration Allocation Cost
- **malloc calls per iteration**: ~1.0
- **Bytes allocated per iteration**: ~176 bytes
- **Net heap growth per iteration**: 0 (all freed)

## Massif Top Allocators (at peak snapshot #10)

| Allocator | Size | % of Peak |
|-----------|------|-----------|
| `ClientConnection::ClientConnection` (vector reserve) | 81,920 B | 48.2% |
| `ConsoleRecorderFactory::CreateConsoleLoggingBackend` (logging) | 24,792 B | 14.6% |
| `LogRecord` circular allocator (logging buffer) | 16,384 B | 9.6% |
| Other (154 places below threshold) | 24,370 B | 14.3% |
| Allocator overhead (extra-heap) | 3,262 B | 1.8% |

## Key Findings

1. **Stable Memory Profile**: Peak heap is ~173-174 KB useful bytes, perfectly flat across 1M iterations — no growth
2. **Peak Memory**: 173 KB peak (Massif useful-heap), 176 KB total with overhead
3. **Allocation Pattern**: Single dominant allocation size (176-191 bytes) accounts for 99% of all 1M allocations — one allocation per iteration
4. **No Leaks**: Perfect balance — 1,005,141 free calls vs 1,005,137 malloc + 3 calloc calls (1 extra free is normal cleanup)
5. **Reduced Allocation Count**: ~1M total malloc calls (down from ~6M in previous run), indicating allocation path optimization
6. **Dominant Allocator**: `ClientConnection` buffer reserve accounts for 48% of peak heap (one-time setup cost)
7. **Logging Overhead**: Console logging backend accounts for ~24% of peak heap (24.8 KB + 16.4 KB)

## Comparison with Previous Run (2026-04-22)

| Metric | Previous (Apr 22) | Current (Apr 27) | Change |
|--------|-------------------|-------------------|--------|
| Total malloc calls | 6,021,543 | 1,005,137 | -83% |
| Total heap allocated | 283 MB | 168 MB | -41% |
| Peak heap (memusage) | 172 KB | 174 KB | +1% |
| Peak heap (Massif useful) | 172 KB | 173 KB | +1% |
| Benchmark latency | 3.9 µs | 3.6 µs | -8% |
| Total execution time | 6.2 s | 5.7 s | -9% |
| Binary size | 18 MB | 23 MB | +28% |
| Bazel version | 8.0.0 | 8.4.1 | upgraded |
| Dominant block size | 32-47 B (49%) | 176-191 B (99%) | changed |

**Notable**: Allocation count dropped by 83% (6M → 1M) while peak heap remained essentially unchanged. This indicates that the per-iteration allocation path has been consolidated from ~6 small allocations to ~1 allocation of 176-191 bytes, reducing allocator overhead significantly. Latency improved by ~8%.

## Recommendations

- **Light Memory Footprint**: Peak at 173 KB useful heap — excellent for embedded/real-time systems
- **Flat Allocation Profile**: Zero heap growth over 1M iterations confirms no leaks and stable memory
- **Allocation Consolidation**: The shift to a single 176-191 byte allocation per iteration is an improvement over the previous 6 small allocations per iteration
- **Logging Overhead**: ~24% of peak heap is logging infrastructure; consider disabling in production or using a lighter backend
- **ClientConnection Buffer**: 80 KB reserve (48% of peak) is one-time setup cost, acceptable for long-running services

## Output Files

- `memusage.data`: Raw profiling data file
- `memusage.png`: Memory usage timeline graph
- `massif.out.1`: Valgrind Massif heap profile
- `benchmark_run.log`: memusage execution log
- `massif_run.log`: Massif execution log
- `PROFILING_REPORT.md`: This report
