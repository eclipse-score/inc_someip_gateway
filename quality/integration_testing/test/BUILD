# *******************************************************************************
# Copyright (c) 2025 Contributors to the Eclipse Foundation
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License Version 2.0 which is available at
# https://www.apache.org/licenses/LICENSE-2.0
#
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************

load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files")
load("//quality/integration_testing:integration_testing.bzl", "integration_test")

cc_binary(
    name = "example-app",
    srcs = ["main.cpp"],
)

cc_binary(
    name = "example-app2",
    srcs = ["main2.cpp"],
)

pkg_files(
    name = "example-app-pkg",
    srcs = [
        ":example-app",
        ":example-app2",
    ],
    attributes = pkg_attributes(
        mode = "0777",
    ),
    prefix = "/",
)

integration_test(
    name = "test_hello_world",
    timeout = "moderate",
    srcs = [
        "test_hello_world.py",
    ],
    filesystem = ":example-app-pkg",
)

integration_test(
    name = "test_hello_world_ebclfsa_refint",
    timeout = "moderate",
    srcs = [
        "test_hello_world.py",
    ],
    filesystem = ":example-app-pkg",
    linux_qemu_architecture = "aarch64",
    linux_qemu_config = "//quality/integration_testing/environments/ebclfsa_aarch64_qemu:qemu_config",
    linux_qemu_image = "@score_ref_int//images/ebclfsa_aarch64/build:ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64.wic",
    linux_qemu_kernel = "@score_ref_int//images/ebclfsa_aarch64/build:ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64-vmlinux",
    linux_qemu_seed_iso = None,
)
