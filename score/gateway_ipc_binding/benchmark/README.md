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

The applications exercise the `mw::com` (LoLa) implementation of the gateway IPC
binding. A SOCom provider on the client side sends an event through a
`GenericSkeleton` and `GenericProxy` to a SOCom consumer on the server side.
Both peers run in one process, with asynchronous LoLa notification delivery.

- `gateway_ipc_binding_benchmark`: Google Benchmark latency measurements at
  64 B, 256 B, 1 KiB, and 1 MiB payload capacities.
- `gateway_ipc_binding_memory`: a dedicated 1 MiB memory-profiling application,
  defaulting to 1,000,000 events. Use `--iterations=N` for shorter instrumented runs.

The legacy transport's cached shared-memory-manager lookup benchmark is no longer
part of this suite because that cache is not used by the `mw::com` binding.

## Build and Validate

Run commands from the repository root. Release optimizations and disabled
sanitizers are required for representative profiles:

```bash
bazel test -c opt --features=-tsan \
  //score/gateway_ipc_binding/benchmark:all --test_output=errors

mkdir -p .llm_tmp/gateway_ipc_binding_profile
```

This builds both applications and runs the deployment-schema check and the
transmission test at all four payload sizes. If your local Bazel configuration
enables other sanitizers, disable them as well.

The applications explicitly initialize `mw::com` using the bundled
`mw_com_config.json`, resolving it from Bazel runfiles when available or from the
repository root for direct execution. The deployment reserves 64 event slots and
one subscriber, with dedicated service IDs 6450 and 6451. Run only one benchmark,
test, or profiler using this deployment at a time on a host; concurrent runs can
interfere with service discovery and shared memory.

## Latency Baseline

```bash
./bazel-bin/score/gateway_ipc_binding/benchmark/gateway_ipc_binding_benchmark \
  --benchmark_min_time=0.5s \
  --benchmark_repetitions=3 \
  --benchmark_out=.llm_tmp/gateway_ipc_binding_profile/benchmark.json \
  --benchmark_out_format=json \
  > .llm_tmp/gateway_ipc_binding_profile/benchmark.log 2>&1
```

Manual timing starts immediately before `update_event()` and ends in the receiving
SOCom callback. Allocation is outside this interval. One event is in flight at a
time. Only the 8-byte sequence word is written and read; the remaining payload
capacity is not filled or inspected. Consequently the reported byte rate is a
logical zero-copy capacity rate, **not measured payload-copy bandwidth**.

## Memory Profiles

GNU memusage, memusagestat, Valgrind, and ms_print must be installed. Run the
profilers sequentially:

```bash
/usr/bin/memusage \
  -d .llm_tmp/gateway_ipc_binding_profile/memusage.data \
  -p .llm_tmp/gateway_ipc_binding_profile/memusage.png \
  ./bazel-bin/score/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory \
  > .llm_tmp/gateway_ipc_binding_profile/memusage.log 2>&1

valgrind --tool=massif --time-unit=ms \
  --massif-out-file=.llm_tmp/gateway_ipc_binding_profile/massif.out \
  ./bazel-bin/score/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory \
  --iterations=10000 \
  > .llm_tmp/gateway_ipc_binding_profile/massif.log 2>&1

ms_print .llm_tmp/gateway_ipc_binding_profile/massif.out \
  > .llm_tmp/gateway_ipc_binding_profile/massif.txt
```

The default Massif profile describes heap allocations, not shared-memory mappings
or thread-stack reservations. LoLa also has a mapped event data pool of roughly
64 MiB for this deployment. Do not interpret heap peak as total process memory.
Neither memusage nor Massif establishes that the application is leak-free; use
Memcheck for a separate leak investigation.

## CPU Profiles

When a working `perf` binary and host sampling permissions are available:

```bash
for payload in 64 256 1024 1048576; do
  perf record -e cpu-clock -F 999 -g --call-graph dwarf,16384 \
    -o .llm_tmp/gateway_ipc_binding_profile/perf_event_c2s_${payload}.data \
    -- ./bazel-bin/score/gateway_ipc_binding/benchmark/gateway_ipc_binding_benchmark \
    --benchmark_filter="^benchmark_event_transmission_client_to_server/${payload}/" \
    --benchmark_min_time=5s \
    > .llm_tmp/gateway_ipc_binding_profile/perf_event_c2s_${payload}_run.log 2>&1
done

perf record -e cpu-clock -F 999 -g --call-graph dwarf,16384 \
  -o .llm_tmp/gateway_ipc_binding_profile/perf_memory_1048576.data \
  -- ./bazel-bin/score/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory \
  > .llm_tmp/gateway_ipc_binding_profile/perf_memory_1048576_run.log 2>&1
```

The trailing slash in each filter avoids accidentally selecting the 1 MiB case
when profiling 1 KiB. `cpu-clock` samples on-CPU execution across the process's
threads, including userspace and kernel execution; it does not measure off-CPU
waiting time. Setup, benchmark calibration, and teardown are included.

To generate interactive SVG flamegraphs and self-cost reports:

```bash
git clone --depth 1 https://github.com/brendangregg/FlameGraph.git .llm_tmp/FlameGraph
set -o pipefail

for name in perf_event_c2s_64 perf_event_c2s_256 perf_event_c2s_1024 \
            perf_event_c2s_1048576 perf_memory_1048576; do
  profile=".llm_tmp/gateway_ipc_binding_profile/${name}"
  perf report -i "${profile}.data" --stdio --no-children --no-call-graph \
    --percent-limit=1 --sort comm,dso,symbol > "${profile}_report.txt"
  perf script -i "${profile}.data" -F comm,pid,tid,time,event,ip,sym,dso | \
    perl .llm_tmp/FlameGraph/stackcollapse-perf.pl > "${profile}.folded"
  perl .llm_tmp/FlameGraph/flamegraph.pl \
    --title "mw::com CPU profile: ${name}" --countname samples \
    "${profile}.folded" > "${profile}_flamegraph.svg"
done
```

The explicit `perf script` fields omit `period`, so each stack contributes one
sample. Otherwise `cpu-clock` periods are treated as weights, and labeling them
as sample counts would be incorrect. Flamegraph width represents inclusive stack
sample share, while the text reports show self costs; these are different views
of the same recording. Open an SVG in a browser to zoom and search the stacks.

For a single-case run, for example:

```bash
perf record -e cpu-clock -F 999 -g --call-graph dwarf,16384 \
  -o .llm_tmp/gateway_ipc_binding_profile/perf_event_c2s_1024.data \
  -- ./bazel-bin/score/gateway_ipc_binding/benchmark/gateway_ipc_binding_benchmark \
  --benchmark_filter='benchmark_event_transmission_client_to_server/1024/' \
  --benchmark_min_time=5s
```

If kernel sampling is unavailable, collect portable CPU instruction costs with
Callgrind instead:

```bash
valgrind --tool=callgrind \
  --callgrind-out-file=.llm_tmp/gateway_ipc_binding_profile/callgrind.out \
  ./bazel-bin/score/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory \
  --iterations=100000 \
  > .llm_tmp/gateway_ipc_binding_profile/callgrind.log 2>&1

callgrind_annotate --auto=no --inclusive=no --threshold=95 \
  .llm_tmp/gateway_ipc_binding_profile/callgrind.out \
  > .llm_tmp/gateway_ipc_binding_profile/callgrind_self.txt
```

Callgrind reports executed userspace instructions, not wall time, kernel time, or
native CPU samples. These profiles include startup and teardown; use the separate
Google Benchmark run for uninstrumented latency.
