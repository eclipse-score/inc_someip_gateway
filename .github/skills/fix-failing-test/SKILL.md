---
name: fix-failing-test
description: 'Fix a failing test in this C++/Bazel/Python project. Use when a test is red, flaky, or crashes (segfault, assertion, timeout). Covers: reproducing the failure, root-cause analysis, surgical fix, and verification. Trigger phrases: fix failing test, test is broken, test crashes, test flakes, make test pass.'
argument-hint: 'test target or test name, e.g. //src/gateway_ipc_binding/test:gateway_ipc_binding_test'
---

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

# Fix Failing Test

## When to Use
- A Bazel test target is failing or flaky
- A test crashes (segfault, abort, timeout, exit 130)
- You need to make a specific test pass without breaking others

## Procedure

### 1. Reproduce the Failure
```bash
# Run the specific test in isolation
bazel test <target> --test_output=all

# For flaky tests, run multiple times
bazel test <target> --runs_per_test=5 --test_output=all
```
- Capture the full error message, assertion, or signal (SIGSEGV, SIGABRT, exit 130).
- Note whether the failure is deterministic or intermittent (flaky).

### 2. Read the Test and Production Code
- Open the test file and the unit(s) under test.
- Understand what the test is asserting and the expected sequence of operations.
- For C++ tests: check `gtest` assertions, mock expectations, and RAII lifetimes.
- For Python tests: check pytest fixtures in `conftest.py` and any `vsomeip.json` configs.

### 3. Classify the Failure

| Symptom | Likely Cause |
|---------|-------------|
| Assertion failure on state | Wrong initial state or missed transition |
| Crash / SIGSEGV | Use-after-free, dangling ref, uninitialized memory |
| Mock expectation not met | Missing call, wrong order, or wrong argument matcher |
| Timeout / deadlock | Missing notify, wrong condition variable, lock inversion |
| Flaky (passes sometimes) | Race condition, reconnect/retry timing, test isolation leak |

### 4. Check Repo Memory
- Read `/memories/repo/` for known flaky-test patterns in this codebase before deep-diving into the code. Known patterns are recorded there to avoid re-discovering them.

### 5. Implement the Fix
- Make the **minimum change** that fixes the root cause.
- Do not change unrelated code, add logging, or refactor.
- For reconnect/state-machine bugs: replay pending state after reconnect, use `insert_or_assign` for reused handles, and do not advance state when a send fails.

### 6. Verify
```bash
# Confirm the target test passes
bazel test <target> --test_output=all

# Run the full test suite to check for regressions
bazel test //...
```
- For flaky fixes: run several times (`--runs_per_test=10`) to confirm stability.

### 7. Record New Patterns
- If the root cause was non-obvious or flaky, add a bullet to the relevant file under `/memories/repo/` so future agents avoid the same investigation.

## Project-Specific Notes
- **Build system**: Bazel — always use `bazel test`, not direct binary execution, to get correct runfiles and sandbox.
- **IPC tests**: `gateway_ipc_binding` tests exercise reconnect logic; see `/memories/repo/gateway_ipc_binding_reconnect_flake.md` for known pitfalls.
- **Integration tests**: live under `//tests/integration`; require two daemons (`gatewayd` + `someipd`) and vsomeip config from `tests/integration/vsomeip-local.json`.
- **Python tests**: use `py_pytest` rule; run with `bazel test //tests/integration:integration --test_output=all`.
- **License headers**: any new source file must include the Apache 2.0 header from `AGENTS.md`.
