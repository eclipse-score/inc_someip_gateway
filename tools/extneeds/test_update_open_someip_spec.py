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
"""Tests for updating the pinned Open SOME/IP commit."""

import unittest

from update_open_someip_spec import (
    DOCS_PIN_PATTERN,
    MODULE_PIN_PATTERN,
    replace_commit_pin,
    validate_commit,
)


class OpenSomeipPinUpdateTest(unittest.TestCase):
    def test_replaces_module_and_docs_pins(self) -> None:
        commit = "b" * 40
        old_commit = "a" * 40
        module = f'git_repository(\n    name = "open_someip_spec",\n    commit = "{old_commit}",\n)'
        docs = f'SPEC_COMMIT = "{old_commit}"\n'

        self.assertIn(commit, replace_commit_pin(module, MODULE_PIN_PATTERN, commit, "MODULE.bazel"))
        self.assertEqual(
            f'SPEC_COMMIT = "{commit}"\n', replace_commit_pin(docs, DOCS_PIN_PATTERN, commit, "docs/conf.py")
        )

    def test_rejects_non_full_commit(self) -> None:
        with self.assertRaisesRegex(ValueError, "full 40-character"):
            validate_commit("deadbeef")

    def test_rejects_ambiguous_pin(self) -> None:
        content = 'SPEC_COMMIT = "' + "a" * 40 + '"\nSPEC_COMMIT = "' + "b" * 40 + '"\n'
        with self.assertRaisesRegex(ValueError, "found 2"):
            replace_commit_pin(content, DOCS_PIN_PATTERN, "c" * 40, "docs/conf.py")


if __name__ == "__main__":
    unittest.main()
