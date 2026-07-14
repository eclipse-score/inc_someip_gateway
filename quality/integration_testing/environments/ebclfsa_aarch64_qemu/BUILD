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

exports_files(
    [
        "kernel_cmdline.txt",
        "qemu_config.json",
    ],
    visibility = ["//:__subpackages__"],
)

filegroup(
    name = "qemu_config",
    srcs = ["qemu_config.json"],
    visibility = ["//:__subpackages__"],
)

filegroup(
    name = "kernel_cmdline",
    srcs = ["kernel_cmdline.txt"],
    visibility = ["//visibility:public"],
)

# Convenience alias for accessing the image
alias(
    name = "image",
    actual = "@score_ref_int//images/ebclfsa_aarch64/build:ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64.wic",
    visibility = ["//visibility:public"],
)

# Convenience alias for accessing the kernel image
alias(
    name = "kernel",
    actual = "@score_ref_int//images/ebclfsa_aarch64/build:ebcl-qemuarm64-itf/fastdev-ubuntu-ebclfsa-ebcl-qemuarm64-vmlinux",
    visibility = ["//visibility:public"],
)
