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
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@score_baselibs//score/language/safecpp:toolchain_features.bzl", "COMPILER_WARNING_FEATURES")
load("//bazel/tools:json_schema_validator.bzl", "validate_json_schema_test")

cc_binary(
    name = "gateway_ipc_binding_benchmark",
    srcs = [
        "event_transmission_benchmark_context.hpp",
        "event_transmission_client_to_server_benchmark.cpp",
    ],
    data = [
        "mw_com_config.json",
        "tsan.supp",
    ],
    env = {
        "TSAN_OPTIONS": "halt_on_error=1 suppressions=$(location tsan.supp)",
    },
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//visibility:public"],
    deps = [
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "@google_benchmark//:benchmark",
        "@score_baselibs//score/mw/log:backend_stub_testutil",
    ],
)

cc_binary(
    name = "gateway_ipc_binding_memory",
    srcs = [
        "event_transmission_benchmark_context.hpp",
        "event_transmission_client_to_server_memory.cpp",
    ],
    data = [
        "mw_com_config.json",
        "tsan.supp",
    ],
    env = {
        "TSAN_OPTIONS": "halt_on_error=1 suppressions=$(location tsan.supp)",
    },
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//visibility:public"],
    deps = [
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "@score_baselibs//score/mw/log:backend_stub_testutil",
    ],
)

cc_test(
    name = "event_transmission_benchmark_context_test",
    size = "small",
    srcs = [
        "event_transmission_benchmark_context.hpp",
        "event_transmission_benchmark_context_test.cpp",
    ],
    data = ["mw_com_config.json"],
    # Isolate LoLa's abstract Unix sockets between parallel test processes.
    tags = ["block-network"],
    deps = [
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "@googletest//:gtest",
        "@score_baselibs//score/mw/log:backend_stub_testutil",
    ],
)

validate_json_schema_test(
    name = "mw_com_config_schema_valid",
    size = "small",
    json = "mw_com_config.json",
    schema = "@score_communication//score/mw/com:config_schema",
    tags = ["lint"],
)
