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

"""Self-contained flamegraph rendering: `perf script` output -> folded stacks -> SVG.

Reimplements the two essential steps of Brendan Gregg's FlameGraph scripts
(stackcollapse-perf.pl / flamegraph.pl) in Python, so profiling does not depend on cloning that
repository.
"""

from __future__ import annotations

import html
import zlib
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

ROW_HEIGHT = 18


def collapse_perf_script(script_text: str) -> Counter[str]:
    """Folds `perf script` text output into semicolon-joined `root;...;leaf` stack counts."""
    folded: Counter[str] = Counter()
    frames: list[str] = []
    for line in script_text.splitlines():
        if line.strip() == "":
            if frames:
                folded[";".join(reversed(frames))] += 1
                frames = []
            continue
        if not line[0].isspace():
            continue  # sample header line (comm/pid/timestamp), not a stack frame
        stripped = line.strip()
        parts = stripped.split(None, 1)
        if len(parts) < 2:
            continue
        rest = parts[1]
        dso_start = rest.rfind(" (")
        symbol = rest[:dso_start] if dso_start != -1 else rest
        symbol = symbol.split("+")[0].strip()
        frames.append(symbol or "[unknown]")
    if frames:
        folded[";".join(reversed(frames))] += 1
    return folded


@dataclass
class _Node:
    name: str
    value: int = 0
    children: dict[str, "_Node"] = field(default_factory=dict)


def _build_tree(folded: Counter[str]) -> _Node:
    root = _Node("root")
    for stack, count in folded.items():
        root.value += count
        node = root
        for frame in stack.split(";"):
            node = node.children.setdefault(frame, _Node(frame))
            node.value += count
    return root


def _max_depth(node: _Node) -> int:
    if not node.children:
        return 0
    return 1 + max(_max_depth(child) for child in node.children.values())


def _color_for(name: str) -> str:
    # Warm palette (Brendan Gregg's default "hot" scheme), hue picked from a hash of the name.
    hue = zlib.crc32(name.encode()) % 40
    return f"hsl({hue}, 80%, 55%)"


def _truncate(name: str, width: float) -> str:
    max_chars = max(0, int(width / 6.5))
    return name if len(name) <= max_chars else name[: max(0, max_chars - 1)] + "\u2026"


def _render(
    node: _Node, depth: int, x: float, width: float, top_offset: int, out: list[str]
) -> None:
    if width <= 0.3:
        return
    y = top_offset + depth * ROW_HEIGHT
    rect = (
        f'<rect x="{x:.2f}" y="{y}" width="{width:.2f}" height="{ROW_HEIGHT}" '
        f'fill="{_color_for(node.name)}" stroke="white" stroke-width="0.5">'
        f"<title>{html.escape(node.name)} ({node.value} samples)</title></rect>"
    )
    out.append(rect)
    if width > 25:
        label = (
            f'<text x="{x + 2:.2f}" y="{y + 13}" font-size="11" font-family="monospace">'
            f"{html.escape(_truncate(node.name, width))}</text>"
        )
        out.append(label)
    child_x = x
    for child in sorted(node.children.values(), key=lambda n: n.name):
        child_width = width * child.value / node.value if node.value else 0.0
        _render(child, depth + 1, child_x, child_width, top_offset, out)
        child_x += child_width


def write_flamegraph_svg(
    folded: Counter[str], svg_path: Path, title: str, width_px: int = 1800
) -> None:
    """Renders folded stack counts as an interactive-hover SVG flamegraph."""
    root = _build_tree(folded)
    top_offset = 24
    if root.value == 0:
        empty_svg = (
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width_px}" '
            f'height="{top_offset + ROW_HEIGHT}">'
            f'<text x="10" y="16" font-size="14" font-family="monospace">'
            f"{html.escape(title)}: no samples captured</text></svg>"
        )
        _ = svg_path.write_text(empty_svg)
        return

    height = top_offset + (_max_depth(root) + 1) * ROW_HEIGHT + 10
    title_line = (
        f'<text x="10" y="16" font-size="14" font-family="monospace">'
        f"{html.escape(title)} ({root.value} samples)</text>"
    )
    out = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width_px}" height="{height}">',
        title_line,
    ]
    child_x = 0.0
    for child in sorted(root.children.values(), key=lambda n: n.name):
        child_width = width_px * child.value / root.value
        _render(child, 0, child_x, child_width, top_offset, out)
        child_x += child_width
    out.append("</svg>")
    _ = svg_path.write_text("\n".join(out))
