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

# EB Linux (EBcLfSA) fastdev image for aarch64 QEMU
# Provides direct access to the prebuilt image archive
filegroup(
    name = "itf-image",
    srcs = ["@eb_corbos_toolkit_fastdev_archive//file"],
    visibility = ["//visibility:public"],
)

# Convenience alias for accessing the image
alias(
    name = "fastdev-image",
    actual = ":itf-image",
    visibility = ["//visibility:public"],
)
