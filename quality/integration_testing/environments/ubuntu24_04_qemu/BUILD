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

# Cloud-init NoCloud seed ISO, generated from user-data and meta-data.
# Tagged "manual" so it is only built when explicitly needed (linux_qemu tests).
genrule(
    name = "seed_iso",
    srcs = [
        "user-data",
        "meta-data",
    ],
    outs = ["seed.img"],
    # Prefer cloud-localds with a vfat seed image for robust NoCloud detection.
    # Fallback to genisoimage when cloud-localds is not installed.
    # cmd = "if command -v cloud-localds >/dev/null 2>&1; then cloud-localds -f vfat $@ $(location user-data) $(location meta-data); else genisoimage -output $@ -volid cidata -iso-level 3 -full-iso9660-filenames -relaxed-filenames -joliet -rock $(SRCS); fi",
    cmd = "cloud-localds -f vfat $@ $(location user-data) $(location meta-data)",
    # cmd = "genisoimage -output $@ -volid cidata -iso-level 3 -full-iso9660-filenames -relaxed-filenames -joliet -rock $(SRCS)",
    tags = ["manual"],
    visibility = ["//:__subpackages__"],
)
