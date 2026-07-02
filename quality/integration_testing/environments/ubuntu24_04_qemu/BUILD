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

# Network config for cloud-init
# Tagged "manual" so it is only built when explicitly needed (linux_qemu tests).
genrule(
    name = "network_config",
    srcs = ["qemu_config.json"],
    outs = ["network-config.yaml"],
    cmd = """
set -euo pipefail

python3 - <<'PY' "$(location qemu_config.json)" "$@"
import json
import sys

config_path, output_path = sys.argv[1:3]

with open(config_path, "r", encoding="utf-8") as config_file:
    config = json.load(config_file)

networks = config.get("networks", [])
if not networks:
    raise ValueError("qemu_config.json must define at least one network")

network = networks[0]

with open(output_path, "w", encoding="utf-8") as network_config:
    network_config.write("version: 2\\n")
    network_config.write("ethernets:\\n")
    network_config.write("  testnic0:\\n")
    network_config.write("    match:\\n")
    network_config.write('      name: "en*"\\n')
    network_config.write("    dhcp4: false\\n")
    network_config.write("    dhcp6: false\\n")
    network_config.write("    optional: true\\n")
    network_config.write("    link-local: []\\n")
    network_config.write("    addresses:\\n")
    network_config.write(f"      - {network['ip_address']}/16\\n")
    network_config.write("    routes:\\n")
    network_config.write("      - to: default\\n")
    network_config.write("        on-link: true\\n")
    network_config.write(f"        via: {network['gateway']}\\n")
PY
""",
    tags = ["manual"],
    visibility = ["//:__subpackages__"],
)

# Cloud-init seed image generation to configure the Ubuntu 24.04 QEMU image on boot.
# Tagged "manual" so it is only built when explicitly needed (linux_qemu tests).
genrule(
    name = "seed_iso",
    srcs = [
        ":network_config",
        "user-data",
        "meta-data",
    ],
    outs = ["seed.img"],
    cmd = "cloud-localds -N $(location :network_config) $@ $(location user-data) $(location meta-data)",
    tags = ["manual"],
    visibility = ["//:__subpackages__"],
)
