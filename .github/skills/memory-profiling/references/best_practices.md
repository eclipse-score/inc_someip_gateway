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

# Memory Profiling Best Practices

## Pre-Profiling Checklist

- [ ] **Build configuration verified**: `-c opt --features=-tsan` (enforced by scripts)
- [ ] **Target is ready to build**: `bazel build <target>` passes
- [ ] **No debug symbols expected**: Release builds strip symbols by default
- [ ] **System resources available**: Task has 2+ GB free RAM for Valgrind overhead
- [ ] **Valgrind/memusage installed**: `which valgrind && which memusage`

## Choosing the Right Tool

### Use Valgrind Massif When:

✓ Need precise, per-allocation tracking
✓ Investigating memory leaks (allocation source)
✓ Analyzing heap fragmentation
✓ Detailed timeline of memory events
✓ Can tolerate 10x slowdown

**Example**: Debugging unexpected memory peak in long-running service

### Use GNU memusage When:

✓ Want quick overview with minimal overhead
✓ Benchmarking memory footprint
✓ Integrating into CI/CD pipelines
✓ Profiling over long execution (hours)
✓ System resources are constrained

**Example**: Verifying memory improvement across releases

## Running Profiling Correctly

### 1. Baseline Measurement First

Before optimizing, capture baseline metrics:

```bash
# Baseline: current state
./run_massif_profile.sh "//target:target" > baseline.txt 2>&1
# → baseline_massif.out, baseline_summary.txt

# After optimization
./run_massif_profile.sh "//target:target" > optimized.txt 2>&1
# → optimized_massif.out

# Compare
ms_print baseline_massif.out | grep "100.00%"
ms_print optimized_massif.out | grep "100.00%"
```

### 2. Isolate Variables

Profile single workload, not mixed scenarios:

```bash
# ✓ Good: Profile one benchmark filter
./run_massif_profile.sh "//target:benchmark" \
  --benchmark_filter='read_only_lookup'

# ✗ Bad: Mix multiple benchmarks
./run_massif_profile.sh "//target:benchmark"  # All filters
```

### 3. Repeat for Confidence

Memory behavior varies; take multiple samples:

```bash
for i in {1..3}; do
  ./run_massif_profile.sh "//target:target" \
    --benchmark_min_time=1.0s
  mv massif.out massif.out.run$i
done

# Average results
ms_print massif.out.run1 | grep "100.00%" | awk '{print $3}'
ms_print massif.out.run2 | grep "100.00%" | awk '{print $3}'
ms_print massif.out.run3 | grep "100.00%" | awk '{print $3}'
```

---

## Analyzing and Reporting Results

### Standard Report Format

```markdown
## Memory Profile: [Component Name]

**Tool**: Valgrind Massif / GNU memusage
**Date**: YYYY-MM-DD
**Configuration**: -c opt --features=-tsan
**System**: [CPU, RAM, Linux version]

### Metrics

| Metric | Value | Target | Status |
|--------|-------|--------|--------|
| Peak Memory | 191 KB | < 500 KB | ✓ Pass |
| Useful / Total | 96.3% | > 90% | ✓ Pass |
| Allocations | 1,234 | — | — |

### Top Allocators (Top 3)

1. `buffer_init()` → 102 KB (53%)
2. `message_queue::new()` → 54 KB (28%)
3. `socket_bind()` → 35 KB (18%)

### Timeline

- **Startup**: 0–50 ms, 15 KB (initialization)
- **Steady-state**: 50 ms–end, 191 KB peak (stable)
- **Shutdown**: Clean (no leaks)

### Observations

- Excellent fragmentation (96% efficiency)
- Memory stable after initialization
- No growth pattern over time
- Allocation sources expected

### Recommendations

- ✓ No optimization needed at this time
```

### Quick Summary Command

```bash
ms_print massif.out | awk '
  /^  [0-9]+ / {
    print $1 " " $3 " bytes @ " $2
  }
  /100.00%/ {
    print "PEAK: " $3
  }
' | tail -5
```

---

## Common Pitfalls and How to Avoid Them

### Pitfall 1: Debug Build Profiling

**Problem**: Debug builds include symbols and instrumentation, inflating memory.

```bash
# ✗ Wrong: Default is debug
bazel build //target

# ✓ Right: Explicit release
bazel build -c opt --features=-tsan //target
```

**Impact**: Memory can be 2–5x higher in debug builds.

---

### Pitfall 2: Mixed Workloads

**Problem**: Running all benchmarks combines different memory behaviors.

```bash
# ✗ Wrong: Measures entire suite
./run_massif_profile.sh "//benchmark:all_benchmarks"

# ✓ Right: Profile one benchmark
./run_massif_profile.sh "//benchmark:all_benchmarks" \
  --benchmark_filter='cached_read_only_lookup'
```

---

### Pitfall 3: Ignoring Sanitizers

**Problem**: ASAN/TSAN add significant overhead and change memory layout.

```bash
# ✗ Wrong: Sanitizers enabled (default in some configs)
bazel build //target

# ✓ Right: Explicitly disable
bazel build -c opt --features=-tsan --features=-asan //target
```

**Scripts enforce this, but verify**:
```bash
ldd bazel-bin/target | grep -i "asan\|tsan"  # Should be empty
```

---

### Pitfall 4: Misinterpreting Transient Spikes

**Problem**: Initialization or cache-filling can cause brief memory spikes.

```bash
# Check if spike is sustained or transient
ms_print massif.out | grep "^        n " | head -10
ms_print massif.out | grep "^        n " | tail -10
```

If tail is much smaller: Spike was transient (initialization), not a leak.

---

## Integration with CI/CD

### Example: GitHub Actions

```yaml
- name: Baseline Memory Profile
  run: |
    ./.github/skills/memory-profiling/scripts/run_memusage_profile.sh \
      "//src/gateway_ipc_binding/benchmark:gateway_ipc_binding_benchmark"
    cp memusage.out baseline_memusage.txt

- name: Report
  run: |
    echo "## Memory Profile" >> $GITHUB_STEP_SUMMARY
    cat baseline_memusage.txt >> $GITHUB_STEP_SUMMARY
```

### Example: GitLab CI

```yaml
memory_profile:
  script:
    - ./.github/skills/memory-profiling/scripts/run_memusage_profile.sh
      "//target:target"
  artifacts:
    paths:
      - memusage.out
      - memusage.data
    reports:
      performance: memusage.out
```

---

## References

- [Valgrind Massif Advanced Usage](https://valgrind.org/docs/manual/ms-manual.html#ms-command-line-options)
- [Creating Custom Allocators](../../../src/gateway_ipc_binding/doc/)
- [Project Profiling Results](../../../memory_profile/PROFILING_REPORT.md)
