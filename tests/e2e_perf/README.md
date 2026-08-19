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
| vsomeip unicast address | `172.17.0.2` | `172.17.0.3` |
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
- Payloads are capped at 1 KiB. SOME/IP-TP is not used, so a message must fit into one UDP
  datagram.

## Host prerequisites

The test needs two local IPv4 addresses on a multicast capable interface and a route for the
SOME/IP-SD multicast address:

```bash
sudo ip addr add 172.17.0.3/16 dev eth0
sudo ip route add 224.244.224.245/32 dev eth0
```

Use `E2E_PERF_NODE_A_IP` / `E2E_PERF_NODE_B_IP` to point the test at different addresses. If the
prerequisites are missing the test skips with an explanation instead of failing.

## Running

```bash
bazel test //tests/e2e_perf:e2e_perf --test_output=streamed
```

The target is tagged `manual` (it depends on the host setup above) and `exclusive` (it binds fixed
UDP ports and uses global `/dev/shm` names).

Daemon logs, rendered vsomeip configs, per-case result JSONs and a combined
`e2e_perf_report.json` are written to `TEST_UNDECLARED_OUTPUTS_DIR/e2e_perf`.

To confirm that traffic really crosses the network stack, capture while the test runs. Traffic
between two local addresses is delivered via `lo`, so capture on `any`:

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
