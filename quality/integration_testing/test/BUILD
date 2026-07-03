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
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")
load("@score_itf//:defs.bzl", "py_itf_test")
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

pkg_files(
    name = "example-app-ebclfsa-pkg",
    srcs = [
        "example-app-aarch64.sh",
        "example-app2-aarch64.sh",
    ],
    attributes = pkg_attributes(
        mode = "0777",
    ),
    prefix = "/",
    renames = {
        "example-app-aarch64.sh": "example-app",
        "example-app2-aarch64.sh": "example-app2",
    },
)

integration_test(
    name = "test_hello_world",
    timeout = "moderate",
    srcs = [
        "test_hello_world.py",
    ],
    filesystem = ":example-app-pkg",
)

pkg_tar(
    name = "_qemu_filesystem_test_hello_world_ebclfsa_aarch64_integration_test",
    srcs = [":example-app-ebclfsa-pkg"],
)

py_itf_test(
    name = "test_hello_world_ebclfsa_aarch64_integration_test",
    size = "enormous",
    timeout = "moderate",
    srcs = ["test_hello_world.py"],
    args = select({
        "//quality/integration_testing/flags:linux_qemu": [
            "--log-cli-level=DEBUG",
            "--qemu-config=$(location //quality/integration_testing/environments/ebclfsa_aarch64_qemu:qemu_config)",
            "--qemu-image=$(location @score_ref_int//images/ebclfsa_aarch64/build:ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64.wic)",
            "--qemu-architecture=aarch64",
            "--qemu-filesystem-tar=$(location _qemu_filesystem_test_hello_world_ebclfsa_aarch64_integration_test)",
            "--qemu-kernel=$(location @score_ref_int//images/ebclfsa_aarch64/build:ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64-vmlinux)",
        ],
        "//conditions:default": [],
    }),
    data = select({
        "//quality/integration_testing/flags:linux_qemu": [
            "_qemu_filesystem_test_hello_world_ebclfsa_aarch64_integration_test",
            "//quality/integration_testing/environments/ebclfsa_aarch64_qemu:qemu_config",
            "@score_ref_int//images/ebclfsa_aarch64/build:ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64.wic",
            "@score_ref_int//images/ebclfsa_aarch64/build:ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64-vmlinux",
        ],
        "//conditions:default": [],
    }),
    env = {"DOCKER_HOST": ""},
    plugins = select({
        "//quality/integration_testing/flags:linux_qemu": [
            "//quality/integration_testing/plugins/linux_qemu:linux_qemu_plugin",
        ],
        "//conditions:default": [],
    }),
    tags = [
        "block-network",
        "requires-fakeroot",
    ],
    target_compatible_with = select({
        "//quality/integration_testing/flags:linux_qemu": [
            "@platforms//os:linux",
        ],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
)
