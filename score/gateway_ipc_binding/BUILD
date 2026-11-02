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
load("@score_docs_as_code//:docs.bzl", "docs_bundle")

filegroup(
    name = "headers",
    srcs = glob(
        include = ["*.hpp"],
    ),
)

cc_library(
    name = "gateway_ipc_binding_mw_com",
    srcs = glob(include = ["impl/**"]),
    hdrs = [":headers"],
    features = COMPILER_WARNING_FEATURES,
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

# Keep the bundle at the component package boundary so coverage includes local
# implementation dependencies while excluding code owned by other components.
docs_bundle(
    name = "docs_bundle",
    code_targets = [
        ":gateway_ipc_binding_mw_com",
    ],
    primary_need_id = "comp__gateway_ipc_binding",
    source_dir = "docs",
    visibility = ["//visibility:public"],
)
