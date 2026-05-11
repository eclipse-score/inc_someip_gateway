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

# Understanding Memory Profiling Output

## Valgrind Massif (Heap Profiler)

### What Massif Measures

Massif tracks **heap allocations** over time:
- **Peak memory**: Highest total allocated memory
- **Allocation patterns**: Which functions allocate most memory
- **Memory timeline**: How memory usage changes during execution
- **Fragmentation**: Unused space in allocations

### Reading `ms_print` Output

```
================================================================================
    Allocs in group 1: 1,234 allocs, 5.2 MB allocated, 0 BP

        n        time(B)         total(B)   useful-heap(B) extra-heap(B)    stacks(B)
        1     108,901,234       5,242,880         5,200,000        42,880           0
        2     109,012,345       4,856,320         4,800,000        56,320           0
  100.00%  5,242,880 (peak)  5,200,000 total heap

  n_allocators
    n  place                                               total(B)   useful-B  extra-B
    1  0x...: malloc (vg_replace_malloc.c:...)            1,024,000  1,000,000  24,000
    2  0x...: operator new (...)                            512,000    500,000  12,000
```

**Key columns**:
- `time(B)`: Bytes allocated at this point
- `total(B)`: Total heap size (includes fragmentation)
- `useful-heap(B)`: Actually usable memory
- `extra-heap(B)`: Metadata + fragmentation overhead

**Interpretation**:
- `useful-heap` ÷ `total` = **allocation efficiency**. If → 80%, consider fragmentation concerns
- Top allocators: Functions contributing most to peak memory

### Example Analysis

```bash
# Get summary
ms_print massif.out | head -50

# Get top N allocators (detailed)
ms_print massif.out | grep -A 20 "^  1  0x"

# Find peak memory point
ms_print massif.out | grep "100.00%"
```

**Peak 191 KB example** (from gateway_ipc_binding benchmark):
```
  100.00%  191,976 (peak)  186,880 total heap
```
→ 191 KB peak, 96.3% efficiency (low fragmentation)

---

## GNU memusage (Lightweight Tracker)

### What memusage Measures

memusage tracks:
- **Resident Set Size (RSS)**: Physical memory used
- **Virtual memory**: Allocated but not yet physical
- **Heap growth**: When heap grows during execution
- **Allocation timeline**: Memory usage as program runs

### Reading `memusage.out` Output

```
Memory usage summary: heap total: 1024, heap peak: 512, stack peak: 256
        Calls to malloc: 1234
        Calls to realloc: 12
        Calls to free: 1100
        Calls to memalign: 0
        Calls to posix_memalign: 0

        Total memory allocated: 5242880
        Total memory freed: 5050368
        Total memory in use: 192512
```

**Key metrics**:
- `heap peak`: Maximum heap size reached
- `Total memory allocated`: Sum of all allocations (may be > peak if freed)
- `Total memory in use`: Currently allocated (= peak for inactive phases)

### Comparing Massif vs memusage

| Aspect | Massif | memusage |
|--------|--------|----------|
| **Overhead** | ~10x slowdown, high memory | ~5-20% slowdown, low memory |
| **Precision** | Exact per-allocation tracking | Statistical estimates |
| **Best for** | Detailed profiling, heap snapshots | Quick overview, CI/CD |
| **Output** | Binary + text snapshots | Text summary + timeline |
| **Visualization** | ms_print (automatic) | gnuplot (if available) |

---

## Interpreting Benchmark Memory Results

### Gateway IPC Binding Benchmark

From [PROFILING_REPORT.md](../../../memory_profile/PROFILING_REPORT.md):

**Measured** (release build, Massif):
- **Peak**: 191 KB (minimal)
- **Allocated**: ~186 KB
- **Efficiency**: 96.3% (excellent)

**Interpretation**:
- ✅ Tiny footprint (comparable to embedded systems)
- ✅ No memory leaks (freed on exit)
- ✅ Efficient buffering (low fragmentation)

### What Could Indicate Problems

| Signal | Investigation |
|--------|----------------|
| Peak >>> Expected | Temporary allocations not freed promptly; check for leak patterns |
| useful-heap << total-heap | High fragmentation; consider custom allocator |
| Allocations grow linearly over time | Likely memory leak; use Memcheck to confirm |
| Spike at startup then stable | Expected for initialization; not a leak |

---

## Optimization Strategies

### 1. Reduce Peak Usage

**Identify** largest allocations from `ms_print`:
```bash
ms_print massif.out | grep -A 5 "1  0x"
```

**Optimize**:
- Defer allocation until needed
- Free unused buffers earlier
- Use stack allocation for small, fixed-size objects

### 2. Reduce Allocation Rate

**Identify** hot allocation paths:
```bash
valgrind --tool=callgrind ./binary  # Profile call graph
callgrind_annotate callgrind.out     # Show allocation hotspots
```

**Optimize**:
- Use object pools for frequently allocated objects
- Batch allocations
- Cache heap blocks

### 3. Detect Fragmentation

**Check** useful-heap vs total-heap ratio:
```bash
ms_print massif.out | grep "100.00%"
```

If < 85%:
- Use jemalloc or tcmalloc for better fragmentation behavior
- Consider arena-based allocators for specific workloads

---

## Troubleshooting

### "massif.out too small / no snapshots"

**Problem**: Binary ran too fast; Massif had no time to sample.
**Solution**: Increase iteration count or benchmark time:
```bash
./run_massif_profile.sh "//target:target" \
  --benchmark_min_time=1.0s \
  --benchmark_samples=100
```

### "Peak looks artificially high"

**Problem**: Includes temporary initialization allocations.
**Solution**: Check heap snapshot at execution midpoint:
```bash
ms_print massif.out | grep "^        n " | tail -5
```

### "memusage output missing"

**Problem**: Binary crashed or exited abnormally.
**Solution**: Check stderr and verify execution:
```bash
./binary 2>&1 | head -20
```

---

## References

- [Valgrind Massif Manual](https://valgrind.org/docs/manual/ms-manual.html)
- [GNU memusage docs](https://manpages.ubuntu.com/manpages/jammy/man1/memusage.1.html)
- [Memory Profiling Best Practices](./best_practices.md)
