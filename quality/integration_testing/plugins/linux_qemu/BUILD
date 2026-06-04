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

load("@rules_python//python:defs.bzl", "py_library", "py_test")
load("@score_itf//bazel:py_itf_plugin.bzl", "py_itf_plugin")

py_library(
    name = "linux_qemu_lib",
    srcs = [
        "__init__.py",
        "config.py",
        "qemu.py",
        "qemu_process.py",
    ],
    imports = ["../.."],
    target_compatible_with = ["@platforms//os:linux"],
    visibility = ["//visibility:public"],
    deps = [
        "@score_itf//score/itf/core/process",
        "@score_itf//score/itf/core/utils",
        "@score_itf//score/itf/plugins:core",
        "@score_itf//score/itf/plugins/qemu",
        "@score_someip_gateway_pip//pytest",
    ],
)

py_itf_plugin(
    name = "linux_qemu_plugin",
    enabled_plugins = [
        "quality.integration_testing.plugins.linux_qemu",
    ],
    py_library = ":linux_qemu_lib",
    visibility = ["//visibility:public"],
)

py_test(
    name = "linux_qemu_unit_tests",
    srcs = ["qemu_test.py"],
    imports = ["../.."],
    main = "qemu_test.py",
    target_compatible_with = ["@platforms//os:linux"],
    deps = [
        ":linux_qemu_lib",
    ],
)
