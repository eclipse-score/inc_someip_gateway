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

import os
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from urllib.error import HTTPError
from unittest.mock import patch

from update_open_someip_spec import (
    DOCS_PIN_PATTERN,
    _bazel_source_dir,
    _fetch_upstream_file,
    check_pins_consistent,
    extract_module_commit,
    extract_spec_repo,
    replace_commit_pin,
    replace_module_pin,
    validate_commit,
    validate_integrity,
    validate_source_locations,
)

INTEGRITY = "sha256-" + "A" * 43 + "="


class OpenSomeipPinUpdateTest(unittest.TestCase):
    def test_replaces_module_and_docs_pins(self) -> None:
        commit = "b" * 40
        old_commit = "a" * 40
        module = (
            f'download_archive(\n    name = "open_someip_spec",\n    integrity = "sha256-old",\n'
            f'    strip_prefix = "open-someip-spec-{old_commit}",\n    urls = ["/archive/{old_commit}.tar.gz"],\n)'
        )
        docs = f'SPEC_COMMIT = "{old_commit}"\n'

        self.assertEqual(
            module.replace(old_commit, commit).replace("sha256-old", INTEGRITY),
            replace_module_pin(module, commit, INTEGRITY),
        )
        self.assertEqual(
            f'SPEC_COMMIT = "{commit}"\n', replace_commit_pin(docs, DOCS_PIN_PATTERN, commit, "docs/conf.py")
        )

    def test_module_pin_does_not_depend_on_attribute_order_or_other_archives(self) -> None:
        old_commit = "a" * 40
        commit = "b" * 40
        other = 'download_archive(\n    name = "other",\n    integrity = "sha256-other",\n)\n'
        module = (
            f"{other}"
            f'download_archive(\n    urls = [\n        "https://example.com/{old_commit}.tar.gz",\n    ],\n'
            f'    strip_prefix = "open-someip-spec-{old_commit}",\n'
            f'    integrity = "sha256-old",\n    name = "open_someip_spec",\n)\n'
        )

        self.assertEqual(old_commit, extract_module_commit(module))
        updated = replace_module_pin(module, commit, INTEGRITY)
        self.assertEqual(commit, extract_module_commit(updated))
        self.assertIn(other, updated)
        self.assertIn(f'integrity = "{INTEGRITY}"', updated)

    def test_rejects_module_pin_with_inconsistent_commits(self) -> None:
        module = (
            'download_archive(\n    name = "open_someip_spec",\n    integrity = "sha256-old",\n'
            f'    strip_prefix = "open-someip-spec-{"a" * 40}",\n    urls = ["/archive/{"b" * 40}.tar.gz"],\n)'
        )
        with self.assertRaisesRegex(ValueError, "consistent commit"):
            extract_module_commit(module)

    def test_detects_differing_module_and_docs_pins(self) -> None:
        module = f'download_archive(\n    name = "open_someip_spec",\n    urls = ["/archive/{"a" * 40}.tar.gz"],\n)'
        self.assertEqual("a" * 40, check_pins_consistent(module, f'SPEC_COMMIT = "{"a" * 40}"\n'))
        with self.assertRaisesRegex(ValueError, "pins differ"):
            check_pins_consistent(module, f'SPEC_COMMIT = "{"b" * 40}"\n')

    def test_rejects_malformed_integrity(self) -> None:
        validate_integrity(INTEGRITY)
        with self.assertRaisesRegex(ValueError, "integrity must have the form"):
            validate_integrity("sha256-short")

    def test_repository_pins_are_consistent(self) -> None:
        root = Path(os.environ["TEST_SRCDIR"]) / os.environ.get("TEST_WORKSPACE", "_main")
        check_pins_consistent(
            (root / "MODULE.bazel").read_text(encoding="utf-8"),
            (root / "docs" / "conf.py").read_text(encoding="utf-8"),
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

    def test_validates_requirement_locations_from_local_source_files(self) -> None:
        needs = {
            "feat_req_someip_1": {"id": "feat_req_someip_1", "docname": "someip-rpc", "lineno": 1},
            "feat_req_someip_2": {"id": "feat_req_someip_2", "docname": "someip-rpc", "lineno": 5},
        }
        source = (
            ".. feat_req:: Requirement A\n"
            "    :id: feat_req_someip_1\n"
            "    :reqtype: Requirement\n"
            "\n"
            ".. feat_req:: Requirement B\n"
            "    :id: feat_req_someip_2\n"
            "    :reqtype: Requirement\n"
        )
        with TemporaryDirectory() as temp_dir:
            source_dir = Path(temp_dir)
            (source_dir / "someip-rpc.rst").write_text(source, encoding="utf-8")
            with patch("update_open_someip_spec._fetch_upstream_file") as fetch_source:
                validate_source_locations(needs, source_dir)
                fetch_source.assert_not_called()

    def test_rejects_a_mismatched_requirement_id(self) -> None:
        needs = {"feat_req_someip_1": {"id": "feat_req_someip_1", "docname": "someip-rpc", "lineno": 1}}
        with TemporaryDirectory() as temp_dir:
            source_dir = Path(temp_dir)
            (source_dir / "someip-rpc.rst").write_text(
                ".. feat_req:: Requirement\n    :id: feat_req_someip_2\n\n", encoding="utf-8"
            )
            with self.assertRaisesRegex(ValueError, "line 1 maps to.*feat_req_someip_1"):
                validate_source_locations(needs, source_dir)

    def test_resolves_local_source_directory_from_bazel_query_output(self) -> None:
        with TemporaryDirectory() as output_base:
            source_path = Path("external/+_repo_rules+open_someip_spec/src/someip-rpc.rst")
            (Path(output_base) / source_path.parent).mkdir(parents=True)
            with patch(
                "update_open_someip_spec.subprocess.run",
                side_effect=[
                    SimpleNamespace(returncode=0, stdout=f"{source_path}\n"),
                    SimpleNamespace(returncode=0, stdout=f"{output_base}\n"),
                ],
            ) as run:
                self.assertEqual(
                    Path(output_base) / source_path.parent,
                    _bazel_source_dir(["--config=test"]),
                )

            self.assertEqual(2, run.call_count)

    def test_rejects_a_missing_bazel_source_directory(self) -> None:
        with TemporaryDirectory() as output_base:
            with patch(
                "update_open_someip_spec.subprocess.run",
                side_effect=[
                    SimpleNamespace(returncode=0, stdout="external/missing/src/a.rst\n"),
                    SimpleNamespace(returncode=0, stdout=f"{output_base}\n"),
                ],
            ):
                with self.assertRaisesRegex(ValueError, "does not exist"):
                    _bazel_source_dir([])

    def test_reports_http_errors_for_archive_files(self) -> None:
        url = "https://github.com/example/spec/archive/" + "a" * 40 + ".tar.gz"
        http_error = HTTPError(url, 404, "Not Found", {}, None)
        with patch("update_open_someip_spec.request.urlopen", side_effect=http_error):
            with self.assertRaisesRegex(ValueError, "could not fetch upstream file"):
                _fetch_upstream_file(url)


if __name__ == "__main__":
    unittest.main()
