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
load("@rules_cc//cc:cc_shared_library.bzl", "cc_shared_library")
load("@score_baselibs//score/language/safecpp:toolchain_features.bzl", "COMPILER_WARNING_FEATURES")

filegroup(
    name = "headers",
    srcs = glob(include = ["include/**"]),
    visibility = ["//visibility:public"],
)

cc_library(
    name = "someip_plugin_interface_lib",
    srcs = glob(include = ["src/**"]),
    hdrs = [":headers"],
    features = COMPILER_WARNING_FEATURES,
    includes = [
        "include",
        "src/gateway/plugin_interface/include",
    ],
    visibility = ["//visibility:public"],
)

cc_shared_library(
    name = "someip_plugin_interface",
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//visibility:public"],
    deps = [
        ":someip_plugin_interface_lib",
    ],
)
