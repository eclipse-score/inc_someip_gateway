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
# Copyright 2020 The Bazel Authors. All rights reserved.
# SPDX-FileCopyrightText: 2026 Contributors to the Eclipse Foundation
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************

"""Rule to extract variables from the C++ toolchain for use in package names."""

load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cc_toolchain")
load("@rules_pkg//pkg:providers.bzl", "PackageVariablesInfo")

def _names_from_toolchains_impl(ctx):
    values = {}

    # TODO(https://github.com/bazelbuild/bazel/issues/7260): Switch from
    # calling find_cc_toolchain to direct lookup via the name.
    # cc_toolchain = ctx.toolchains["@rules_cc//cc:toolchain_type"]
    cc_toolchain = find_cc_toolchain(ctx)

    values["cc_cpu"] = cc_toolchain.cpu
    values["libc"] = cc_toolchain.libc

    values["compilation_mode"] = ctx.var.get("COMPILATION_MODE")

    return PackageVariablesInfo(values = values)

#
# Extracting variables from the toolchain to use in the package name.
#
names_from_toolchains = rule(
    # Going forward, the preferred way to depend on a toolchain through the
    # toolchains attribute. The current C++ toolchains, however, are still not
    # using toolchain resolution, so we have to depend on the toolchain
    # directly.
    # TODO(https://github.com/bazelbuild/bazel/issues/7260): Delete the
    # _cc_toolchain attribute.
    attrs = {
        "_cc_toolchain": attr.label(
            default = Label(
                "@rules_cc//cc:current_cc_toolchain",
            ),
        ),
    },
    toolchains = ["@rules_cc//cc:toolchain_type"],
    implementation = _names_from_toolchains_impl,
)
