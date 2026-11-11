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

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("@score_baselibs//score/language/safecpp:toolchain_features.bzl", "COMPILER_WARNING_FEATURES")

cc_library(
    name = "mw_com_mocks",
    testonly = True,
    hdrs = ["mw_com_mocks.hpp"],
    features = COMPILER_WARNING_FEATURES,
    visibility = ["//score/gatewayd/test:__pkg__"],
    deps = [
        "//score/socom",
        "//score/socom:mock",
        "@googletest//:gtest",
        "@score_communication//score/mw/com",
    ],
)

cc_library(
    name = "ipc_binding_test_fixture",
    testonly = True,
    hdrs = ["ipc_binding_test_fixture.hpp"],
    features = COMPILER_WARNING_FEATURES,
    deps = [
        ":mw_com_mocks",
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "//score/socom",
        "@googletest//:gtest",
        "@score_communication//score/mw/com",
    ],
)

cc_test(
    name = "error_test",
    size = "small",
    srcs = ["error_test.cpp"],
    features = COMPILER_WARNING_FEATURES,
    linkstatic = True,
    tags = [
        "block-network",
        "unit",
    ],
    visibility = ["//:__pkg__"],
    deps = [
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "fixed_size_container_test",
    size = "small",
    srcs = ["fixed_size_container_test.cpp"],
    features = COMPILER_WARNING_FEATURES,
    linkstatic = True,
    tags = [
        "block-network",
        "unit",
    ],
    visibility = ["//:__pkg__"],
    deps = [
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "sample_layout_test",
    size = "small",
    srcs = ["sample_layout_test.cpp"],
    features = COMPILER_WARNING_FEATURES,
    linkstatic = True,
    tags = [
        "block-network",
        "unit",
    ],
    visibility = ["//:__pkg__"],
    deps = [
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com_impl_headers",
        "@googletest//:gtest_main",
    ],
)

cc_test(
    name = "service_binding_test",
    size = "small",
    srcs = ["service_binding_test.cpp"],
    features = COMPILER_WARNING_FEATURES,
    linkstatic = True,
    tags = [
        "block-network",
        "unit",
    ],
    visibility = ["//:__pkg__"],
    deps = [
        ":ipc_binding_test_fixture",
        ":mw_com_mocks",
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com_impl_headers",
        "//score/socom",
        "//score/socom:mock",
        "@googletest//:gtest_main",
        "@score_baselibs//score/mw/log:backend_stub_testutil",
        "@score_communication//score/mw/com",
    ],
)

cc_test(
    name = "consumer_service_binding_test",
    size = "small",
    srcs = ["consumer_service_binding_test.cpp"],
    features = COMPILER_WARNING_FEATURES,
    linkstatic = True,
    tags = [
        "block-network",
        "unit",
    ],
    visibility = ["//:__pkg__"],
    deps = [
        ":ipc_binding_test_fixture",
        ":mw_com_mocks",
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com_impl_headers",
        "//score/socom",
        "//score/socom:mock",
        "@googletest//:gtest_main",
        "@score_baselibs//score/mw/log:backend_stub_testutil",
        "@score_communication//score/mw/com",
    ],
)

cc_test(
    name = "provider_service_binding_test",
    size = "small",
    srcs = ["provider_service_binding_test.cpp"],
    features = COMPILER_WARNING_FEATURES,
    linkstatic = True,
    tags = [
        "block-network",
        "unit",
    ],
    visibility = ["//:__pkg__"],
    deps = [
        ":ipc_binding_test_fixture",
        ":mw_com_mocks",
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com_impl_headers",
        "//score/socom",
        "//score/socom:mock",
        "@googletest//:gtest_main",
        "@score_baselibs//score/mw/log:backend_stub_testutil",
        "@score_communication//score/mw/com",
    ],
)

cc_test(
    name = "someipd_service_binding_test",
    size = "small",
    srcs = ["someipd_service_binding_test.cpp"],
    features = COMPILER_WARNING_FEATURES,
    linkstatic = True,
    tags = [
        "block-network",
        "unit",
    ],
    visibility = ["//:__pkg__"],
    deps = [
        ":ipc_binding_test_fixture",
        ":mw_com_mocks",
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com",
        "//score/gateway_ipc_binding:gateway_ipc_binding_mw_com_impl_headers",
        "//score/socom",
        "//score/socom:mock",
        "@googletest//:gtest_main",
        "@score_baselibs//score/mw/log:backend_stub_testutil",
        "@score_communication//score/mw/com",
    ],
)
