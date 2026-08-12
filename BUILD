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
load("@score_tooling//:defs.bzl", "use_format_targets")

# ==============================================================================
# Code Formatting
# ==============================================================================

use_format_targets()

# ==============================================================================
# Documentation
# ==============================================================================

docs(
    source_dir = "docs",
)

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
