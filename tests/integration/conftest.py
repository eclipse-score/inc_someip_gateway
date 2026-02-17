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
# SPDX-FileCopyrightText: 2025, 2026 Contributors to the Eclipse Foundation
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************

"""
Pytest configuration and fixtures for integration tests.
"""

import pytest
from typing import Generator
import subprocess
from pathlib import Path


@pytest.fixture(scope="class")
def someipd_config() -> Path:
    """Provide SOME/IP configuration parameters."""
    # TODO: We probably should have a way to add more configuration from fixtures in the tests
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


# Pytest configuration
def pytest_configure(config: pytest.Config) -> None:
    """Pytest configuration hook."""
    config.addinivalue_line("markers", "integration: mark test as integration test")
    config.addinivalue_line("markers", "slow: mark test as slow running")
    config.addinivalue_line("markers", "network: mark test as requiring network access")


def pytest_collection_modifyitems(
    config: pytest.Config, items: list[pytest.Item]
) -> None:
    """Modify test items during collection."""
    for item in items:
        # Auto-mark all tests in this directory as integration tests
        item.add_marker(pytest.mark.integration)
