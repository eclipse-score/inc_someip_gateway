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
- **Test Duration**: ~5.4 seconds (memusage), ~48 seconds (Massif)
- **Profiling Data**: `memusage.data`, `memusage.png`, `massif.out.1`

## Memory Usage Summary

### GNU memusage Statistics
- **Total Heap Allocated**: 201,941 bytes (~197 KB cumulative — all during setup)
- **Peak Heap Usage**: 176,034 bytes (~172 KB)
- **Peak Stack Usage**: 10,960 bytes (~11 KB)

### Allocation Operations
- **Total malloc calls**: 373
- **Total realloc calls**: 0
- **Total calloc calls**: 3
- **Total free calls**: 377
- **Failed allocations**: 0

### Valgrind Massif Statistics
- **Peak Heap Usage (useful)**: 176,002 bytes (~172 KB)
- **Peak Heap Usage (total with overhead)**: 178,800 bytes (~175 KB)
- **Number of Snapshots**: 65
- **Heap Profile Shape**: Flat/stable across entire run (no growth)

### Allocation Pattern (Histogram)
All 373 allocations occur during setup; diverse sizes across initialization:
- **32-47 bytes**: 73 blocks (19%)
- **16-31 bytes**: 56 blocks (14%)
- **48-63 bytes**: 40 blocks (10%)
- **0-15 bytes**: 35 blocks (9%)
- **4096-4111 bytes**: 24 blocks (6%)
- **112-127 bytes**: 17 blocks (4%)
- **240-255 bytes**: 16 blocks (4%)
- Other sizes: 112 blocks (30%)

## Execution Performance
(Dedicated memory profiling application, 1 MB payload, 1M iterations)

### Event Transmission (1 MB payload)
- **Total benchmark time**: 3,475 ms
- **Total execution time**: 5,435 ms (including setup/teardown)
- **Latency per iteration**: ~3.5 µs
- **Iterations**: 1,000,000

### Per-Iteration Allocation Cost
- **malloc calls per iteration**: 0
- **Bytes allocated per iteration**: 0
- **Net heap growth per iteration**: 0

## Massif Top Allocators (at peak snapshot #40)

| Allocator | Size | % of Peak |
|-----------|------|-----------|
| `ClientConnection::ClientConnection` (vector reserve) | 81,920 B | 45.8% |
| `ConsoleRecorderFactory::CreateConsoleLoggingBackend` (logging) | 24,792 B | 13.9% |
| `LogRecord` circular allocator (logging buffer) | 16,384 B | 9.2% |
| Other (153 places below threshold) | 23,794 B | 13.3% |
| Allocator overhead (extra-heap) | 2,798 B | 1.6% |

## Key Findings

1. **Allocation-Free Hot Path**: Zero heap allocations during 1M iterations — all 373 mallocs occur during initialization only
2. **Stable Memory Profile**: Peak heap is ~172 KB useful bytes, perfectly flat across 1M iterations — no growth
3. **Peak Memory**: 172 KB peak (Massif useful-heap), 175 KB total with overhead
4. **No Leaks**: Perfect balance — 377 free calls vs 373 malloc + 3 calloc calls (1 extra free is normal cleanup)
5. **Dramatic Allocation Reduction**: 373 total malloc calls (down from 1M in previous run, 6M two runs ago) — per-iteration allocations completely eliminated
6. **Dominant Allocator**: `ClientConnection` buffer reserve accounts for 46% of peak heap (one-time setup cost)
7. **Logging Overhead**: Console logging backend accounts for ~23% of peak heap (24.8 KB + 16.4 KB)

## Comparison with Previous Runs

| Metric | Apr 22 | Apr 27 (prev) | Current | Change (vs prev) |
|--------|--------|---------------|---------|------------------|
| Total malloc calls | 6,021,543 | 1,005,137 | 373 | -99.96% |
| Total heap allocated | 283 MB | 168 MB | 197 KB | -99.9% |
| Peak heap (memusage) | 172 KB | 174 KB | 172 KB | -1% |
| Peak heap (Massif useful) | 172 KB | 173 KB | 172 KB | -1% |
| Benchmark latency | 3.9 µs | 3.6 µs | 3.5 µs | -3% |
| Total execution time | 6.2 s | 5.7 s | 5.4 s | -5% |
| Binary size | 18 MB | 23 MB | 23 MB | 0% |
| Bazel version | 8.0.0 | 8.4.1 | 8.4.1 | same |
| malloc calls/iteration | ~6 | ~1 | 0 | -100% |

**Notable**: Per-iteration heap allocations have been completely eliminated. The 373 total malloc calls (all during initialization) compare to 1M in the previous run and 6M two runs ago. Cumulative heap allocation dropped from 168 MB to 197 KB — a 99.9% reduction. This confirms the event transmission hot path is now fully allocation-free.

## Recommendations

- **Allocation-Free Hot Path**: Zero per-iteration allocations confirms real-time suitability — no allocator jitter during event transmission
- **Light Memory Footprint**: Peak at 172 KB useful heap — excellent for embedded/real-time systems
- **Flat Allocation Profile**: Zero heap growth over 1M iterations confirms no leaks and stable memory
- **Logging Overhead**: ~23% of peak heap is logging infrastructure; consider disabling in production or using a lighter backend
- **ClientConnection Buffer**: 80 KB reserve (46% of peak) is one-time setup cost, acceptable for long-running services

## Output Files

- `memusage.data`: Raw profiling data file
- `memusage.png`: Memory usage timeline graph
- `massif.out.1`: Valgrind Massif heap profile
- `benchmark_run.log`: memusage execution log
- `massif_run.log`: Massif execution log
- `PROFILING_REPORT.md`: This report
