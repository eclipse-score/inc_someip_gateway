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

"""
Integration Tests
"""

load("@rules_pkg//pkg:mappings.bzl", "pkg_attributes", "pkg_files")
load("//quality/integration_testing:integration_testing.bzl", "integration_test")

pkg_files(
    name = "vsomeip-runtime-pkg",
    srcs = [
        "@vsomeip",
    ],
    attributes = pkg_attributes(
        mode = "0777",
    ),
    prefix = "/usr",
)

pkg_files(
    name = "example-app-pkg",
    srcs = [
        "vsomeip-local.json",
        ":vsomeip-runtime-pkg",
        "//src/config:config_file",
        "//src/gatewayd",
        "//src/someipd",
        "//src/someipd:etc/mw_com_config.json",
        "//tests/integration/sample_client",
    ],
    attributes = pkg_attributes(
        mode = "0777",
    ),
    prefix = "/",
    renames = {
        "//src/config:config_file": "mw_someip_config.bin",
        "//src/someipd:etc/mw_com_config.json": "mw_com_config.json",
    },
)

integration_test(
    name = "test_hello_world",
    timeout = "moderate",
    srcs = [
        "conftest.py",
        "test_hello_world.py",
    ],
    filesystem = ":example-app-pkg",
    deps = [
        "@score_someip_gateway_pip//pytest",
        "@score_someip_gateway_pip//someip",
    ],
)
