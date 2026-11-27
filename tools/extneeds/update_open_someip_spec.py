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
import base64
import hashlib
import json
import re
import subprocess
from pathlib import Path
from urllib import error, request

ROOT = Path(__file__).resolve().parents[2]
MODULE_BAZEL = ROOT / "MODULE.bazel"
DOCS_CONF = ROOT / "docs" / "conf.py"
REQUIREMENTS_GENERATOR = ROOT / "tools" / "extneeds" / "generate_spec_requirements.py"
ARCHIVE_BLOCK_PATTERN = re.compile(r"(?ms)^download_archive\(\n.*?^\)$")
ARCHIVE_NAME_PATTERN = re.compile(r'(?m)^\s*name\s*=\s*"open_someip_spec"\s*,?\s*$')
BLOCK_COMMIT_PATTERN = re.compile(r"(?<![0-9A-Za-z])[0-9a-f]{40}(?![0-9A-Za-z])")
INTEGRITY_ATTRIBUTE_PATTERN = re.compile(r'(?m)^(\s*integrity\s*=\s*")[^"]*(")')
INTEGRITY_PATTERN = re.compile(r"sha256-[A-Za-z0-9+/]{43}=")
DOCS_PIN_PATTERN = re.compile(r'(?m)^SPEC_COMMIT = "(?P<commit>[0-9a-f]{40})"$')
REQUIREMENTS_PIN_PATTERN = re.compile(r'(?m)^SPEC_COMMIT = "(?P<commit>[0-9a-f]{40})"$')
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


def _find_archive_block(content: str) -> re.Match[str]:
    matches = [m for m in ARCHIVE_BLOCK_PATTERN.finditer(content) if ARCHIVE_NAME_PATTERN.search(m.group(0))]
    if len(matches) != 1:
        raise ValueError(f"expected exactly one Open SOME/IP archive pin in MODULE.bazel; found {len(matches)}")
    return matches[0]


def extract_module_commit(content: str) -> str:
    """Return the commit used by the archive URL and strip_prefix in MODULE.bazel."""
    commits = set(BLOCK_COMMIT_PATTERN.findall(_find_archive_block(content).group(0)))
    if len(commits) != 1:
        raise ValueError(f"expected one consistent commit in the Open SOME/IP archive pin; found {len(commits)}")
    return commits.pop()


def extract_docs_commit(content: str) -> str:
    matches = list(DOCS_PIN_PATTERN.finditer(content))
    if len(matches) != 1:
        raise ValueError(f"expected exactly one Open SOME/IP commit pin in docs/conf.py; found {len(matches)}")
    return matches[0].group("commit")


def extract_requirements_commit(content: str) -> str:
    matches = list(REQUIREMENTS_PIN_PATTERN.finditer(content))
    if len(matches) != 1:
        raise ValueError(
            f"expected exactly one Open SOME/IP commit pin in generate_spec_requirements.py; found {len(matches)}"
        )
    return matches[0].group("commit")


def check_pins_consistent(module_content: str, docs_content: str, requirements_content: str) -> str:
    """Ensure the Bazel archive and generated source links use the same commit."""
    module_commit = extract_module_commit(module_content)
    docs_commit = extract_docs_commit(docs_content)
    if module_commit != docs_commit:
        raise ValueError(
            f"Open SOME/IP pins differ: MODULE.bazel uses {module_commit}, docs/conf.py uses {docs_commit}"
        )
    requirements_commit = extract_requirements_commit(requirements_content)
    if module_commit != requirements_commit:
        raise ValueError(
            "Open SOME/IP pins differ: MODULE.bazel uses "
            f"{module_commit}, generate_spec_requirements.py uses {requirements_commit}"
        )
    return module_commit


def validate_integrity(integrity: str) -> None:
    if INTEGRITY_PATTERN.fullmatch(integrity) is None:
        raise ValueError("integrity must have the form sha256-<44-character base64 digest>")


def replace_module_pin(content: str, commit: str, integrity: str) -> str:
    """Update the archive commit (URL and strip_prefix) and integrity."""
    validate_commit(commit)
    validate_integrity(integrity)
    match = _find_archive_block(content)
    old_commit = extract_module_commit(content)
    block = match.group(0).replace(old_commit, commit)
    if len(INTEGRITY_ATTRIBUTE_PATTERN.findall(block)) != 1:
        raise ValueError("expected exactly one integrity attribute in the Open SOME/IP archive pin")
    block = INTEGRITY_ATTRIBUTE_PATTERN.sub(lambda m: f"{m.group(1)}{integrity}{m.group(2)}", block)
    return f"{content[: match.start()]}{block}{content[match.end() :]}"


def extract_spec_repo(content: str) -> str:
    matches = list(SPEC_REPO_PATTERN.finditer(content))
    if len(matches) != 1:
        raise ValueError(f"expected exactly one Open SOME/IP repository URL in docs/conf.py; found {len(matches)}")
    return matches[0].group("repo").rstrip("/")


