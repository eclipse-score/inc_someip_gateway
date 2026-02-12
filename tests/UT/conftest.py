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
import time

import paramiko
import pytest


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
    last_error = None
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
            last_error = e
            if attempt < max_retries:
                time.sleep(2)

    pytest.fail(f"Cannot connect to QEMU at {SSH_HOST}:{SSH_PORT} - is QEMU running? Error: {last_error}")
