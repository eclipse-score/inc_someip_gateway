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
# SPDX-FileCopyrightText: 2024, 2026 Contributors to the Eclipse Foundation
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************

# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html


# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = "Module Template Project"
project_url = "https://eclipse-score.github.io/module_template/"
project_prefix = "MODULE_TEMPLATE_"
author = "S-CORE"
version = "0.1"

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration


extensions = [
    "sphinxcontrib.plantuml",
    "score_sphinx_bundle",
]

exclude_patterns = [
    # The following entries are not required when building the documentation via 'bazel
    # build //docs:docs', as that command runs in a sandboxed environment. However, when
    # building the documentation via 'bazel run //docs:incremental' or esbonio, these
    # entries are required to prevent the build from failing.
    "bazel-*",
    ".venv_docs",
]

templates_path = ["templates"]

# Enable numref
numfig = True