def _fetch_upstream_file(url: str) -> bytes:
    http_request = request.Request(url, headers={"User-Agent": "open-someip-spec-pin-updater"})
    try:
        with request.urlopen(http_request, timeout=60) as response:
            return response.read()
    except (error.URLError, TimeoutError) as exc:
        raise ValueError(f"could not fetch upstream file {url}: {exc}") from exc


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
    source_dir: Path,
) -> None:
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

    for docname, needs_by_line in sorted(lines_by_document.items()):
        source_path = source_dir / f"{docname}.rst"
        source_lines = source_path.read_text(encoding="utf-8").splitlines()
        for line, need_id in sorted(needs_by_line.items()):
            actual_id = _requirement_id_at_line(source_lines, line)
            if actual_id != need_id:
                raise ValueError(f"{source_path}: line {line} maps to {actual_id!r}, expected requirement {need_id!r}")

    print(
        f"Validated {sum(len(needs_by_line) for needs_by_line in lines_by_document.values())} source location(s) "
        f"across {len(lines_by_document)} upstream document(s).",
        flush=True,
    )


def _bazel_source_dir(bazel_args: list[str]) -> Path:
    source_files = subprocess.run(
        ["bazel", "cquery", *bazel_args, "@open_someip_spec//:srcs", "--output=files"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if source_files.returncode:
        raise SystemExit(source_files.returncode)

    source_paths = [Path(line) for line in source_files.stdout.splitlines() if line.endswith(".rst")]
    source_dirs = {path.parent for path in source_paths}
    if len(source_dirs) != 1:
        raise ValueError(f"expected RST files from one Bazel source directory; found {len(source_dirs)}")

    output_base = subprocess.run(
        ["bazel", "info", *bazel_args, "output_base"],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )
    if output_base.returncode:
        raise SystemExit(output_base.returncode)

    source_dir = Path(output_base.stdout.strip()) / next(iter(source_dirs))
    if not source_dir.is_dir():
        raise ValueError(f"Bazel source directory does not exist: {source_dir}")
    return source_dir


def _archive_integrity(commit: str, docs_content: str) -> str:
    archive = _fetch_upstream_file(f"{extract_spec_repo(docs_content)}/archive/{commit}.tar.gz")
    return "sha256-" + base64.b64encode(hashlib.sha256(archive).digest()).decode()


def _update_pins(commit: str, integrity: str | None) -> None:
    module_content = MODULE_BAZEL.read_text(encoding="utf-8")
    docs_content = DOCS_CONF.read_text(encoding="utf-8")
    requirements_content = REQUIREMENTS_GENERATOR.read_text(encoding="utf-8")

    if integrity is None:
        integrity = _archive_integrity(commit, docs_content)
        print(f"Computed archive integrity {integrity}; review it in the MODULE.bazel diff.", flush=True)
    updated_module = replace_module_pin(module_content, commit, integrity)
    updated_docs = replace_commit_pin(docs_content, DOCS_PIN_PATTERN, commit, "docs/conf.py")
    updated_requirements = replace_commit_pin(
        requirements_content, REQUIREMENTS_PIN_PATTERN, commit, "generate_spec_requirements.py"
    )

    MODULE_BAZEL.write_text(updated_module, encoding="utf-8")
    DOCS_CONF.write_text(updated_docs, encoding="utf-8")
    REQUIREMENTS_GENERATOR.write_text(updated_requirements, encoding="utf-8")


def _run_validation(bazel_args: list[str], verify_source_links: bool) -> None:
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
            source_dir = _bazel_source_dir(bazel_args)
            validate_source_locations(metadata["needs"], source_dir)


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
        help="verify generated requirement locations and IDs against the RST files fetched by Bazel",
    )
    parser.add_argument(
        "--integrity",
        metavar="SRI",
        help="sha256-<base64> digest of the archive; skips downloading it to compute the digest",
    )
    args = parser.parse_args()

    try:
        validate_commit(args.commit)
        if args.integrity is not None:
            validate_integrity(args.integrity)
        _update_pins(args.commit, args.integrity)
        check_pins_consistent(
            MODULE_BAZEL.read_text(encoding="utf-8"),
            DOCS_CONF.read_text(encoding="utf-8"),
            REQUIREMENTS_GENERATOR.read_text(encoding="utf-8"),
        )
    except ValueError as error:
        parser.error(str(error))

    print(
        f"Updated the Open SOME/IP pin to {args.commit} in MODULE.bazel, docs/conf.py, "
        "and generate_spec_requirements.py.",
        flush=True,
    )
    try:
        _run_validation(args.bazel_arg, args.verify_source_links)
    except ValueError as error:
        parser.exit(1, f"{parser.prog}: error: {error}\n")


if __name__ == "__main__":
    main()
