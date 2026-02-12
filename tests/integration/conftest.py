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

import os
import re
import subprocess
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Generator

import paramiko
import pytest

# ---------------------------------------------------------------------------
# QEMU SSH connectivity fixtures
# ---------------------------------------------------------------------------

# QEMU SLIRP port forwarding configuration
SSH_HOST = "127.0.0.1"
SSH_PORT = 2222
SSH_USER = "root"
SSH_TIMEOUT = 10


@pytest.fixture(scope="module")
def ssh_client():
    """Create SSH connection to QEMU guest via port forwarding."""
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    # Try connecting with retries (QEMU might still be booting)
    max_retries = 3
    for attempt in range(max_retries):
        try:
            client.connect(
                hostname=SSH_HOST,
                port=SSH_PORT,
                username=SSH_USER,
                password="",  # QNX root has no password by default
                timeout=SSH_TIMEOUT,
                allow_agent=False,
                look_for_keys=False,
            )
            yield client
            client.close()
            return
        except Exception as e:
            if attempt < max_retries - 1:
                time.sleep(2)
            else:
                pytest.fail(f"Cannot connect to QEMU at {SSH_HOST}:{SSH_PORT} - is QEMU running? Error: {e}")


# ---------------------------------------------------------------------------
# Local (non-QEMU) fixtures — preserved from original
# ---------------------------------------------------------------------------

@pytest.fixture(scope="class")
def someipd_config() -> Path:
    """Provide SOME/IP configuration parameters."""
    return Path("vsomeip-local.json")


@pytest.fixture(scope="class")
def someipd(someipd_config) -> Generator[None, None, None]:
    """Start someipd before tests and stop it after."""
    someipd = subprocess.Popen(
        ["src/someipd/someipd"],
        env={"VSOMEIP_CONFIGURATION": str(someipd_config.absolute())},
    )
    yield
    someipd.terminate()
    someipd.wait()


# Pytest hooks
def pytest_configure(config: pytest.Config) -> None:
    config.addinivalue_line("markers", "integration: mark test as integration test")
    config.addinivalue_line("markers", "slow: mark test as slow running")
    config.addinivalue_line("markers", "network: mark test as requiring network access")
    config.addinivalue_line("markers", "qemu: mark test as requiring QEMU + QNX IFS image")


def pytest_collection_modifyitems(
    config: pytest.Config, items: list[pytest.Item]
) -> None:
    for item in items:
        item.add_marker(pytest.mark.integration)
