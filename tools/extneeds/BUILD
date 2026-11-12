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

load("@rules_python//python:py_binary.bzl", "py_binary")
load("@rules_python//python:py_test.bzl", "py_test")

py_binary(
    name = "generate_spec_needs",
    srcs = ["generate_spec_needs.py"],
    visibility = ["//visibility:public"],
)

py_binary(
    name = "generate_spec_requirements",
    srcs = ["generate_spec_requirements.py"],
    visibility = ["//visibility:public"],
)

py_test(
    name = "generate_spec_needs_test",
    srcs = [
        "generate_spec_needs.py",
        "test_generate_spec_needs.py",
    ],
    imports = ["."],
    main = "test_generate_spec_needs.py",
)

py_test(
    name = "update_open_someip_spec_test",
    srcs = [
        "test_update_open_someip_spec.py",
        "update_open_someip_spec.py",
    ],
    imports = ["."],
    main = "test_update_open_someip_spec.py",
)
