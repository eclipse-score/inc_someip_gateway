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

load("@score_itf//:defs.bzl", "py_itf_test")

py_itf_test(
    name = "test_example",
    srcs = ["test_example.py"],
    args = ["--docker-image=ubuntu:24.04"],
    plugins = ["@score_itf//score/itf/plugins:docker_plugin"],
)
