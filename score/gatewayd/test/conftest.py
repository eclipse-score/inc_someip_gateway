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

from pathlib import Path

import pytest


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption("--gatewayd", required=True, type=Path)
    parser.addoption("--someipd", required=True, type=Path)
    parser.addoption("--configuration", required=True, type=Path)
    parser.addoption("--service-instance-manifest", required=True, type=Path)
    parser.addoption("--someipd-service-instance-manifest", required=True, type=Path)
    parser.addoption("--local-service-provider", required=True, type=Path)


@pytest.fixture(autouse=True)
def _test_paths(request: pytest.FixtureRequest) -> None:
    """Make the paths passed on the command line available as attributes of unittest test classes."""
    request.cls.gatewayd = request.config.getoption("--gatewayd")
    request.cls.someipd = request.config.getoption("--someipd")
    request.cls.configuration = request.config.getoption("--configuration")
    request.cls.service_instance_manifest = request.config.getoption("--service-instance-manifest")
    request.cls.someipd_service_instance_manifest = request.config.getoption("--someipd-service-instance-manifest")
    request.cls.local_service_provider = request.config.getoption("--local-service-provider")
