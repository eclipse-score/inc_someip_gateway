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
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE_BAZEL = ROOT / "MODULE.bazel"
DOCS_CONF = ROOT / "docs" / "conf.py"
MODULE_PIN_PATTERN = re.compile(
    r'(?m)^git_repository\(\n    name = "open_someip_spec",\n'
    r'(?:(?!^git_repository\().)*?^    commit = "(?P<commit>[0-9a-f]{40})",$',
    re.DOTALL,
)
DOCS_PIN_PATTERN = re.compile(r'(?m)^SPEC_COMMIT = "(?P<commit>[0-9a-f]{40})"$')
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")


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


def _update_pins(commit: str) -> None:
    module_content = MODULE_BAZEL.read_text(encoding="utf-8")
    docs_content = DOCS_CONF.read_text(encoding="utf-8")

    updated_module = replace_commit_pin(module_content, MODULE_PIN_PATTERN, commit, "MODULE.bazel")
    updated_docs = replace_commit_pin(docs_content, DOCS_PIN_PATTERN, commit, "docs/conf.py")

    MODULE_BAZEL.write_text(updated_module, encoding="utf-8")
    DOCS_CONF.write_text(updated_docs, encoding="utf-8")


def _run_validation(bazel_args: list[str]) -> None:
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
    args = parser.parse_args()

    try:
        validate_commit(args.commit)
        _update_pins(args.commit)
    except ValueError as error:
        parser.error(str(error))

    print(f"Updated the Open SOME/IP pin to {args.commit} in MODULE.bazel and docs/conf.py.", flush=True)
    _run_validation(args.bazel_arg)


if __name__ == "__main__":
    main()
