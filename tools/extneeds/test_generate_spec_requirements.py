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
"""Tests for generating mirrored Open SOME/IP requirements."""

import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from generate_spec_requirements import main


class GenerateSpecRequirementsTest(unittest.TestCase):
    def test_preserves_upstream_status(self) -> None:
        needs = {
            "feat_req_someip_11": {
                "id": "feat_req_someip_11",
                "docname": "someip-sd",
                "lineno": 7,
                "reqtype": "Requirement",
                "security": "NO",
                "safety": "QM",
                "status": "invalid",
            }
        }

        with tempfile.TemporaryDirectory() as temporary_directory:
            source = Path(temporary_directory) / "needs.json"
            destination = Path(temporary_directory) / "index.rst"
            source.write_text(json.dumps({"needs": needs}), encoding="utf-8")

            with patch.object(sys, "argv", ["generate_spec_requirements.py", str(source), str(destination)]):
                main()

            generated = destination.read_text(encoding="utf-8")

        self.assertIn("\n   :status: invalid\n", generated)
        self.assertNotIn(":status: valid", generated)


if __name__ == "__main__":
    unittest.main()
