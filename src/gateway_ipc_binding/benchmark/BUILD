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

load("@rules_cc//cc:cc_binary.bzl", "cc_binary")
load("@score_baselibs//score/language/safecpp:toolchain_features.bzl", "COMPILER_WARNING_FEATURES")

cc_binary(
    name = "gateway_ipc_binding_benchmark",
    srcs = [
        "event_transmission_benchmark_context.hpp",
        "event_transmission_client_to_server_benchmark.cpp",
        "read_only_memory_managers_benchmark.cpp",
    ],
    data = ["tsan.supp"],
    env = {
        "TSAN_OPTIONS": "halt_on_error=1 suppressions=$(location tsan.supp)",
    },
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//visibility:public"],
    deps = [
        "//src/gateway_ipc_binding",
        "@google_benchmark//:benchmark_main",
        "@score_baselibs//score/mw/log:backend_stub_testutil",
    ],
)

cc_binary(
    name = "gateway_ipc_binding_memory",
    srcs = [
        "event_transmission_benchmark_context.hpp",
        "event_transmission_client_to_server_memory.cpp",
    ],
    data = ["tsan.supp"],
    env = {
        "TSAN_OPTIONS": "halt_on_error=1 suppressions=$(location tsan.supp)",
    },
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//visibility:public"],
    deps = [
        "//src/gateway_ipc_binding",
        "@google_benchmark//:benchmark",
        "@score_baselibs//score/mw/log:backend_stub_testutil",
    ],
)
