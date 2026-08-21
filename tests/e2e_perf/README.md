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

# End-to-end performance tests

Measures latency, throughput and loss across the complete gateway chain. Both nodes run on the
same host.

```
node A                                                node B
┌──────────────┐   mw::com    ┌──────────┐         ┌──────────┐   mw::com   ┌───────────────┐
│ perf_sender  │─────────────▶│ gatewayd │────────▶│ gatewayd │────────────▶│ perf_receiver │
└──────────────┘              └────┬─────┘         └────▲─────┘             └───────────────┘
                                   │ IPC                │ IPC
                              ┌────▼─────┐  SOME/IP  ┌──┴───────┐
                              │ someipd  │──────────▶│ someipd  │
                              └──────────┘    UDP    └──────────┘
```

In `roundtrip` mode the receiver echoes every message back over a second service, so the reverse
chain is exercised as well.

## Why everything is named twice

The two nodes share one operating system, and several gateway resources are global to the host.
Each of them therefore gets a per-node name:

| Resource | node A | node B |
|---|---|---|
| gatewayd/someipd IPC socket (`--ipc_channel`) | `perf_ipc_a` | `perf_ipc_b` |
| gateway_ipc_binding shared memory (`/dev/shm/<service_type_name>_<service_id>`) | `perf_req_tx`, `perf_resp_rx` | `perf_req_rx`, `perf_resp_tx` |
| vsomeip unix sockets (`network` in the vsomeip config) | `perf-node-a` | `perf-node-b` |
| vsomeip unicast address | `127.0.0.2` | `127.0.0.3` |
| offered UDP port | 31100 (request) | 31101 (response) |
| mw::com/LoLa service ids | 7100 request, 7103 response | 7102 request, 7101 response |

The mw::com service ids are deliberately different on the two nodes. If both sides used the same
id, the receiver's proxy would bind straight to the sender's skeleton through LoLa shared memory
and the test would silently bypass SOME/IP entirely.

The SOME/IP service ids on the wire are of course identical on both sides: `0x7100` for the
request service and `0x7101` for the response service, instance `0x0001`.

Two more constraints are baked into the configs:

- `service_version_major` is `0`, because someipd subscribes with vsomeip's `DEFAULT_MAJOR` (see
  the TODO in `score/someipd/impl/remote_network_service.cpp`). A different major version on the
  offering side means the subscription is never sent.
- No `eventgroup_ids` are configured, so both sides fall back to using the event id as the
  eventgroup id — again because the consuming side does not yet honour `eventgroup_ids`.
- The performance test covers payloads of 8 bytes, 64 bytes, 1 KiB, 16 KiB, 64 KiB and 256 KiB.
  The SOME/IP connection uses TCP so the larger messages do not depend on UDP datagram limits.

## Host prerequisites

Unprivileged user namespaces must be enabled on the host. `setup_network.sh` runs as
`--run_under`, creates the test's own user and network namespaces, and configures everything the
nodes need: it brings the loopback interface up, assigns the two node addresses (`127.0.0.2` and
`127.0.0.3`) and adds a route for the SOME/IP-SD multicast address. vsomeip only offers services
on the network once it has seen both the interface owning its unicast address and that route, so
neither is optional.

If the setup fails the test skips with an explanation instead of failing.

## Running

```bash
bazel test --config=perf-tests //tests/e2e_perf:e2e_perf --test_output=streamed
```

`--config=perf-tests` points `--run_under` at `setup_network.sh`. The wrapper uses
`unshare --user --net --map-root-user` so the fixed UDP ports cannot clash with the host and the
script can configure the namespace without Bazel's `linux-sandbox`, `block-network`, or
`requires-fakeroot` requirements. The target is incompatible with other configurations because it
only works with that config.

Daemon logs, rendered vsomeip configs, per-case result JSONs and a combined
`e2e_perf_report.json` are written to `TEST_UNDECLARED_OUTPUTS_DIR/e2e_perf`.

> NOTE:
> When run with bazel the logs are usually written to `bazel-testlogs/tests/e2e_perf/e2e_perf/test.outputs/e2e_perf/e2e_perf_report.json`.

## Profiling (flamegraphs)

`:e2e_perf` runs every payload size/mode combination, which is too much overhead to profile as a
whole. `:e2e_perf_profile` runs only the `roundtrip`/`xlarge` case, wraps every daemon and app
(someipd/gatewayd on both nodes, `perf_sender`, `perf_receiver`) in `perf record`, and renders
each capture into a flamegraph SVG:

```bash
bazel test --config=perf-tests-profile //tests/e2e_perf:e2e_perf_profile --test_output=streamed
```

`--config=perf-tests-profile` extends `--config=perf-tests` with `-c opt`,
`-fno-omit-frame-pointer` and `-g`, so `perf` can produce readable call stacks. The host needs a
working `perf` on `PATH` (override with `E2E_PERF_PERF_BIN` if the versioned binary under
`/usr/lib/linux-tools-<version>/perf` must be used instead of the `perf` wrapper, e.g. when the
installed `linux-tools` package does not match the running kernel exactly).

Flamegraph SVGs (`*.perf.data.svg`) and a `roundtrip_xlarge_result.json` are written next to the
other artifacts, under
`bazel-testlogs/tests/e2e_perf/e2e_perf_profile/test.outputs/e2e_perf_profile/`. Open the SVGs in
a browser; wider frames took more samples (i.e. more CPU time).

To confirm that traffic really crosses the network stack, capture while the test runs. Traffic
between the two node addresses is delivered via `lo`, so capture on `any` inside the test's
network namespace:

```bash
sudo tcpdump -i any -n "udp port 31100 or udp port 31101 or udp port 30490"
```

## Running the apps by hand

```bash
bazel build //tests/e2e_perf/... //score/gatewayd //score/someipd

VSOMEIP_CONFIGURATION=$PWD/tests/e2e_perf/config/vsomeip_node_a.json \
  bazel-bin/score/someipd/someipd -c bazel-bin/tests/e2e_perf/node_a_someip_config.bin -i perf_ipc_a &
bazel-bin/score/gatewayd/gatewayd -c bazel-bin/tests/e2e_perf/node_a_someip_config.bin \
  -s tests/e2e_perf/config/node_a_mw_com_config.json -i perf_ipc_a &
# ... same for node B with the _b configs and -i perf_ipc_b

bazel-bin/tests/e2e_perf/perf_receiver -s tests/e2e_perf/config/node_b_mw_com_config.json -n 1000 &
bazel-bin/tests/e2e_perf/perf_sender   -s tests/e2e_perf/config/node_a_mw_com_config.json -n 1000
```

Both binaries print their result JSON to stdout; `--help` lists all options.
