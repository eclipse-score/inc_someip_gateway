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

"""Clang-tidy lint aspect and test rule for score_someip_gateway.

Wires the S-CORE baseline .clang-tidy policy from score_cpp_policies with an
optional local override at the repo root. Load clang_tidy_aspect and
clang_tidy_test from this file in BUILD files.
"""

load("@score_cpp_policies//clang_tidy:defs.bzl", "make_clang_tidy_aspect", "make_clang_tidy_test")

clang_tidy_aspect = make_clang_tidy_aspect(
    binary = Label("@llvm_toolchain//:clang-tidy"),
    local_configs = [
        Label("@score_cpp_policies//clang_tidy:.clang-tidy"),  # central baseline
        Label("//:.clang-tidy"),  # local repo-level overrides
    ],
    lint_target_headers = True,
    angle_includes_are_system = True,
)

clang_tidy_test = make_clang_tidy_test(aspect = clang_tidy_aspect)
