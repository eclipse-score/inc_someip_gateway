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

load("@rules_pkg//pkg/private/tar:tar.bzl", "pkg_tar")

pkg_tar(
    name = "socom_headers",
    srcs = [
        "//src/socom:headers",
        "//src/socom:mock_headers",
    ],
    mode = "0644",
    package_dir = "include/score/socom",
)

pkg_tar(
    name = "plugin_interface_headers",
    srcs = [
        "//src/gateway/plugin_interface:headers",
    ],
    mode = "0644",
    package_dir = "include/score/gateway",
)

pkg_tar(
    name = "release_libs",
    srcs = [
        "//src/gateway/plugin_interface:someip_plugin_interface",
        "//src/socom",
    ],
    mode = "0644",
    package_dir = "lib",
)

pkg_tar(
    name = "applications",
    srcs = [
        "//src/gateway/gateway:someip_gateway",
        "//src/gateway/test_integration:test_someip_gateway.sh",
        "//src/gateway/test_integration/common:common.sh",
        "//src/gateway/test_integration/ipc_test_client:ipc_bridge_cpp",
        "//src/gateway/test_integration/ipc_test_client:ipc_bridge_rs",
        "//src/gateway/test_integration/someip_plugin:someip_network_plugin",
    ],
    mode = "0755",
    package_dir = "bin",
)

# Create a tarball that contains the SDK for building SOME/IP plugins.
# It also contains scripts and applications to test the SDK and created plugin.
pkg_tar(
    name = "inc_gateway_sdk",
    deps = [
        ":applications",
        ":plugin_interface_headers",
        ":release_libs",
        ":socom_headers",
    ],
)
