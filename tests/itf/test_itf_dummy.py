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
"""Dummy ITF test to verify the Integration Test Framework is working."""


def test_itf_is_working():
    """A simple assertion-true test to verify ITF integration."""
    assert True


def test_basic_arithmetic():
    """Verify basic arithmetic works within the ITF test runner."""
    assert 1 + 1 == 2


def test_string_operations():
    """Verify string operations work within the ITF test runner."""
    assert "someip_gateway" in "score_inc_someip_gateway"
