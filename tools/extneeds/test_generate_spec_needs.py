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
"""Tests for metadata extraction and upstream pin update helpers."""

import tempfile
import unittest
from pathlib import Path

from generate_spec_needs import _requirements_from_source


class RequirementsFromSourceTest(unittest.TestCase):
    def test_extracts_only_metadata_and_directive_line(self) -> None:
        content = """.. feat_req:: Upstream title must not be copied
   :id: feat_req_someip_11
   :reqtype: Requirement
   :security: TBD
   :safety: QM
   :status: valid
   :collapse: true

   Upstream requirement wording must not be copied.
"""
        with tempfile.TemporaryDirectory() as temporary_directory:
            source = Path(temporary_directory) / "someip-sd.rst"
            source.write_text(content, encoding="utf-8")

            needs = _requirements_from_source(source)

        self.assertEqual(
            [
                {
                    "id": "feat_req_someip_11",
                    "type": "feat_req",
                    "docname": "someip-sd",
                    "lineno": 1,
                    "reqtype": "Requirement",
                    "security": "TBD",
                    "safety": "QM",
                    "status": "valid",
                }
            ],
            needs,
        )

    def test_rejects_missing_required_metadata(self) -> None:
        content = """.. feat_req:: Title
   :id: feat_req_someip_11
   :reqtype: Requirement

   Requirement wording.
"""
        with tempfile.TemporaryDirectory() as temporary_directory:
            source = Path(temporary_directory) / "someip-sd.rst"
            source.write_text(content, encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "missing required options"):
                _requirements_from_source(source)

    def test_rejects_unknown_requirement_type(self) -> None:
        content = """.. feat_req:: Title
   :id: feat_req_someip_11
   :reqtype: Unknown
   :security: TBD
   :safety: QM
   :status: valid
"""
        with tempfile.TemporaryDirectory() as temporary_directory:
            source = Path(temporary_directory) / "someip-sd.rst"
            source.write_text(content, encoding="utf-8")

            with self.assertRaisesRegex(ValueError, "unexpected reqtype"):
                _requirements_from_source(source)


if __name__ == "__main__":
    unittest.main()
