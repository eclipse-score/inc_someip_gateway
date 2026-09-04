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
exports_files([
    ".clang-tidy",
    ".ruff.toml",
])

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

# ==============================================================================
# Quality-pack aggregate test suites
# ==============================================================================
# Unit tests (tagged `unit`). A test_suite in a top-level BUILD file cannot use
# a package wildcard like `//score/...` in its `tests` attribute, so unit tests
# are aggregated by explicit label. Run with:
#   bazel test //:unit_tests
test_suite(
    name = "unit_tests",
    tests = [
        "//score/serializer:null_serializer_test",
        "//score/socom/test/unit:socom_test",
    ],
    visibility = ["//visibility:public"],
)

# Component-level tests (integration / stress, tagged `component`). Run with:
#   bazel test //:component_tests
test_suite(
    name = "component_tests",
    tests = [
        "//score/gateway_ipc_binding/test:gateway_ipc_binding_test",
        "//score/socom/test/stress:socom_stress_test",
    ],
    visibility = ["//visibility:public"],
)

docs(
    bundles = [
        {
            "bundle": "//score/config/docs:docs_bundle",
            "mount_at": "config",
            "attach_to": "components",
        },
        {
            "bundle": "//score/gateway_ipc_binding/docs:docs_bundle",
            "mount_at": "gateway_ipc_binding",
            "attach_to": "components",
        },
        {
            "bundle": "//score/gatewayd/docs:docs_bundle",
            "mount_at": "gatewayd",
            "attach_to": "components",
        },
        {
            "bundle": "//score/serializer/docs:docs_bundle",
            "mount_at": "serializer",
            "attach_to": "components",
        },
        {
            "bundle": "//score/socom/docs:docs_bundle",
            "mount_at": "socom",
            "attach_to": "components",
        },
        {
            "bundle": "//score/someip/docs:docs_bundle",
            "mount_at": "someip",
            "attach_to": "components",
        },
        {
            "bundle": "//score/someipd/docs:docs_bundle",
            "mount_at": "someipd",
            "attach_to": "components",
        },
    ],
    source_dir = "docs",
)

# ==============================================================================
# Clang-Tidy and Ruff Linting
# ==============================================================================
# Same code and interface like https://github.com/eclipse-score/communication/blob/5c22c564320afa3d37a1129b827f79c93367edbd/quality/quality.md#clang-tidy

use_clang_tidy_targets()

use_ruff_targets()

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
