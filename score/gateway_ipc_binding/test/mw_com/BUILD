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

load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@score_baselibs//score/language/safecpp:toolchain_features.bzl", "COMPILER_WARNING_FEATURES")
load("//bazel/tools:json_schema_validator.bzl", "validate_json_schema_test")

# Runs a real mw::com (LoLa) runtime in-process: the SomeipdService skeleton and
# the proxy that observes it both live in this test binary. mw::com may only be
# initialized once per process, so main.cpp does it for the whole binary.
cc_test(
    name = "gateway_ipc_binding_mw_com_test",
    size = "small",
    srcs = glob(include = [
        "*.cpp",
        "*.hpp",
    ]),
    data = ["mw_com_config.json"],
    features = COMPILER_WARNING_FEATURES,
    linkstatic = True,
    # LoLa's message passing uses *abstract* unix domain sockets named
    # LoLa_2_<pid>_QM. The abstract socket namespace is scoped to the network
    # namespace, and under linux-sandbox every test gets a fresh PID namespace
    # and therefore the same low PIDs. Without an own network namespace two
    # concurrent LoLa test processes collide with "Address already in use".
    # block-network makes linux-sandbox put this test into its own network
    # namespace. /dev/shm and /tmp, which LoLa also uses, are already unshared
    # for all tests via --sandbox_tmpfs_path in .bazelrc.
    tags = [
        "block-network",
        "exclusive",  # block-network still does not enable concurrent LoLa tests, so run this test exclusively
    ],
    deps = [
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "//score/socom",
        "@googletest//:gtest",
        "@score_baselibs//score/filesystem",
        "@score_communication//score/mw/com",
    ],
)

validate_json_schema_test(
    name = "mw_com_config_schema_valid",
    size = "small",
    json = "mw_com_config.json",
    schema = "@score_communication//score/mw/com:config_schema",
    tags = ["lint"],
)
