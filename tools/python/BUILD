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

load("@rules_python//python:pip.bzl", "compile_pip_requirements")
load("@score_tooling//:defs.bzl", "dash_license_checker")

# ==============================================================================
# Python Dependency Lock File Update
# ==============================================================================
# Run `bazel run //tools/python:python_requirements.update` to update the lock file.
compile_pip_requirements(
    name = "python_requirements",
    src = "//:pyproject.toml",
    env_inherit = [
        "HTTP_PROXY",
        "HTTPS_PROXY",
        "no_proxy",
    ],
    requirements_txt = "//:python_requirements_lock.txt",
)

# ==============================================================================
# License Check (Eclipse Dash)
# ==============================================================================
# Run by the license-check CI workflow: `bazel run //tools/python:license-check`
# Checks the Python dependencies pinned in python_requirements_lock.txt.
dash_license_checker(
    src = "//:python_requirements_lock.txt",
    file_type = "requirements",
    visibility = ["//visibility:public"],
)
