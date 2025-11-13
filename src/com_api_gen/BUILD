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
load("@rules_rust//rust:defs.bzl", "rust_library", "rust_test")
load("@score_orchestrator_crates//:defs.bzl", "all_crate_deps")
load("@score_someip_gateway_crates//:defs.bzl", "all_crate_deps")

rust_library(
    name = "libcom_api_gen",
    srcs = glob(["src/**/*.rs"]),
    crate_features = ["iceoryx"],
    crate_name = "com_api_gen",
    visibility = ["//visibility:public"],
    deps = [
        "//src/gateway_common:libgateway_common",
        "@score_orchestrator//src/kyron:libkyron",
        "@score_someip_gateway_crates//:com-api",
        "@score_someip_gateway_crates//:com-api-runtime-iceoryx",
        "@score_someip_gateway_crates//:iceoryx2-0.7.0",
    ],
)
