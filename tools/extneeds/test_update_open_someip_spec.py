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
from urllib.error import HTTPError
from unittest.mock import patch

from update_open_someip_spec import (
    DOCS_PIN_PATTERN,
    MODULE_PIN_PATTERN,
    _fetch_source_file,
    extract_spec_repo,
    replace_commit_pin,
    validate_source_locations,
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

    def test_extracts_the_source_repository_from_docs_config(self) -> None:
        self.assertEqual(
            "https://github.com/example/spec", extract_spec_repo('SPEC_REPO = "https://github.com/example/spec"\n')
        )

    def test_validates_requirement_locations_and_fetches_each_document_once(self) -> None:
        commit = "a" * 40
        fetched_urls: list[str] = []

        def fetch_source(url: str) -> bytes:
            fetched_urls.append(url)
            return (
                b".. feat_req:: Requirement A\n"
                b"    :id: feat_req_someip_1\n"
                b"    :reqtype: Requirement\n"
                b"\n"
                b".. feat_req:: Requirement B\n"
                b"    :id: feat_req_someip_2\n"
                b"    :reqtype: Requirement\n"
            )

        needs = {
            "feat_req_someip_1": {"id": "feat_req_someip_1", "docname": "someip-rpc", "lineno": 1},
            "feat_req_someip_2": {"id": "feat_req_someip_2", "docname": "someip-rpc", "lineno": 5},
        }
        validate_source_locations(needs, "https://github.com/example/spec", commit, fetch_source)

        self.assertEqual(
            [f"https://raw.githubusercontent.com/example/spec/{commit}/src/someip-rpc.rst"],
            fetched_urls,
        )

    def test_rejects_a_mismatched_requirement_id(self) -> None:
        needs = {"feat_req_someip_1": {"id": "feat_req_someip_1", "docname": "someip-rpc", "lineno": 1}}
        source = b".. feat_req:: Requirement\n    :id: feat_req_someip_2\n\n"
        with self.assertRaisesRegex(ValueError, "line 1 maps to.*feat_req_someip_1"):
            validate_source_locations(needs, "https://github.com/example/spec", "a" * 40, lambda _: source)

    def test_reports_http_errors_for_source_files(self) -> None:
        url = "https://raw.githubusercontent.com/example/spec/" + "a" * 40 + "/src/someip-rpc.rst"
        http_error = HTTPError(url, 404, "Not Found", {}, None)
        with patch("update_open_someip_spec.request.urlopen", side_effect=http_error):
            with self.assertRaisesRegex(ValueError, "could not fetch upstream source file"):
                _fetch_source_file(url)


if __name__ == "__main__":
    unittest.main()
