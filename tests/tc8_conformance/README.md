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

# TC8 SOME/IP Conformance Tests

Tests for the S-CORE SOME/IP Gateway based on
[OPEN Alliance TC8](https://opensig.org/tech-committee/tc8-automotive-ethernet-ecu-test-specification/).

## Test Scopes

| Scope | Description | DUT | Status |
|---|---|---|---|
| **Protocol Conformance** | Wire-level SOME/IP (SD, messages, events, fields) | `someipd` standalone | Implemented (SD + MSG + EVT + FLD) |
| **Application-Level Tests** | End-to-end via mw::com through the gateway | gatewayd + someipd + C++ apps | [Planned](application/README.md) |

Protocol conformance tests send/receive raw SOME/IP packets using
[scapy](https://scapy.net/). Application-level tests use C++ `mw::com` applications
and work with any SOME/IP binding.

## Quick Start

Run the wildcard command for your platform for complete coverage. Bazel runs
tests tagged `exclusive` without other tests running concurrently.

### Linux

```bash
# Run all TC8 conformance tests (Linux)
bazel test --config=qemu-integration //tests/tc8_conformance/...

# Run a specific target
bazel test --config=qemu-integration //tests/tc8_conformance:test_tc8_service_discovery

# Run only the timing-sensitive tests
bazel test --config=qemu-integration \
  //tests/tc8_conformance:test_tc8_sd_find \
  //tests/tc8_conformance:test_tc8_sd_phases_timing \
  //tests/tc8_conformance:test_tc8_sd_session_wrap
```

### QNX x86_64

```bash
# Run all TC8 conformance tests
bazel test --config=x86_64-qnx //tests/tc8_conformance/...

# Run only the timing-sensitive tests
bazel test --config=x86_64-qnx \
  //tests/tc8_conformance:test_tc8_sd_find \
  //tests/tc8_conformance:test_tc8_sd_phases_timing \
  //tests/tc8_conformance:test_tc8_sd_session_wrap
```

Timing-sensitive tests are selected by wildcard runs (`/...`) like the other
conformance tests. Their `exclusive` tag makes Bazel run them without other
tests running concurrently. To run only these tests, select the three targets
explicitly as shown above.

## Further Reading

- Architecture, diagrams, and design rationale:
  [docs/architecture/tc8_conformance_testing.rst](../../docs/architecture/tc8_conformance_testing.rst)
- Application-level test scope and plan:
  [application/README.md](application/README.md)
- Network setup and helpers: see the module docstrings in
  [tc8_itf_conftest.py](tc8_itf_conftest.py) and [helpers/](helpers/).
