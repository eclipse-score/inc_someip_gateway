# *******************************************************************************
# Copyright (c) 2026 Contributors to the Eclipse Foundation
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

load("@score_tooling//coverage:defs.bzl", "score_coverage_reporter", "score_coverage_scope")

score_coverage_scope(
    name = "coverage_scope",
    testonly = True,
    deps = [
        "//score/config:config_flatbuffers",
        "//score/gateway_ipc_binding",
        "//score/gatewayd",
        "//score/serializer:interface",
        "//score/serializer:null_serializer",
        "//score/serializer:pre_serialized_data",
        "//score/socom",
        "//score/someip",
        "//score/someip:someip_error",
        "//score/someipd",
    ],
)

score_coverage_reporter(
    name = "reporter_wrapper",
    testonly = True,
    coverage_scope = ":coverage_scope",
    llvm_cov = "@llvm_toolchain//:llvm-cov",
    llvm_cxxfilt = "@llvm_toolchain_llvm//:bin/llvm-cxxfilt",
    llvm_profdata = "@llvm_toolchain//:llvm-profdata",
)
