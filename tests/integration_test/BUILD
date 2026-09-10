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
        ":vsomeip.json",
        ":vsomeip-runtime-pkg",
        "//score/config:config_file",
        "//score/gatewayd",
        "//score/gatewayd:etc/mw_com_config.json",
        "//score/serializer:null_serializer",
        "//score/someipd",
    ],
    attributes = pkg_attributes(
        mode = "0777",
    ),
    prefix = "/",
    renames = {
        "//score/config:config_file": "mw_someip_config.bin",
        "//score/gatewayd:etc/mw_com_config.json": "gatewayd_mw_com_config.json",
    },
)

pkg_files(
    name = "score-test-applications-pkg",
    testonly = True,
    srcs = [
        "//score/gateway_ipc_binding/test:gateway_ipc_binding_test",
        "//score/serializer:null_serializer_test",
        "//score/socom/test/stress:socom_stress_test",
        "//score/socom/test/unit:socom_test",
    ],
    attributes = pkg_attributes(
        mode = "0777",
    ),
    prefix = "/",
)

integration_test(
    name = "test_score_test_applications",
    timeout = "long",
    srcs = [
        "score_test_applications.py",
    ],
    filesystem = ":score-test-applications-pkg",
    deps = [
        "@score_someip_gateway_pip//pytest",
    ],
)

integration_test(
    name = "test_tcpdump",
    timeout = "moderate",
    srcs = [
        "tcpdump.py",
        "util.py",
    ],
    filesystem = ":example-app-pkg",
    deps = [
        "@score_someip_gateway_pip//pytest",
    ],
)

integration_test(
    name = "test_gateway_startup",
    timeout = "moderate",
    srcs = [
        "conftest.py",
        "gatewayd_startup.py",
        "util.py",
    ],
    filesystem = ":example-app-pkg",
    deps = [
        "@score_someip_gateway_pip//pytest",
        "@score_someip_gateway_pip//someip",
    ],
)

integration_test(
    name = "test_someipd_startup",
    timeout = "moderate",
    srcs = [
        "conftest.py",
        "someipd_startup.py",
        "util.py",
    ],
    filesystem = ":example-app-pkg",
    deps = [
        "@score_someip_gateway_pip//pytest",
        "@score_someip_gateway_pip//someip",
    ],
)

integration_test(
    name = "test_someipd_sends_someip-sd_message",
    timeout = "moderate",
    srcs = [
        "conftest.py",
        "someipd_sends_someip-sd_message.py",
        "util.py",
    ],
    filesystem = ":example-app-pkg",
    deps = [
        "@score_someip_gateway_pip//pytest",
        "@score_someip_gateway_pip//someip",
    ],
)

integration_test(
    name = "test_someipd_started_by_test",
    timeout = "moderate",
    srcs = [
        "conftest.py",
        "someipd_started_by_test.py",
        "util.py",
    ],
    filesystem = ":example-app-pkg",
    deps = [
        "@score_someip_gateway_pip//pytest",
        "@score_someip_gateway_pip//someip",
    ],
)
