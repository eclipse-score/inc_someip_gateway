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

load("@score_docs_as_code//:docs.bzl", "docs")
load("@score_tooling//third_party/format:macros.bzl", "use_format_targets")

# Needed for coverage report by score/tooling
exports_files(["MODULE.bazel"])

# Needed by //tools/python:python_requirements for lock file regeneration
exports_files([
    "python_requirements_lock.txt",
    "pyproject.toml",
])

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

docs(
    bundles = [
        {
            "bundle": "//score/config:docs_bundle",
            "mount_at": "config",
            "attach_to": "components",
        },
        {
            "bundle": "//score/gateway_ipc_binding:docs_bundle",
            "mount_at": "gateway_ipc_binding",
            "attach_to": "components",
        },
        {
            "bundle": "//score/gatewayd:docs_bundle",
            "mount_at": "gatewayd",
            "attach_to": "components",
        },
        {
            "bundle": "//score/serializer:docs_bundle",
            "mount_at": "serializer",
            "attach_to": "components",
        },
        {
            "bundle": "//score/socom:docs_bundle",
            "mount_at": "socom",
            "attach_to": "components",
        },
        {
            "bundle": "//score/someip:docs_bundle",
            "mount_at": "someip",
            "attach_to": "components",
        },
        {
            "bundle": "//score/someipd:docs_bundle",
            "mount_at": "someipd",
            "attach_to": "components",
        },
    ],
    external_needs = [
        "@score_platform//:needs_json_file",
        "@score_process_description//:needs_json_file",
    ],
    source_dir = "docs",
)

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
        "//score/bridged_services/test:bridged_service_configs_test",
        "//score/gateway_ipc_binding/test/unit:consumer_service_binding_test",
        "//score/gateway_ipc_binding/test/unit:error_test",
        "//score/gateway_ipc_binding/test/unit:fixed_size_container_test",
        "//score/gateway_ipc_binding/test/unit:provider_service_binding_test",
        "//score/gateway_ipc_binding/test/unit:sample_layout_test",
        "//score/gateway_ipc_binding/test/unit:service_binding_test",
        "//score/gateway_ipc_binding/test/unit:someipd_service_binding_test",
        "//score/gatewayd/test:gatewayd_test",
        "//score/gatewayd/test:local_service_instance_test",
        "//score/gatewayd/test:remote_service_instance_test",
        "//score/serializer:null_serializer_test",
        "//score/socom/test/unit:socom_test",
        "//score/someipd/test:gatewayd_services_test",
        "//score/someipd/test:local_network_service_test",
        "//score/someipd/test:someipd_test",
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
