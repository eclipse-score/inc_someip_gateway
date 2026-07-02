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

# Linux QEMU

For Linux QEMU, the macro provide the following arguments:

- `--qemu-config` QEMU config following the format of ITF QEMU config
- `--qemu-image` Linux base image with [cloud-init](https://docs.cloud-init.io/en/latest/index.html) support
- `--qemu-seed-iso` Image containing [cloud-init](https://docs.cloud-init.io/en/latest/index.html) configuration
- `--qemu-filesystem-tar` built from the `filesystem` target

The custom plugin behavior is:

1. Create a **temporary qcow2 overlay** backed by `--qemu-image`.
2. Boot QEMU from that overlay (optionally with seed ISO).
3. Upload and extract `--qemu-filesystem-tar` into `/` inside the guest.
4. Remove the temporary overlay after the test session.

This is intentional: the Linux base image is treated as a pristine input and is **not modified** by test execution.
That ensures that tests cannot leave artifacts influencing subsequent tests.

## Why this plugin

The ITF QEMU plugin does not support specifying more than one disk via `--qemu-seed-iso`,
which is needed for customization without modification of the base image.
