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

# benchmark_event_transmission_client_to_server profiling analysis

## Scope
- Benchmark target: `//src/gateway_ipc_binding/benchmark:gateway_ipc_binding_benchmark`
- Benchmark case profiled: `benchmark_event_transmission_client_to_server/1024/manual_time`
- Build mode used for final analysis: `-c opt --features=-tsan`
- Profiling command: `perf record -F 999 -g --call-graph dwarf,16384`

## Artifacts
- Flamegraph (final, non-TSan): `profiling/event_transmission_client_to_server_1024_notsan_flamegraph.svg`
- Perf data (final, non-TSan): `profiling/perf_event_c2s_1024_notsan_ok.data`
- Folded stacks: `profiling/perf_event_c2s_1024_notsan_ok.folded`
- Percent summary: `profiling/perf_event_c2s_1024_notsan_ok_percent_summary.txt`

## Benchmark result used for profile
- `benchmark_event_transmission_client_to_server/1024/manual_time`: **4382 ns** manual time, 4210 ns CPU time (139426 iterations, 222.851 MiB/s)
- Previous result: ~5228 ns manual time
- **Improvement: ~16.2% latency reduction**

## Important caveat
An initial profile was collected while ThreadSanitizer instrumentation was active due `user.bazelrc` containing `common --features=tsan`. That profile was dominated by `__tsan_*` internals and is not representative for release bottlenecks. Final analysis below uses the non-TSan profile.

## Bottlenecks (non-TSan)
Percentages below are cumulative stack presence from folded stacks and indicate where execution time is spent across call paths.

1. Syscall and scheduler heavy path
- `entry_SYSCALL_64_after_hwframe`: ~74.15%
- `do_syscall_64`: ~73.13%
- `x64_sys_call`: ~69.68%
- `schedule` + `__schedule`: ~30.38% combined cumulative presence
- Interpretation: the event transmission path remains dominated by kernel transitions, waiting/wakeup, and scheduling overhead rather than payload processing. Proportions are broadly similar to the previous profile.

2. Poll/recv/send loop overhead
- `score::os::SysPollImpl::poll`: ~26.22%
- `__GI___poll`: ~25.56%
- `score::os::SocketImpl::recvmsg`: ~30.74%
- `score::os::SocketImpl::sendmsg`: ~8.78%
- `do_sys_poll`: ~21.39%, `__x64_sys_recvmsg`: ~28.49%, `__x64_sys_sendmsg`: ~8.07%
- Interpretation: recv-side costs have increased relative to send-side — `recvmsg` now dominates over `sendmsg` more than before (30.7% vs 8.8%), suggesting the receive path is a larger proportion of the workload. Poll overhead remains a central cost.

3. Message handling and dispatch path
- `score::message_passing::detail::ClientConnection::ProcessInputEvent`: ~33.26%
- `score::message_passing::UnixDomainEngine::ReceiveProtocolMessage`: ~31.23%
- `score::message_passing::detail::UnixDomainServer::ServerConnection::ProcessInput`: ~13.95%
- `score::gateway_ipc_binding::Gateway_ipc_binding_base::handle_event_update_message`: ~7.08%
- `score::gateway_ipc_binding::Gateway_ipc_binding_base::handle_request_service_message`: ~7.63%
- `score::gateway_ipc_binding::Gateway_ipc_binding_base::handle_payload_consumed_message`: ~6.47%
- Interpretation: client-side input processing (`ProcessInputEvent` at 33.3%) now appears more prominently than server-side processing (`ProcessInput` at 14.0%). The `handle_event_update_message` cost dropped from ~11.9% to ~7.1%, indicating the event handling path itself has been optimized.

4. Shared memory management overhead (new finding)
- `score::gateway_ipc_binding::Shared_memory_managers::payload_consumed`: ~5.16%
- `unordered_map::erase` (shared memory allocation map): ~4.00%
- `Shared_memory_allocation::~Shared_memory_allocation`: ~3.64%
- `Read_only_shared_memory_payload::__on_zero_shared`: ~3.34%
- Interpretation: shared memory slot lifecycle management (allocation tracking via hash map, destruction of allocation entries, shared_ptr reference counting) contributes ~16% cumulative cost. This is a newly visible bottleneck now that other paths have improved.

