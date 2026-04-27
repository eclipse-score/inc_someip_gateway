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

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@score_baselibs//score/language/safecpp:toolchain_features.bzl", "COMPILER_WARNING_FEATURES")

cc_library(
    name = "gateway_ipc_binding",
    srcs = glob(include = ["src/**"]),
    hdrs = glob(include = ["include/**"]),
    features = COMPILER_WARNING_FEATURES,
    includes = [
        "include",
        "src/gateway_ipc_binding/include",
    ],
    linkopts = ["-latomic"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/socom",
        "@score_baselibs//score/memory/shared",
        "@score_baselibs//score/mw/log",
        "@score_communication//score/message_passing",
    ],
)
