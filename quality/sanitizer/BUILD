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

load("@rules_pkg//pkg:tar.bzl", "pkg_tar")
load("//bazel/rules:expand_template.bzl", "expand_template")

filegroup(
    name = "env_template",
    srcs = select({
        "//quality/sanitizer/flags:tsan": ["tsan.env.template"],
        "//quality/sanitizer/flags:asan_ubsan_lsan": ["asan_ubsan_lsan.env.template"],
    }),
    target_compatible_with = ["//quality/sanitizer/constraints:any_sanitizer"],
)

[
    expand_template(
        name = name + "_env",
        out = name + "_sanitizer.env",
        substitutions = {
            "%ROOT%": root,
        },
        target_compatible_with = ["//quality/sanitizer/constraints:any_sanitizer"],
        template = ":env_template",
        visibility = ["//:__subpackages__"],
    )
    for name, root in (
        [
            "absolute",
            "/",
        ],
        [
            "relative",
            "./",
        ],
    )
]

filegroup(
    name = "suppressions",
    srcs = select({
        "//quality/sanitizer/flags:tsan": ["tsan.supp"],
        "//quality/sanitizer/flags:asan_ubsan_lsan": [
            "asan.supp",
            "lsan.supp",
            "ubsan.supp",
        ],
    }),
    target_compatible_with = ["//quality/sanitizer/constraints:any_sanitizer"],
)

pkg_tar(
    name = "suppressions_pkg",
    srcs = [":suppressions"],
    mode = "0444",
    package_dir = "quality/sanitizer",
    target_compatible_with = ["//quality/sanitizer/constraints:any_sanitizer"],
    visibility = ["//:__subpackages__"],
)

sh_binary(
    name = "wrapper",
    srcs = ["wrapper.sh"],
    data = [
        ":relative_env",
        ":suppressions",
    ],
    target_compatible_with = ["//quality/sanitizer/constraints:any_sanitizer"],
)
