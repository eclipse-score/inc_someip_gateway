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

load("@bazel_skylib//rules:common_settings.bzl", "string_flag")

string_flag(
    name = "linux_backend",
    build_setting_default = "disabled",
    values = [
        "disabled",
        "qemu",
    ],
    visibility = ["//visibility:public"],
)

config_setting(
    name = "linux_qemu",
    constraint_values = ["@platforms//os:linux"],
    flag_values = {":linux_backend": "qemu"},
    visibility = ["//visibility:public"],
)
