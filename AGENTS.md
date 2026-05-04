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

# SOME/IP Gateway — Agent Instructions

The S-CORE SOME/IP Gateway bridges the SCORE middleware with SOME/IP communication stacks (C++, Rust examples, Python integration tests).

## Essentials

| What | Value |
|------|-------|
| Build system | **Bazel** |
| Build | `bazel build //src/gatewayd:gatewayd` |
| Test all | `bazel test //...` |
| Unit tests | `bazel test //src/...` |
| Integration tests | `bazel test //tests/integration:integration` |
| Compile commands | `bazel run //:bazel-compile-commands` |

## Universal Rules

- **License headers**: All source files must have the Apache 2.0 header. Always add it when editing a file that's missing one.
- **Tests required**: New code must include tests (unit tests co-located, integration in `tests/`).
- **Temporary files**: Use `.llm_tmp/` at repo root. Never use `/tmp`. This directory is ephemeral — overwrite freely without confirmation.

## Detailed Instructions

Topic-specific guidelines are in [.github/instructions/](.github/instructions/):

| File | Scope | Content |
|------|-------|---------|
| [agent-behavior.md](.github/instructions/agent-behavior.md) | All tasks | Think before coding, simplicity, surgical changes, goal-driven execution |
| [architecture.md](.github/instructions/architecture.md) | `src/**` | Components, IPC boundary, ASIL separation, external dependencies |
| [code-style.md](.github/instructions/code-style.md) | `*.cpp,*.h,*.rs,*.py` | License header template, C++ conventions, style application rules |
| [build-and-test.md](.github/instructions/build-and-test.md) | `BUILD*`, `tests/**` | Full Bazel command reference, test requirements |
| [project-structure.md](.github/instructions/project-structure.md) | `src/**` | Directory layout, config management, logging, example patterns |
| [contributing.md](.github/instructions/contributing.md) | `.github/**` | PR templates, Eclipse Foundation rules |

---

**Last updated: 2026-04-30**
