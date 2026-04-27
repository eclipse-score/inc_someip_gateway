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

load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@score_baselibs//score/language/safecpp:toolchain_features.bzl", "COMPILER_WARNING_FEATURES")

cc_test(
    name = "gateway_ipc_binding_test",
    srcs = glob(include = [
        "*.cpp",
        "*.hpp",
    ]),
    data = ["tsan.supp"],
    env = {
        "TSAN_OPTIONS": "halt_on_error=1 suppressions=$(location tsan.supp)",
    },
    features = COMPILER_WARNING_FEATURES,
    deps = [
        "//src/gateway_ipc_binding",
        "//src/socom",
        "//src/socom:mock",
        "@googletest//:gtest",
        "@score_baselibs//score/mw/log:backend_stub_testutil",
    ],
)
