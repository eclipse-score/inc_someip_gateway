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

# TC8 Enhanced Testability — Application-Level Tests

> **Status:** Planned — placeholder directory.

## Purpose

Enhanced testability tests verify the full gateway end-to-end:

- A **TC8 Service** (C++ mw::com Skeleton) that sends events and fields
- A **TC8 Client** (C++ mw::com Proxy) that subscribes and checks data
- Both go through **gatewayd + someipd**

All test apps use `score::mw::com` only — no direct SOME/IP dependency.
Swapping the SOME/IP stack only requires a config change.

## Planned Structure

```
application/
├── BUILD.bazel                    # Bazel targets (py_pytest + cc_binary)
├── conftest.py                    # Orchestrator: start gatewayd, someipd, service, client
├── apps/
│   ├── tc8_service/               # C++ mw::com skeleton (events + fields)
│   │   ├── BUILD.bazel
│   │   └── main.cpp
│   ├── tc8_client/                # C++ mw::com proxy (subscribe + validate)
│   │   ├── BUILD.bazel
│   │   └── main.cpp
│   └── config/                    # mw_com_config.json, SOME/IP stack JSON, gatewayd config
├── test_enhanced_testability.py   # End-to-end: client ↔ gatewayd ↔ someipd ↔ service
└── helpers/
    └── process_orchestrator.py    # Multi-process lifecycle management
```

## Related Work

- TC8 enhanced testability service and client
- End-to-end validation with SOME/IP gateway
- Integration tests for gatewayd + someipd

## Prerequisites

- TC8 enhanced testability service interface (mw::com IDL)
- S-CORE ITF assessment complete
- TC8 spec analysis for events/fields

## See Also

- [Architecture](../../../docs/architecture/tc8_conformance_testing.rst) — test scope overview
- [Requirements](../../../docs/tc8_conformance/requirements.rst)
- Protocol conformance tests in the parent directory
