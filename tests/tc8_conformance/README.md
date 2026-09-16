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

For architecture diagrams and design rationale, see
[docs/architecture/tc8_conformance_testing.rst](../../docs/architecture/tc8_conformance_testing.rst).

## Test Scopes

| Scope | Description | DUT | Status |
|---|---|---|---|
| **Protocol Conformance** | Wire-level SOME/IP (SD, messages, events, fields) | `someipd` standalone | Implemented (SD + MSG + EVT + FLD) |
| **Application-Level Tests** | End-to-end via mw::com through the gateway | gatewayd + someipd + C++ apps | [Planned](application/README.md) |

Protocol conformance tests send/receive raw SOME/IP packets using the Python `someip`
library. Application-level tests use C++ `mw::com` applications and work with any
SOME/IP binding.

## Quick Start

```bash
# Run all TC8 conformance tests (Linux)
bazel test --config=tc8-itf //tests/tc8_conformance/...

# Run a specific target
bazel test --config=tc8-itf //tests/tc8_conformance:test_tc8_service_discovery

# Run on QNX x86_64
bazel test --config=tc8-itf-qnx //tests/tc8_conformance/...
```

## Further Reading

- Architecture, diagrams, and design rationale:
  [docs/architecture/tc8_conformance_testing.rst](../../docs/architecture/tc8_conformance_testing.rst)
- Application-level test scope and plan:
  [application/README.md](application/README.md)
- Network setup, port assignment, and config template placeholders: see the
  module docstrings in `tc8_itf_conftest.py` and `helpers/dut_lifecycle.py`
- Purpose of each helper: see the module docstring at the top of each file
  under `helpers/`
