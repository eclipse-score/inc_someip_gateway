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

load("@rules_python//python:defs.bzl", "py_library")
load("@score_tooling//:defs.bzl", "score_py_pytest")

# ---------------------------------------------------------------------------
# Shared test utilities — consumed by both tests/integration_test and
# tests/tc8_conformance.  imports=["."] adds tests/common/ to sys.path so
# downstream code can do:
#   from capture import tcpdump_capture
# ---------------------------------------------------------------------------

py_library(
    name = "capture",
    srcs = ["capture.py"],
    imports = ["."],
    visibility = ["//tests:__subpackages__"],
)

score_py_pytest(
    name = "test_capture",
    size = "small",
    srcs = ["test_capture.py"],
    deps = [
        ":capture",
    ],
)
