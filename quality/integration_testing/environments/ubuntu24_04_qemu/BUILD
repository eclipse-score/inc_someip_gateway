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

exports_files(
    [
        "qemu_config.json",
        "user-data",
        "meta-data",
    ],
    visibility = ["//:__subpackages__"],
)

filegroup(
    name = "qemu_config",
    srcs = ["qemu_config.json"],
    visibility = ["//:__subpackages__"],
)

# Build a persistent Ubuntu base image with tools needed by integration tests.
# It boots once with internet access and applies the cloud-init customization.
genrule(
    name = "prepared_image",
    srcs = [
        "@ubuntu_24_04_cloudimg//file",
        "@score_itf//scripts:build_ubuntu_x86_64_image.sh",
        "@score_itf//scripts:render_user_data.py",
        "user-data",
        "meta-data",
        "qemu_config.json",
    ],
    outs = ["ubuntu24_04_prepared.qcow2"],
    cmd = """
        bash $(location @score_itf//scripts:build_ubuntu_x86_64_image.sh) \
            $(RULEDIR) \
            $(location @ubuntu_24_04_cloudimg//file) \
            $@ \
            $(location user-data) \
            $(location meta-data) \
            $(location qemu_config.json) \
            $(location @score_itf//scripts:render_user_data.py)
    """,
    tags = [
        "manual",
        "requires-network",
    ],
    tools = [
        "@score_itf//scripts:build_ubuntu_x86_64_image.sh",
        "@score_itf//scripts:render_user_data.py",
    ],
    visibility = ["//:__subpackages__"],
)
