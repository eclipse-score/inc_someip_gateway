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

## Recorded Profile: 2026-10-15

Collected in the Ubuntu 24.04 dev container on an x86-64 Intel Core i7-12700H
(20 logical CPUs), using `-c opt --features=-tsan`. Google Benchmark 1.9.5 reports
a release build. CPU scaling was enabled and the host was not isolated, so these
values are observations, not performance acceptance thresholds. Three repetitions
were collected with a 0.5-second minimum benchmark time.

| Payload Capacity | Median Send-to-Callback Latency | Latency CV |
|------------------|--------------------------------|------------|
| 64 B | 4.074 us | 8.9% |
| 256 B | 4.586 us | 6.3% |
| 1 KiB | 3.922 us | 12.3% |
| 1 MiB | 3.415 us | 11.0% |

The median is across repetition averages, not a per-event percentile.

| Memory Metric | Observation |
|---------------|-------------|
| memusage events | 1,000,000 at 1 MiB capacity |
| memusage peak heap | 67,371,705 B (64.25 MiB) |
| memusage stack estimate | 138,304 B (135.1 KiB); not aggregate thread-stack memory |
| malloc calls | 7,064,750; no failed calls |
| calloc calls | 9; no failed calls |
| Total allocated bytes | 1,355,938,558 B |
| Instrumented loop elapsed time | 8.368 s |
| Massif events | 10,000 at 1 MiB capacity |
| Massif peak useful heap | 67,370,725 B |
| Massif peak allocator overhead | 9,387 B |

Massif attributes 67,109,376 B of the peak to `EventDataStorage` during
`SkeletonMemoryManager::CalculateShmResourceStorageSizesBySimulation`: LoLa sizes
the shared-memory pool using temporary heap storage. The old transport's roughly
172 KiB peak is therefore not an applicable expectation.

The allocation histogram includes approximately one allocation per event in each
of the 64-79, 96-111, 128-143, 208-223, and 464-479 byte buckets, plus two in the
144-159 byte bucket. This implementation does **not** have zero per-event heap
allocation. Reducing these allocations is a separate optimization task.

The 100,000-event Callgrind run recorded 1,781,504,076 userspace instructions.
Selected self costs were:

| Function | Instruction Share |
|----------|-------------------|
| Consumer slot status / `ReferenceNextEvent` | 7.21% |
| `pthread_mutex_lock` | 5.59% |
| Provider `AllocateNextSlot` | 4.52% |
| `pthread_mutex_unlock` | 4.17% |
| `_int_malloc` | 3.68% |
| Consumer `ReferenceNextEvent` implementation | 3.51% |
| `_int_free` | 2.46% |

Several inlined parts of the same operation appear separately, so these rows are
not complete subsystem totals.

### Native perf and Flamegraphs

After installing the matching Linux tools, `perf` 7.0.14 successfully recorded
all four benchmark payload sizes and the 1,000,000-event memory workload. These
additional runs used 999 Hz `cpu-clock` sampling and 16 KiB DWARF stack captures.
FlameGraph revision `41fee1f99f9276008b7cd112fca19dc3ea84ac32` generated the SVGs.

| Workload | Folded Samples | Stacks With Unknown Frames | Flamegraph Filename |
|----------|----------------|----------------------------|---------------------|
| 64 B benchmark | 9,793 | 0.00% | `perf_event_c2s_64_flamegraph.svg` |
| 256 B benchmark | 17,975 | 7.85% | `perf_event_c2s_256_flamegraph.svg` |
| 1 KiB benchmark | 10,881 | 3.51% | `perf_event_c2s_1024_flamegraph.svg` |
| 1 MiB benchmark | 18,839 | 1.53% | `perf_event_c2s_1048576_flamegraph.svg` |
| 1 MiB memory workload | 7,569 | 5.01% | `perf_memory_1048576_flamegraph.svg` |

All recordings reported **zero lost samples**, and stack conversion and SVG
generation completed without warnings. Some stacks contain unresolved frames,
so attribution is not complete even though the samples themselves were retained.

For the 1 KiB benchmark, the native self-cost report shows:

