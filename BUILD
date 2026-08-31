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

load("@rules_python//python:pip.bzl", "compile_pip_requirements")
load("@score_docs_as_code//:docs.bzl", "docs")
load("@score_tooling//third_party/format:macros.bzl", "use_format_targets")
load("//tools/lint:linters.bzl", "use_clang_tidy_targets", "use_ruff_targets")

# Needed for coverage report by score/tooling
exports_files(["MODULE.bazel"])

# Expose local .clang-tidy override for the clang-tidy lint aspect
exports_files([".clang-tidy"])

# ==============================================================================
# Code Formatting
# ==============================================================================

use_format_targets(
    languages = [
        "python",
        "starlark",
        "yaml",
        "cpp",
    ],
)

# ==============================================================================
# Documentation
# ==============================================================================

docs(
    bundles = [
        {
            "bundle": "//score/socom/docs:docs_bundle",
            "mount_at": "socom",
            "attach_to": "components",
        },
        {
            "bundle": "//score/gateway_ipc_binding/docs:docs_bundle",
            "mount_at": "gateway_ipc_binding",
            "attach_to": "components",
        },
    ],
    source_dir = "docs",
)

# ==============================================================================
# Clang-Tidy
# ==============================================================================
# Run: bazel test --config=clang-tidy //score/...
# The clang_tidy_aspect is applied to all cc_library/cc_binary targets via the
# --aspects flag in clang_tidy.bazelrc when --config=clang-tidy is used.
# To add per-package lint tests, load clang_tidy_test from //tools/lint:linters.bzl
# and add targets like:
#   clang_tidy_test(name = "clang_tidy", srcs = [":my_lib"])

use_clang_tidy_targets()

use_ruff_targets()

# clang_tidy_test(
#     name = "clang_tidy",
#     srcs = [
#         # "//score/config",
#         "//score/gateway_ipc_binding",
#         # "//score/gateway_ipc_binding/benchmark:gateway_ipc_binding_benchmark",
#         # "//score/gateway_ipc_binding/benchmark:gateway_ipc_binding_memory",
#         # "//score/gateway_ipc_binding/test:gateway_ipc_binding_test",
#         # "//score/gatewayd",
#         # "//score/serializer",
#         "//score/socom",
#         # "//score/socom/test/framework:socom_test_framework",
#         # "//score/socom/test/stress:socom_stress_test",
#         # "//score/socom/test/unit:socom_test",
#         "//score/someip",
#         "//score/someipd",
#     ],  # or a more targeted glob
# )

# ==============================================================================
# Python Dependencies
# ==============================================================================

# Run `bazel run //:python_requirements.update` to update the lock file.
compile_pip_requirements(
    name = "python_requirements",
    env_inherit = [
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "no_proxy",
    ],
    requirements_txt = "python_requirements_lock.txt",
)
