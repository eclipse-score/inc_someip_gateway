# *******************************************************************************
# Copyright (c) 2025 Elektrobit Automotive GmbH
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

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@score_baselibs//:bazel/unit_tests.bzl", "cc_gtest_unit_test")
load("@score_baselibs//score/language/safecpp:toolchain_features.bzl", "COMPILER_WARNING_FEATURES")

filegroup(
    name = "headers",
    srcs = glob(include = ["include/**"]),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "socom",
    srcs = glob(include = ["src/**"]),
    hdrs = [":headers"],
    defines = [
        "WITH_SOCOM_DEADLOCK_DETECTION=1",
    ],
    features = COMPILER_WARNING_FEATURES,
    includes = [
        "include",
        "src/socom/include",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@score_baselibs//score/result",
    ],
)

filegroup(
    name = "mock_headers",
    srcs = glob(include = ["mock/**"]),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "mock",
    hdrs = [":mock_headers"],
    includes = ["mock"],
    visibility = ["//visibility:public"],
    deps = [
        ":socom",
        "@googletest//:gtest_for_library",
    ],
)

cc_gtest_unit_test(
    name = "test",
    srcs = glob(include = ["test_unit/**"]),
    deps = [
        ":mock",
        ":socom",
    ],
)
