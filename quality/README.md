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

# Quality Infrastructure

This directory contains quality-related Bazel configuration used by tests in this repository.

## Contents

- `integration_testing/`: shared macro and environment/plugin artifacts for integration tests.
- `sanitizer/`: sanitizer build flags, runtime environment templates, suppressions, and compatibility constraints.

## Integration Testing

The integration test entry point is the `integration_test(...)` macro in `integration_testing/integration_testing.bzl`.

See [integration_testing/test](integration_testing/test/BUILD) for an example usage.

## Typical Usage

Run integration tests with defaults (Linux Docker backend):

```bash
bazel test //tests/integration:integration
```

Run integration tests on Linux QEMU backend:

```bash
bazel test //tests/integration:integration \
  --//quality/integration_testing/flags:linux_backend=qemu
```

### Execution Backend Selection

Integration tests choose their runtime backend using target platform and the `//quality/integration_testing/flags:linux_backend` build setting.

- Default backend on Linux: **Docker** (`linux_backend = "docker"`).
- Linux QEMU backend: selected when `linux_backend = "qemu"` on Linux.
- QNX backend: selected by target platform (`@platforms//os:qnx`) and uses QNX QEMU artifacts.

### filesystem tar

The tests and their data are packaged into a filesystem tar and either build into (Docker) the image or uploaded after image startup.

### What Runs in Docker

For the Docker backend, the macro builds and uses:

- A filesystem tar from the provided `filesystem` target.
- An OCI image containing:
  - that filesystem tar,
  - sanitizer suppressions tar (only when a sanitizer is enabled),
  - base image

The test runs with the upstream Docker ITF plugin.

### What Runs in QEMU

#### Linux QEMU

If QEMU is selected, the custom plugin [`linux_qemu`](integration_testing/plugins/linux_qemu/README.md) is used.

#### QNX QEMU

For QNX targets, the macro builds a QNX IFS image and passes it to the upstream QEMU ITF plugin with the QNX QEMU config.

### Default Test Parameters Applied by the Macro

Unless explicitly overridden by the test target:

- `size = "enormous"`
- `timeout = "short"`

The macro also always injects `--log-cli-level=DEBUG` for all backends.

### Sanitizer Compatibility Default

Integration tests are marked incompatible with TSAN by default via:

- `target_compatible_with += ["//quality/sanitizer/constraints:no_tsan"]`

Reason in code: integration tests are currently flaky with TSAN.

## Sanitizer Support

`quality/sanitizer/` defines a `//quality/sanitizer/flags:sanitizer` string flag with values:

- `none` (default)
- `asan_ubsan_lsan`
- `tsan`

When any sanitizer is enabled, sanitizer-specific environment files and suppressions are produced and consumed by quality targets that opt into them.
