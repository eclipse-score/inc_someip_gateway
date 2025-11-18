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
load("@score_baselibs//score/language/safecpp:toolchain_features.bzl", "COMPILER_WARNING_FEATURES")

cc_library(
    name = "assert_handler",
    srcs = glob(include = ["src/**"]),
    hdrs = glob(include = ["include/**"]),
    features = COMPILER_WARNING_FEATURES,
    includes = [
        "include",
        "src/gateway/assert_handler/include",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "@score_baselibs//score/language/futurecpp",
    ],
)
