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

Uses the S-CORE baseline .clang-tidy from score_cpp_policies as the global
config (passed via --config-file, bypassing filesystem search). The local
.clang-tidy at the repo root is NOT used as a separate config file to avoid
overriding the baseline checks; instead, any local overrides should be added
directly to the global_config or via CheckOptions in the baseline.
"""

load("@aspect_rules_lint//lint:clang_tidy.bzl", "lint_clang_tidy_aspect")
load("@aspect_rules_lint//lint:lint_test.bzl", "lint_test")

clang_tidy_aspect = lint_clang_tidy_aspect(
    binary = Label("@llvm_toolchain//:clang-tidy"),
    # global_config is passed as --config-file to clang-tidy, bypassing the
    # filesystem-based .clang-tidy search. This ensures the S-CORE baseline
    # checks are always applied regardless of sandbox directory layout.
    global_config = [Label("@score_cpp_policies//clang_tidy:.clang-tidy")],
    lint_target_headers = True,
    angle_includes_are_system = True,
)

clang_tidy_test = lint_test(aspect = clang_tidy_aspect)
