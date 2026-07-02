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
"""Configuration loading for the Linux QEMU plugin.

Provides a lightweight configuration loader that does not depend on pydantic,
avoiding the need to reference the score_itf-internal pip hub.
"""

import ipaddress
import json
import logging
import re

logger = logging.getLogger(__name__)

_RAM_SIZE_PATTERN = re.compile(r"^[0-9]+[KMGTP]$")


class PortForwarding:
    __slots__ = ("host_port", "guest_port")

    def __init__(self, host_port: int, guest_port: int):
        if not (1 <= host_port <= 65535):
            raise ValueError(f"host_port must be 1..65535, got {host_port}")
        if not (1 <= guest_port <= 65535):
            raise ValueError(f"guest_port must be 1..65535, got {guest_port}")
        self.host_port = host_port
        self.guest_port = guest_port


class Network:
    __slots__ = ("name", "ip_address", "gateway")

    def __init__(self, name: str, ip_address: str, gateway: str):
        if not name:
            raise ValueError("network name must not be empty")
        ipaddress.IPv4Address(ip_address)
        ipaddress.IPv4Address(gateway)
        self.name = name
        self.ip_address = ip_address
        self.gateway = gateway


class QemuConfig:
    """Validated QEMU configuration."""

    __slots__ = (
        "networks",
        "ssh_port",
        "qemu_num_cores",
        "qemu_ram_size",
        "port_forwarding",
    )

    def __init__(
        self, networks, ssh_port, qemu_num_cores, qemu_ram_size, port_forwarding
    ):
        self.networks = networks
        self.ssh_port = ssh_port
        self.qemu_num_cores = qemu_num_cores
        self.qemu_ram_size = qemu_ram_size
        self.port_forwarding = port_forwarding


def load_configuration(config_file: str) -> QemuConfig:
    """Load and validate a QEMU configuration file.

    Args:
        config_file: Path to a JSON configuration file.

    Returns:
        A validated QemuConfig instance.

    Raises:
        ValueError: If validation fails.
    """
    logger.info(f"Loading configuration from {config_file}")

    with open(config_file, "r") as f:
        data = json.load(f)

    # Networks
    raw_networks = data.get("networks", [])
    if not raw_networks:
        raise ValueError(
            f"Invalid config '{config_file}': at least one network required"
        )
    networks = [Network(**n) for n in raw_networks]

    # SSH port
    ssh_port = data.get("ssh_port")
    if not isinstance(ssh_port, int) or not (1 <= ssh_port <= 65535):
        raise ValueError(f"Invalid config '{config_file}': ssh_port must be 1..65535")

    # Cores
    qemu_num_cores = data.get("qemu_num_cores")
    if not isinstance(qemu_num_cores, int) or qemu_num_cores < 1:
        raise ValueError(f"Invalid config '{config_file}': qemu_num_cores must be >= 1")

    # RAM
    qemu_ram_size = data.get("qemu_ram_size", "")
    if not _RAM_SIZE_PATTERN.match(qemu_ram_size):
        raise ValueError(
            f"Invalid config '{config_file}': qemu_ram_size must match [0-9]+[KMGTP]"
        )

    # Port forwarding
    port_forwarding = [PortForwarding(**pf) for pf in data.get("port_forwarding", [])]

    return QemuConfig(
        networks=networks,
        ssh_port=ssh_port,
        qemu_num_cores=qemu_num_cores,
        qemu_ram_size=qemu_ram_size,
        port_forwarding=port_forwarding,
    )
