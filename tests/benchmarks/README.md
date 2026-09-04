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

# Gateway end-to-end benchmarks

`e2e_benchmarks` runs the existing Google Benchmark client and echo server over the complete
mw::com -> gatewayd -> someipd -> SOME/IP -> someipd -> gatewayd -> mw::com path. Both nodes run
in the isolated network namespace configured by `tests/benchmarks/setup_network.sh`, at
`127.0.0.2` and `127.0.0.3`.

Run the optimized benchmark configuration with:

```bash
bazel test --config=perf-tests //tests/benchmarks:e2e_benchmarks --test_output=streamed
```

To additionally record the gateway daemons and create CPU flamegraphs, install `perf`, then run:

```bash
bazel test --config=perf-tests-flamegraphs //tests/benchmarks:e2e_benchmarks --test_output=streamed
```

To profile only the latency echo benchmark (`IpcBenchmark/LatencyEcho`), run:

```bash
bazel test --config=perf-tests-flamegraphs //tests/benchmarks:e2e_benchmarks_latency_echo --test_output=streamed
```

The `.perf.data` recordings and `.perf.svg` flamegraphs are written beside `benchmarks.json`.

The test starts and stops both daemon pairs and the echo server itself. The client and server use
different mw::com service ids on the shared host, which prevents a LoLa shared-memory shortcut.
The benchmark result is written as `benchmarks.json` under
`bazel-testlogs/tests/benchmarks/e2e_benchmarks/test.outputs/e2e_benchmarks/`.
For the latency echo-only target, results are written under
`bazel-testlogs/tests/benchmarks/e2e_benchmarks_latency_echo/test.outputs/e2e_benchmarks_latency_echo/`.

Unprivileged user namespaces must be enabled so the Bazel `--run_under` wrapper can configure the
isolated loopback addresses and the SOME/IP-SD multicast route.
