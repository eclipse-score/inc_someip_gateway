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

load("@rules_pkg//pkg:tar.bzl", "pkg_tar")

genrule(
    name = "extract-fastdev-image",
    srcs = ["@eb_corbos_toolkit_fastdev_archive//file"],
    outs = [
        "ebcl-qemuarm64/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64.wic",
        "ebcl-qemuarm64/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64-vmlinux",
    ],
    cmd = "mkdir -p $(RULEDIR)/ebcl-qemuarm64 && tar xzf $(location @eb_corbos_toolkit_fastdev_archive//file) -C $(RULEDIR)/ebcl-qemuarm64 && chmod +w $(RULEDIR)/ebcl-qemuarm64/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64.wic",
    visibility = ["//visibility:private"],
)

pkg_tar(
    name = "network_config_tar",
    srcs = [
        "config-overlay/etc/config/network/network",
    ],
    mode = "0755",
    package_dir = "/etc/config/network",
)

genrule(
    name = "itf-image",
    srcs = [
        ":extract-fastdev-image",
        ":network_config_tar",
        "build_image.sh",
    ],
    outs = [
        "ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64.wic",
        "ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64-vmlinux",
    ],
    cmd = """
        bash $(location build_image.sh) \
            $(RULEDIR) \
            $(RULEDIR)/ebcl-qemuarm64 \
            $(RULEDIR)/ebcl-qemuarm64-itf \
            $(location :network_config_tar)
    """,
    visibility = ["//visibility:private"],
)

filegroup(
    name = "image",
    srcs = [":ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64.wic"],
    visibility = ["//visibility:public"],
)

filegroup(
    name = "kernel",
    srcs = [":ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64-vmlinux"],
    visibility = ["//visibility:public"],
)