| Symbol | On-CPU Sample Share |
|--------|---------------------|
| `finish_task_switch.isra.0` | 36.43% |
| `_raw_spin_unlock_irqrestore` | 5.58% |
| `pthread_mutex_lock` | 4.23% |
| `_int_malloc` | 3.50% |
| `do_syscall_64` | 2.87% |

Scheduler and synchronization overhead is prominent in this one-event-at-a-time
workload. These percentages are sampled CPU costs, not percentages of end-to-end
latency, and are not directly comparable to Callgrind instruction shares.

Raw JSON, logs, the memusage graph, Massif data, Callgrind data, perf recordings,
folded stacks, self-cost reports, and interactive SVG flamegraphs were generated
under `.llm_tmp/gateway_ipc_binding_profile/`. That directory is ephemeral and is
not committed; the commands above regenerate the artifacts. All collected runs
completed successfully. The console logging fallback warning is expected when no
logging configuration is supplied.

## Per-Event Allocation Attribution: 2026-10-16

Fresh release profiles used `-c opt --features=-tsan`, `score_communication`
0.5.0, and `score_baselibs` 0.2.15 on the same x86-64 dev container. GNU memusage
runs at 10,000 and 20,000 events established count scaling; Valgrind DHAT runs at
both counts identified allocation stacks, including allocations too short-lived
to dominate a Massif snapshot. All workloads ran sequentially and completed.

### Reproduce the Allocation Stacks

After building the memory application as above:

```bash
mkdir -p .llm_tmp/gateway_ipc_binding_allocations
for count in 10000 20000; do
  memusage -T \
    ./bazel-bin/score/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory \
    --iterations="${count}" \
    > ".llm_tmp/gateway_ipc_binding_allocations/memusage_${count}.log" 2>&1
  valgrind --tool=dhat --num-callers=40 \
    --dhat-out-file=".llm_tmp/gateway_ipc_binding_allocations/dhat_${count}.json" \
    ./bazel-bin/score/gateway_ipc_binding/benchmark/gateway_ipc_binding_memory \
    --iterations="${count}" \
    > ".llm_tmp/gateway_ipc_binding_allocations/dhat_${count}.log" 2>&1
done
```

Load either JSON file in Valgrind's `dh_view.html` viewer (the path is printed
in the log), and sort by total blocks, not peak bytes. With `jq`, recurring sites
and their complete stacks can also be listed directly:

```bash
jq '. as $d | .pps[] | select(.tbk >= 625) |
    {blocks: .tbk, bytes: .tb, stack: [.fs[] | $d.ftbl[.]]}' \
  .llm_tmp/gateway_ipc_binding_allocations/dhat_10000.json
```

### Results by Stage

| Stage | Allocation Stack / Owner | Requested Bytes per Allocation | Blocks at 10k / 20k Events |
|-------|--------------------------|--------------------------------|---------------------------|
| Allocate adapter | `Provider_service_binding::Allocations::insert` -> `unordered_map::emplace` node | 128 | 10,000 / 20,000 |
| Send notification | `NotifyEvent` -> `Executor::Post` -> `SimpleTaskFactory::Make` -> `InterruptibleState<void>::Make` | 464 | 10,000 / 20,000 |
| Send notification | Same promise construction -> `safecpp::Scope` -> shared `ScopeState` | 208 | 10,000 / 20,000 |
| Send notification | `SimpleTaskFactory::InternalMake` -> PMR `make_unique<SimpleTask>` | 72 | 10,000 / 20,000 |
| Send notification | `SimpleTaskBase` constructor -> `stop_source` -> shared stop state | 144 | 10,000 / 20,000 |
| Notification worker cleanup | `ThreadPool::Work` -> assignment of a default-constructed `stop_source` | 144 | 10,000 / 20,000 |
| Notification queue | `ThreadPool::Enqueue` -> `deque::emplace_back` -> deque node allocation | 512 | 625 / 1,250 |

These sites account for **7 allocations plus 1/16 of a queue-block allocation,
and 1,288 requested heap bytes per event**, averaged over these runs. Allocator
metadata is additional. The queue keeps allocating even with one event in flight:
a sliding `deque` releases old blocks and allocates new ones; it is not a reusable
ring buffer. Other allocation sites are startup/teardown costs in these profiles.

