# Copyright 2020 The Bazel Authors. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#    http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0

"""Deployment helpers: toolchain variable extraction and package tar macro."""

load("@rules_cc//cc:find_cc_toolchain.bzl", "find_cc_toolchain")
load("@rules_pkg//pkg:providers.bzl", "PackageVariablesInfo")
load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

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

def scoreipgw_pkg_tar(name, srcs, package_dir):
    """Creates a platform-aware deployment tar package.

    Encapsulates the common pkg_tar attributes shared by all gateway packages:
    runfiles inclusion, platform-stamped filename, and public visibility.

    Args:
        name: Target name; also used as the archive filename prefix.
        srcs: Files and targets to include in the archive.
        package_dir: Root directory path inside the archive.
    """
    pkg_tar(
        name = name,
        srcs = srcs,
        out = name + ".tar.gz",
        extension = "tar.gz",
        include_runfiles = True,
        package_dir = package_dir,
        package_file_name = name + "-{cc_cpu}.tar.gz",
        package_variables = ":toolchain_vars",
        visibility = ["//visibility:public"],
    )

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