5. Synchronization overhead
- `std::__1::mutex::lock`: ~4.99%
- `__GI___lll_lock_wait`: ~4.47%
- `std::__1::condition_variable::wait`: ~4.21%
- `do_futex` / `futex_wait`: ~10.49% / ~8.24%
- `std::__1::shared_ptr::~shared_ptr` / `__release_shared`: ~7.26% / ~7.04%
- Interpretation: mutex contention and futex-based waiting are significant. Shared pointer reference counting (`~shared_ptr` at 7.1%, `__release_shared` at 7.3%) is a notable cost, likely from per-event payload lifetime management.

6. Kernel-side micro-costs (leaf signals)
- `fdget`: ~2.63%
- `simple_copy_to_iter`: ~3.93%
- `copy_msghdr_from_user`: ~4.34%
- `consume_skb` / `kfree_skbmem` / `kmem_cache_free`: ~8.21% / ~3.87% / ~3.87%
- Interpretation: socket buffer allocation/deallocation and data copy operations in the kernel are a steady overhead. The `consume_skb` path (8.2%) is notable — each message send triggers SKB allocation and freeing.

## Improvement suggestions
Prioritized by expected impact and implementation risk.

1. Reduce syscall frequency per event (highest impact)
- Batch event updates where semantics allow.
- Coalesce notifications and transmit multiple updates in one message.
- Prefer fewer, larger transfers over many small transfer operations.
- Why: the profile remains syscall-bound (~74% in kernel transitions); fewer transitions directly lower end-to-end cost.

2. Optimize shared memory slot lifecycle (new recommendation)
- The `unordered_map::erase` + destructor path for `Shared_memory_allocation` costs ~16% combined.
- Consider a slot pool with index-based lookup instead of hash map insert/erase per event.
- Reuse allocation entries rather than creating and destroying them each time.
- Why: this is a newly prominent bottleneck after event handling optimizations.

3. Reduce shared_ptr reference counting overhead
- `shared_ptr` destruction and `__release_shared` together cost ~14%.
- Where possible, pass payloads by reference or use `unique_ptr` instead of `shared_ptr`.
- Consider intrusive reference counting for hot-path payload objects.
- Why: atomic reference count operations are costly at high message rates.

4. Replace or reduce poll wakeups
- If feasible in the transport layer, move from poll loop patterns toward readiness/eventfd integration with less wake-sleep churn.
- Tune poll timeout and wake strategy to avoid frequent short sleeps.
- Why: `poll` and scheduler frames are consistently dominant (~26% and ~30% respectively).

5. Reduce mutex contention in hot callbacks
- `mutex::lock` + `lll_lock_wait` together cost ~9.5%.
- Review whether a lock-free or reader-writer pattern can replace the mutex on the event delivery path.
- Consider per-connection locks to reduce contention scope.
- Why: lock contention directly adds latency to each event round-trip.

6. Reduce kernel socket buffer churn
- `consume_skb` + `kmem_cache_free` together cost ~12%.
- If the transport supports it, use pre-allocated socket buffers or zero-copy sendmsg paths.
- Why: kernel-side buffer lifecycle is a material per-message cost.

## Comparison with previous profile

| Metric | Previous | Current | Change |
|--------|----------|---------|--------|
| Benchmark latency | ~5228 ns | ~4382 ns | **-16.2%** |
| `entry_SYSCALL_64_after_hwframe` | 72.31% | 74.15% | +1.84pp |
| `handle_event_update_message` | 11.92% | 7.08% | **-4.84pp** |
| `SysPollImpl::poll` | 25.76% | 26.22% | +0.46pp |
| `SocketImpl::recvmsg` | 24.77% | 30.74% | +5.97pp |
| `SocketImpl::sendmsg` | 16.79% | 8.78% | **-8.01pp** |
| `schedule` + `__schedule` | 32.01% | 30.38% | -1.63pp |
| Shared memory map ops | not visible | ~16% | new |
| `shared_ptr` ref counting | not visible | ~14% | new |

Key takeaways:
- The event update handling path (`handle_event_update_message`) improved significantly (-4.84pp), contributing to the 16% latency reduction.
- The send path cost dropped substantially (`sendmsg`: 16.8% → 8.8%), suggesting fewer or more efficient send operations.
- Shared memory management and reference counting costs are now visible as the next optimization targets.
- Overall syscall dominance remains — the workload is still kernel-bound.

## Stability note
- The profiled run completed successfully with 139426 iterations and no failures.
- Previous stability issue (`Failed to send or receive benchmark event`) was not observed in this run.
