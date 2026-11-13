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
"""Extract metadata-only Open SOME/IP needs from the pinned RST sources.

The upstream specification is licensed Community-Spec-1.0. This script reads
its requirement directives but writes only IDs, attributes, and source
locations; it never copies directive titles or requirement prose.
"""

import argparse
import json
import re
from pathlib import Path
from typing import Any

DIRECTIVE = re.compile(r"^\.\.\s+feat_req::")
OPTION = re.compile(r"^\s+:([a-z][a-z0-9_-]*):\s*(.*)$")
NEED_ID = re.compile(r"feat_req_(someip[a-z]*)_(\d+)")
REQUIRED_ATTRIBUTES = ("id", "reqtype", "security", "safety", "status")
SUPPORTED_REQTYPES = {"Requirement", "Information"}


def _requirements_from_source(source: Path) -> list[dict[str, Any]]:
    lines = source.read_text(encoding="utf-8").splitlines()
    needs: list[dict[str, Any]] = []

    for index, line in enumerate(lines):
        if not DIRECTIVE.match(line):
            continue

        attributes: dict[str, str] = {}
        for option_line in lines[index + 1 :]:
            if not option_line.strip():
                break
            match = OPTION.match(option_line)
            if match is None:
                break
            name, value = match.groups()
            if name in REQUIRED_ATTRIBUTES:
                if name in attributes:
                    raise ValueError(f"{source}:{index + 1}: duplicate :{name}: option")
                attributes[name] = value.strip()

        missing = [name for name in REQUIRED_ATTRIBUTES if not attributes.get(name)]
        if missing:
            raise ValueError(f"{source}:{index + 1}: missing required options: {', '.join(missing)}")

        if attributes["reqtype"] not in SUPPORTED_REQTYPES:
            raise ValueError(f"{source}:{index + 1}: unexpected reqtype {attributes['reqtype']!r}")

        need_id = attributes["id"]
        if NEED_ID.fullmatch(need_id) is None:
            raise ValueError(f"{source}:{index + 1}: unexpected requirement ID {need_id!r}")

        needs.append(
            {
                "id": need_id,
                "type": "feat_req",
                "docname": source.stem,
                "lineno": index + 1,
                "reqtype": attributes["reqtype"],
                "security": attributes["security"],
                "safety": attributes["safety"],
                "status": attributes["status"],
            }
        )

    return needs


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path, help="generated metadata JSON")
    parser.add_argument("sources", nargs="+", type=Path, help="pinned upstream RST sources")
    args = parser.parse_args()

    needs: dict[str, dict[str, Any]] = {}
    for source in sorted(args.sources, key=lambda path: path.name):
        for need in _requirements_from_source(source):
            need_id = need["id"]
            if need_id in needs:
                raise SystemExit(f"duplicate requirement ID {need_id!r} in {source}")
            needs[need_id] = need

    if not needs:
        raise SystemExit("no feat_req directives found in the supplied RST sources")

    args.output.write_text(json.dumps({"needs": needs}, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(f"generated metadata for {len(needs)} requirements")


if __name__ == "__main__":
    main()
