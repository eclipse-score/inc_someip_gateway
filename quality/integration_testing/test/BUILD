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
load("@score_rules_imagefs//rules/qnx:ifs.bzl", "qnx_ifs")
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

qnx_ifs(
    name = "_qemu_image_test_hello_world_ebclfsa_x86_64_qnx_integration_test",
    srcs = [
        ":example-app-ebclfsa-pkg",
        "//quality/integration_testing/environments/qnx8_qemu:qnx_config",
    ],
    out = "init_ifs_test_hello_world_ebclfsa_x86_64_qnx_integration_test.img",
    build_file = "//quality/integration_testing/environments/qnx8_qemu:init_build",
    target_compatible_with = [
        "@platforms//cpu:x86_64",
        "@platforms//os:qnx",
    ],
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
            "--qemu-image=$(location //quality/integration_testing/environments/ebclfsa_aarch64_qemu:image)",
            "--qemu-architecture=aarch64",
            "--qemu-filesystem-tar=$(location _qemu_filesystem_test_hello_world_ebclfsa_aarch64_integration_test)",
            "--qemu-kernel=$(location //quality/integration_testing/environments/ebclfsa_aarch64_qemu:kernel)",
            "--qemu-kernel-cmdline-file=$(location //quality/integration_testing/environments/ebclfsa_aarch64_qemu:kernel_cmdline)",
        ],
        "//conditions:default": [],
    }),
    data = select({
        "//quality/integration_testing/flags:linux_qemu": [
            "_qemu_filesystem_test_hello_world_ebclfsa_aarch64_integration_test",
            "//quality/integration_testing/environments/ebclfsa_aarch64_qemu:image",
            "//quality/integration_testing/environments/ebclfsa_aarch64_qemu:kernel",
            "//quality/integration_testing/environments/ebclfsa_aarch64_qemu:kernel_cmdline",
            "//quality/integration_testing/environments/ebclfsa_aarch64_qemu:qemu_config",
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

py_itf_test(
    name = "test_hello_world_ebclfsa_x86_64_qnx_integration_test",
    size = "enormous",
    timeout = "moderate",
    srcs = ["test_hello_world.py"],
    args = select({
        "@platforms//os:qnx": [
            "--log-cli-level=DEBUG",
            "--qemu-config=$(location //quality/integration_testing/environments/qnx8_qemu:qemu_config)",
            "--qemu-image=$(location _qemu_image_test_hello_world_ebclfsa_x86_64_qnx_integration_test)",
            "--qemu-architecture=x86_64",
            "--qemu-boot-mode=kernel-image",
        ],
        "//conditions:default": [],
    }),
    data = select({
        "@platforms//os:qnx": [
            "_qemu_image_test_hello_world_ebclfsa_x86_64_qnx_integration_test",
            "//quality/integration_testing/environments/qnx8_qemu:qemu_config",
        ],
        "//conditions:default": [],
    }),
    env = {"DOCKER_HOST": ""},
    plugins = select({
        "@platforms//os:qnx": [
            "//quality/integration_testing/plugins/linux_qemu:linux_qemu_plugin",
        ],
        "//conditions:default": [],
    }),
    tags = [
        "block-network",
        "requires-fakeroot",
    ],
    target_compatible_with = select({
        "@platforms//os:qnx": [
            "@platforms//cpu:x86_64",
            "@platforms//os:qnx",
        ],
        "//conditions:default": ["@platforms//:incompatible"],
    }),
)