The allocation path is
`allocate_event_payload` -> `Provider_service_binding::on_event_payload_allocate`
-> `GenericSkeletonEvent::Allocate` -> LoLa `SkeletonEvent::Allocate`.
LoLa claims an existing shared-memory slot with `AllocateNextSlot`; no recurring
heap allocation was attributed to that operation. The subsequent gateway map
insertion allocates the 128-byte node. Reserving hash buckets alone will not
remove node allocations.

Transmission follows `update_event` -> `Provider_service_binding::on_event_update`
-> `GenericSkeletonEvent::Send` -> LoLa `SkeletonEvent::Send` ->
`MessagePassingServiceInstance::NotifyEvent`. The local notification's
`executor_.Post(...)` constructs a promise even though `Post` returns no future,
and creates a task and its cancellation state. After executing the task, the
worker constructs another stop state merely to clear its active-task entry.
Together with queue blocks, notification infrastructure accounts for 1,064 of
the 1,288 bytes per event, about 83% of recurring requested bytes.

These results describe the benchmark's **same-process local notification path**.
Two separate gateway processes take the remote message-passing path; its
steady-state allocation behavior must be measured separately. The five task and
state allocations must not be assumed to occur in every remote deployment.
There is no evidence here of a per-event 1 MiB payload-buffer allocation or copy.

The fresh default 1,000,000-event memusage run reported 7,064,750 `malloc` calls,
9 `calloc` calls, no failed allocations, 1,355,940,912 total allocated bytes,
and a 67,371,705-byte peak heap. Its instrumented loop took 9.073 seconds.
A fresh 10,000-event Massif run reported 67,370,725 useful peak bytes and
9,403 allocator-overhead bytes. As in the earlier profile, roughly 64 MiB of the
peak is startup shared-memory-layout simulation, not live per-event metadata.
Heap peaks do not include the mapped LoLa data pool or prove absence of leaks.
Raw profiles, logs, the memusage graph, and the Massif text report are under
`.llm_tmp/gateway_ipc_binding_allocations/` and are ephemeral.

### Recommended Changes

1. **Replace provider map-node churn with bounded reusable storage.** Allocate
   sample-holder entries and a free list when setting up the skeleton, sized from
   the deployed provider slot limits across its events (64 for this benchmark),
   not merely the consumer's subscription limit. Use index-plus-generation
   handles so a late payload destructor cannot free a reused entry, including
   after skeleton recreation. Keep the existing weak lifetime guard, teardown
   ordering, and normal backpressure on exhaustion. This removes one allocation
   and 128 requested bytes per event in this build. `unordered_map::reserve` is
   insufficient; a bounded recyclable node allocator is another option.

2. **Make local notifications allocation-free upstream.** In `score_communication`,
   retain asynchronous dispatch but use preallocated notification work, for
   example a bounded ready-event queue with per-event pending state and a
   dedicated worker. Coalescing must recheck/drain samples without losing a
   notification racing with pending-state clearing. Preserve receive-handler
   unregistration synchronization and shutdown behavior. Calling handlers inline
   changes latency, reentrancy, and lock-order semantics and is not an equivalent
   optimization. In `score_baselibs`, a genuine fire-and-forget task should not
   construct unused promise/Scope state; cancellation state and task storage also
   need bounded reuse. Supplying a PMR resource only for `SimpleTask` and the
   queue does not cover the `make_shared` state allocations in these stacks.

3. **Remove worker stop-state and queue churn upstream.** Clear an idle worker's
   active-task state without default-constructing a fresh allocating stop source,
   preserving the shutdown/cancellation race protection. Replace the sliding
   deque with bounded reusable queue storage or a bounded recycling allocator.
   Queue saturation must have defined backpressure or coalescing behavior, not
   silently drop notifications. Combined with the preceding task changes, this
   targets the remaining five allocations and 32 amortized queue bytes per event.

No runtime optimization is applied by this report. The adapter changes and the
dependency changes need separate lifetime/concurrency tests. Verification should
count allocations only after connection, subscription, and warmup, on both sending
and receiving threads, then repeat the 10k/20k DHAT comparison. Cover dropped
unsent payloads, retained received payloads, slot/queue exhaustion, concurrent
events, disconnect/reconnect, and teardown. Finally profile separate provider
and consumer processes; eliminating allocations in this local benchmark alone
does not establish an allocation-free IPC deployment.
