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

load("@score_docs_as_code//:docs.bzl", "docs_bundle")

# The bundle sits in its own package so that score/gateway_ipc_binding/BUILD carries no
# load() of @score_docs_as_code.
docs_bundle(
    name = "docs_bundle",
    source_dir = "",
    visibility = ["//visibility:public"],
)
