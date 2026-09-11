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

filegroup(
    name = "headers",
    srcs = glob(
        include = ["*.hpp"],
        exclude = ["gateway_ipc_binding_mw_com.hpp"],
    ),
)

cc_library(
    name = "gateway_ipc_binding",
    srcs = glob(
        include = ["impl/**"],
        exclude = ["impl/mw_com/**"],
    ),
    hdrs = [":headers"],
    features = COMPILER_WARNING_FEATURES,
    linkopts = ["-latomic"],
    visibility = ["//visibility:public"],
    deps = [
        "//score/socom",
        "@score_baselibs//score/mw/log",
        "@score_communication//score/memory/shared",
        "@score_communication//score/message_passing",
    ],
)

# Alternative binding implementation on top of mw::com (LoLa),
# see docs/mw_com_binding.rst. Kept in a separate target so that users of the
# score::message_passing based implementation do not pull in mw::com.
cc_library(
    name = "gateway_ipc_binding_mw_com",
    srcs = glob(include = ["impl/mw_com/**"]),
    hdrs = ["gateway_ipc_binding_mw_com.hpp"],
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//visibility:public"],
    deps = [
        ":gateway_ipc_binding",
        "//score/socom",
        "@score_baselibs//score/mw/log",
        "@score_communication//score/mw/com",
    ],
)
