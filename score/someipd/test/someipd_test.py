# *******************************************************************************
# Copyright (c) 2026 Contributors to the Eclipse Foundation
#
# See the NOTICE file(s) distributed with this work for additional
# information regarding copyright ownership.
#
# This program and the accompanying materials are made available under the
# terms of the Apache License Version 2.0 which is available at
#
# SPDX-License-Identifier: Apache-2.0
# *******************************************************************************

from pathlib import Path
import subprocess
import sys
import unittest


class SomeipdTest(unittest.TestCase):
    def test_help_returns_success_and_prints_usage(self) -> None:
        binary = Path(sys.argv[1])
        result = subprocess.run([binary, "--help"], capture_output=True, check=False, text=True)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Syntax: someipd -h/--help", result.stdout)


if __name__ == "__main__":
    _ = unittest.main(argv=[sys.argv[0]])
