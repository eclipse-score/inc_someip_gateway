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

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@score_baselibs//score/language/safecpp:toolchain_features.bzl", "COMPILER_WARNING_FEATURES")

cc_binary(
    name = "someip_gateway",
    srcs = glob(include = ["src/**"]),
    dynamic_deps = [
        "//src/gateway/plugin_interface:someip_plugin_interface",
        "//src/socom",
    ],
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//visibility:public"],
    deps = [
        "//src/gateway/assert_handler",
        "//src/gateway/test_integration/payload_transformation_plugin",  # plugin statically linked
        "@boost.program_options",
        "@communication//score/mw/com",
        "@score_baselibs//score/language/futurecpp",
        "@score_baselibs//score/os:glob",
    ],
)
