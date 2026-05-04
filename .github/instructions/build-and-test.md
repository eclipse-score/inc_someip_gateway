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
applyTo: "**/BUILD*,**/MODULE*,tests/**,**/*_test.{cpp,cc,py},**/*_test.rs"
---

# Build & Test

**Bazel is the primary build system.**

## Commands

```bash
# Build specific target
bazel build //src/gatewayd:gatewayd

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

## Test Requirements

When adding new code, tests are required by default:
- Integration tests in [tests/integration/BUILD.bazel](../../tests/integration/BUILD.bazel)
- Unit tests next to the software element's source code
- Use `py_pytest` rule for Python tests

## pre-commit

pre-commit must pass, before considering the work done:

```bash
pre-commit run --all-files
```
