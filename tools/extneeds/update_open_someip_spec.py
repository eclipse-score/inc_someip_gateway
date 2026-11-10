#!/usr/bin/env python3
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
"""Update the pinned Open SOME/IP commit and validate the documentation build."""

import argparse
import json
import re
import subprocess
from pathlib import Path
from typing import Callable
from urllib import error, parse, request

ROOT = Path(__file__).resolve().parents[2]
MODULE_BAZEL = ROOT / "MODULE.bazel"
DOCS_CONF = ROOT / "docs" / "conf.py"
MODULE_PIN_PATTERN = re.compile(
    r'(?m)^git_repository\(\n    name = "open_someip_spec",\n'
    r'(?:(?!^git_repository\().)*?^    commit = "(?P<commit>[0-9a-f]{40})",$',
    re.DOTALL,
)
DOCS_PIN_PATTERN = re.compile(r'(?m)^SPEC_COMMIT = "(?P<commit>[0-9a-f]{40})"$')
SPEC_REPO_PATTERN = re.compile(r'(?m)^SPEC_REPO = "(?P<repo>https://github\.com/[^\"]+)"$')
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")
SOURCE_DOC_PATTERN = re.compile(r"[a-z0-9-]+")


def validate_commit(commit: str) -> None:
    if COMMIT_PATTERN.fullmatch(commit) is None:
        raise ValueError("commit must be a full 40-character lowercase Git SHA")


def replace_commit_pin(content: str, pattern: re.Pattern[str], commit: str, file_label: str) -> str:
    matches = list(pattern.finditer(content))
    if len(matches) != 1:
        raise ValueError(f"expected exactly one Open SOME/IP commit pin in {file_label}; found {len(matches)}")

    match = matches[0]
    start, end = match.span("commit")
    return f"{content[:start]}{commit}{content[end:]}"


def extract_spec_repo(content: str) -> str:
    matches = list(SPEC_REPO_PATTERN.finditer(content))
    if len(matches) != 1:
        raise ValueError(f"expected exactly one Open SOME/IP repository URL in docs/conf.py; found {len(matches)}")
    return matches[0].group("repo").rstrip("/")


def _fetch_source_file(url: str) -> bytes:
    http_request = request.Request(url, headers={"User-Agent": "open-someip-source-link-check"})
    try:
        with request.urlopen(http_request, timeout=15) as response:
            return response.read()
    except (error.URLError, TimeoutError) as exc:
        raise ValueError(f"could not fetch upstream source file {url}: {exc}") from exc


def _requirement_id_at_line(source_lines: list[str], line: int) -> str | None:
    if line > len(source_lines) or re.match(r"^\.\.\s+feat_req::", source_lines[line - 1]) is None:
        return None

    for option_line in source_lines[line:]:
        if not option_line.strip():
            break
        match = re.match(r"^\s+:id:\s*(\S+)\s*$", option_line)
        if match is not None:
            return match.group(1)
    return None


def validate_source_locations(
    needs: dict[str, dict[str, object]],
    repo: str,
    commit: str,
    fetch_source: Callable[[str], bytes] | None = None,
) -> None:
    validate_commit(commit)
    if not needs:
        raise ValueError("generated metadata contains no source requirements to validate")

    lines_by_document: dict[str, dict[int, str]] = {}
    for need_id, need in needs.items():
        docname = need.get("docname")
        line = need.get("lineno")
        if not isinstance(docname, str) or SOURCE_DOC_PATTERN.fullmatch(docname) is None:
            raise ValueError(f"{need_id}: invalid source document name {docname!r}")
        if not isinstance(line, int) or isinstance(line, bool) or line < 1:
            raise ValueError(f"{need_id}: invalid source line number {line!r}")
        lines_by_document.setdefault(docname, {})[line] = need_id

    repo_path = repo.removeprefix("https://github.com/").strip("/")
    if len(repo_path.split("/")) != 2:
        raise ValueError(f"expected a GitHub owner/repository URL, got {repo!r}")

    fetch = fetch_source or _fetch_source_file
    for docname, needs_by_line in sorted(lines_by_document.items()):
        filename = parse.quote(f"{docname}.rst", safe="-")
        url = f"https://raw.githubusercontent.com/{repo_path}/{commit}/src/{filename}"
        source_lines = fetch(url).decode("utf-8").splitlines()
        for line, need_id in sorted(needs_by_line.items()):
            actual_id = _requirement_id_at_line(source_lines, line)
            if actual_id != need_id:
                raise ValueError(
                    f"{url}: line {line} maps to {actual_id!r}, expected requirement {need_id!r}"
                )

    print(
        f"Validated {sum(len(needs_by_line) for needs_by_line in lines_by_document.values())} source location(s) "
        f"across {len(lines_by_document)} upstream document(s).",
        flush=True,
    )


def _update_pins(commit: str) -> None:
    module_content = MODULE_BAZEL.read_text(encoding="utf-8")
    docs_content = DOCS_CONF.read_text(encoding="utf-8")

    updated_module = replace_commit_pin(module_content, MODULE_PIN_PATTERN, commit, "MODULE.bazel")
    updated_docs = replace_commit_pin(docs_content, DOCS_PIN_PATTERN, commit, "docs/conf.py")

    MODULE_BAZEL.write_text(updated_module, encoding="utf-8")
    DOCS_CONF.write_text(updated_docs, encoding="utf-8")


def _run_validation(bazel_args: list[str], commit: str, verify_source_links: bool) -> None:
    commands = (
        (
            "Generate metadata and requirements",
            [
                "bazel",
                "build",
                *bazel_args,
                "//docs_extspec:spec_needs_json",
                "//docs_extspec:spec_requirements_rst",
            ],
        ),
        ("Build documentation", ["bazel", "run", *bazel_args, "//:docs"]),
        ("Check documentation", ["bazel", "run", *bazel_args, "//:docs_check"]),
    )

    for description, command in commands:
        print(f"\n== {description} ==", flush=True)
        result = subprocess.run(command, cwd=ROOT, check=False)
        if result.returncode:
            raise SystemExit(result.returncode)

        if description == "Generate metadata and requirements" and verify_source_links:
            bazel_bin = subprocess.run(
                ["bazel", "info", *bazel_args, "bazel-bin"],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )
            if bazel_bin.returncode:
                raise SystemExit(bazel_bin.returncode)
            metadata_path = Path(bazel_bin.stdout.strip()) / "docs_extspec" / "needs.json"
            metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
            repo = extract_spec_repo(DOCS_CONF.read_text(encoding="utf-8"))
            validate_source_locations(metadata["needs"], repo, commit)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("commit", help="full upstream Open SOME/IP Git commit SHA")
    parser.add_argument(
        "--bazel-arg",
        action="append",
        default=[],
        metavar="ARG",
        help="extra Bazel option (repeat for multiple options)",
    )
    parser.add_argument(
        "--verify-source-links",
        action="store_true",
        help="fetch pinned raw RST files and verify generated requirement locations and IDs (requires network access)",
    )
    args = parser.parse_args()

    try:
        validate_commit(args.commit)
        _update_pins(args.commit)
    except ValueError as error:
        parser.error(str(error))

    print(f"Updated the Open SOME/IP pin to {args.commit} in MODULE.bazel and docs/conf.py.", flush=True)
    _run_validation(args.bazel_arg, args.commit, args.verify_source_links)


if __name__ == "__main__":
    main()
