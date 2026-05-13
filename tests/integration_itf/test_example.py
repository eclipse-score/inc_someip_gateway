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

from score.itf.plugins.core import requires_capabilities


@requires_capabilities("exec")
def test_docker_exec(target):
    exit_code, output = target.execute("echo 'Hello from target!'")
    assert exit_code == 0
    assert b"Hello from target!" in output
