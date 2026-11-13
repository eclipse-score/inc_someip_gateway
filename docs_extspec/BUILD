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

load("@score_docs_as_code//:docs.bzl", "docs_bundle")

# Create a build-only metadata export from the pinned source. The extractor
# deliberately omits requirement titles and prose from its output.
genrule(
    name = "spec_needs_json",
    srcs = ["@open_someip_spec//:rst_sources"],
    outs = ["needs.json"],
    cmd = "$(execpath //tools/extneeds:generate_spec_needs) --output $@ $(SRCS)",
    tools = ["//tools/extneeds:generate_spec_needs"],
)

# Generate the local stakeholder needs and their source locations from the
# build-only metadata export.
genrule(
    name = "spec_requirements_rst",
    srcs = [":spec_needs_json"],
    outs = ["index.rst"],
    cmd = "$(execpath //tools/extneeds:generate_spec_requirements) $< $@",
    tools = ["//tools/extneeds:generate_spec_requirements"],
)

docs_bundle(
    name = "docs_bundle",
    srcs = [":spec_requirements_rst"],
    visibility = ["//visibility:public"],
)
