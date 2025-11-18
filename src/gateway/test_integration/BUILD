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

load("@rules_shell//shell:sh_test.bzl", "sh_test")

exports_files(
    ["test_someip_gateway.sh"],
    visibility = ["//visibility:public"],
)

sh_test(
    name = "integration_test_someip_gateway",
    timeout = "short",
    srcs = [
        "test_someip_gateway.sh",
    ],
    data = [
        "//src/gateway/gateway:someip_gateway",
        "//src/gateway/test_integration/common:common.sh",
        "//src/gateway/test_integration/ipc_test_client:ipc_bridge_cpp",
        "//src/gateway/test_integration/ipc_test_client:ipc_bridge_rs",
        "//src/gateway/test_integration/someip_plugin:someip_network_plugin",
    ],
    tags = ["exclusive"],
)
