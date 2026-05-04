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
---
applyTo: "src/**"
---

# Architecture & Key Patterns

## Project Overview

The S-CORE SOME/IP Gateway bridges the SCORE middleware with SOME/IP communication stacks. It's divided into two architectural components with an IPC isolation boundary:
- **gatewayd**: Network-independent gateway logic (C++)
- **someipd**: SOME/IP stack binding (C++)

The gateway also includes Rust examples and comprehensive Python integration tests.

## Core Components

- [src/gatewayd/](../../src/gatewayd/) - Main daemon
- [src/config](../../src/config/) configuration via FlatBuffers
- [src/socom/](../../src/socom/) - SOME/IP abstraction with plugin interface for IPC binding
- [src/someipd/](../../src/someipd/) - SOME/IP binding layer

## Design Patterns

- Configuration injection via FlatBuffers binary (see [main.cpp](../../src/gatewayd/main.cpp) for loading pattern)
- JSON schemas validate configuration ([mw_someip_config.schema.json](../../src/config/mw_someip_config.schema.json))
- Software elements not allowed to allocate on the heap after they have fully started:
  - [src/socom](../../src/socom/)
  - [src/gatewayd](../../src/gatewayd/)
