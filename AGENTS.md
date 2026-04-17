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

# AI Agent Guidelines for SOME/IP Gateway

<!-- The first sections are based on https://github.com/forrestchang/andrej-karpathy-skills -->

## General instructions

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

### 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

### 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

### 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

### 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

## Project Overview

The S-CORE SOME/IP Gateway bridges the SCORE middleware with SOME/IP communication stacks. It's divided into two architectural components with an IPC isolation boundary:
- **gatewayd**: Network-independent gateway logic (C++)
- **someipd**: SOME/IP stack binding (C++)

The gateway also includes Rust examples and comprehensive Python integration tests.

## Code Style & Conventions

**All source files must include Apache 2.0 license headers:**
```cpp
/********************************************************************************
 * Copyright (c) <year> Contributors to the Eclipse Foundation
 *
 * See the NOTICE file(s) distributed with this work for additional
 * information regarding copyright ownership.
 *
 * This program and the accompanying materials are made available under the
 * terms of the Apache License Version 2.0 which is available at
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * SPDX-License-Identifier: Apache-2.0
 ********************************************************************************/
```

**C++ style**: namespace organization (e.g., `score::someip_gateway::gatewayd`), includes config via FlatBuffers, uses RAII patterns for resource management.

**Rust style**: Standard Rust formatting with module organization matching C++ namespaces conceptually.

## Architecture & Key Patterns

**Core Components:**
- [src/gatewayd/](src/gatewayd/) - Main daemon, configuration via FlatBuffers ([gatewayd_config.fbs](src/gatewayd/etc/gatewayd_config.fbs))
- [src/socom/](src/socom/) - SOME/IP abstraction with plugin interface for IPC binding
- [src/someipd/](src/someipd/) - SOME/IP binding layer
- [src/network_service/interfaces/](src/network_service/interfaces/) - Network abstraction (message_transfer.h)
- [examples/car_window_sim/](examples/car_window_sim/) - Complete working Rust example

**Design Patterns:**
- Configuration injection via FlatBuffers binary (see [main.cpp](src/gatewayd/main.cpp#L52) for loading pattern)
- IPC boundaries enforce ASIL/QM context separation
- JSON schemas validate configuration ([gatewayd_config.schema.json](src/gatewayd/etc/gatewayd_config.schema.json))

## Build & Test Commands

**Bazel is the primary build system.** Python 3.12 is required; pinned in MODULE.bazel.

```bash
# Build specific target
bazel build //src/gatewayd:gatewayd

# Run daemons (requires two terminals)
bazel run //src/gatewayd:gatewayd_example
bazel run //src/someipd

# Run example application
bazel run //examples/car_window_sim:car_window_controller

# Run all tests
bazel test //...

# Run unit tests
bazel test //src/...

# Run integration tests
bazel test //tests/integration:integration

# Run performance benchmarks
bazel test //tests/benchmarks:all

# Generate/update compile_commands.json for IDE support
bazel run //:bazel-compile-commands
```

**When adding new code, tests are required by default:**
- Integration tests in [tests/integration/BUILD.bazel](tests/integration/BUILD.bazel)
- Unit tests next to the software elements source code
- Use `py_pytest` rule for Python tests

## Project Conventions

**Directory layout**
- Units and components are called software elements
- Software elements are placed underneath [src/](src/)
- A software elements directory be default looks like this:
  ```
  <software element>/
  ├── BUILD
  ├── doc/
  ├── include/
  ├── src/
  └── test/```

**Configuration Management:**
- FlatBuffers schemas define typed configs (see [gatewayd_config.fbs](src/gatewayd/etc/gatewayd_config.fbs))
- JSON schemas auto-generated for validation (tool: flatbuffers/flatc)
- Config files loaded from binary format (see [main.cpp](src/gatewayd/main.cpp#L52))

**Logging & Shutdown:**
- Use `std::cout`/`std::cerr` for basic logging (see [main.cpp](src/gatewayd/main.cpp#L37))
- Implement graceful shutdown with signal handlers (SIGTERM, SIGINT)

**Example Pattern (Rust state machines):**
Reference [car_window.rs](examples/car_window_sim/src/car_window.rs#L27) for window state machine implementation—shows how to structure stateful domain logic.

## Integration Points

**External Dependencies:**
- `@score_communication` - SCORE middleware (IPC via mw/com)
- `@flatbuffers` - Configuration serialization
- `@someip_pip` - SOME/IP stack bindings in Python tests
- `@bazel_tools_python` - Testing framework (py_pytest)

**Configuration:**
- Service instances read from [mw_com_config.json](src/gatewayd/etc/mw_com_config.json)
- SOME/IP specifics in [vsomeip.json](tests/integration/vsomeip.json) and [vsomeip-local.json](tests/integration/vsomeip-local.json)

## Security & ASIL Considerations

The IPC boundary between gatewayd and someipd enforces **ASIL/QM context separation**—this is a critical architectural boundary. Changes affecting message transfer across this boundary require careful review.

## Contributing

Follow [CONTRIBUTION.md](CONTRIBUTION.md):
- Use provided PR templates (bug_fix.md, improvement.md)
- All commits must follow Eclipse Foundation rules
- Code review via CODEOWNERS

---

**Last updated: 2025-04-17**
