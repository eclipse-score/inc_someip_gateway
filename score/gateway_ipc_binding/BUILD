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
    name = "gateway_ipc_binding_mw_com",
    srcs = glob(include = ["impl/mw_com/**"]) + ["impl/error.cpp"],
    hdrs = [
        "error.hpp",
        "fixed_size_container.hpp",
        "gateway_ipc_binding_client.hpp",
        "gateway_ipc_binding_mw_com.hpp",
        "gateway_ipc_binding_server.hpp",
    ],
    features = COMPILER_WARNING_FEATURES,
    linkopts = ["-latomic"],
    visibility = ["//visibility:public"],
    deps = [
        "//score/socom",
        "@score_baselibs//score/concurrency:thread_pool",
        "@score_baselibs//score/json",
        "@score_baselibs//score/language/futurecpp",
        "@score_baselibs//score/mw/log",
        "@score_baselibs//score/result",
        "@score_communication//score/mw/com",
    ],
)
