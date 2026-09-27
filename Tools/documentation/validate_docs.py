#!/usr/bin/env python3
"""Validate the repository documentation topology and relative links."""

from __future__ import annotations

import re
import sys
from pathlib import Path
from urllib.parse import unquote, urlsplit


LINK_RE = re.compile(r"\[[^\]]+\]\(([^)]+)\)")
H1_RE = re.compile(r"^# ", re.MULTILINE)


def main() -> int:
    repository = Path(__file__).resolve().parents[2]
    docs = repository / "docs"
    errors: list[str] = []

    required = [
        docs / "README.md",
        docs / "project" / "CURRENT_STATUS.md",
        docs / "project" / "ARCHITECTURE.md",
        docs / "project" / "IMPLEMENTATION_ROADMAP.md",
        docs / "project" / "DOCUMENTATION_POLICY.md",
        docs / "equal-party-quest-sync" / "README.md",
        docs / "equal-party-quest-sync" / "MASTER-HANDOFF.md",
        docs / "equal-party-quest-sync" / "tasks" / "README.md",
    ]

    for path in required:
        if not path.is_file():
            errors.append(f"missing required document: {path.relative_to(repository)}")

    task_root = docs / "equal-party-quest-sync" / "tasks"
    for number in range(1, 16):
        matches = list(task_root.glob(f"{number:02d}-*.md"))
        if len(matches) != 1:
            errors.append(
                f"expected one Task {number:02d} specification, found {len(matches)}"
            )
            continue

        text = matches[0].read_text(encoding="utf-8")
        if "../MASTER-HANDOFF.md" not in text:
            errors.append(
                f"task does not reference the master handoff: "
                f"{matches[0].relative_to(repository)}"
            )

    markdown_files = sorted(docs.rglob("*.md"))
    for path in markdown_files:
        text = path.read_text(encoding="utf-8")
        h1_count = len(H1_RE.findall(text))
        if h1_count != 1:
            errors.append(
                f"expected one top-level heading in {path.relative_to(repository)}, "
                f"found {h1_count}"
            )

        for match in LINK_RE.finditer(text):
            raw_target = match.group(1).strip().strip("<>")
            parsed = urlsplit(raw_target)
            if parsed.scheme or raw_target.startswith("#"):
                continue

            relative = unquote(parsed.path)
            if not relative:
                continue

            target = (path.parent / relative).resolve()
            if not target.exists():
                errors.append(
                    f"broken link in {path.relative_to(repository)}: {raw_target}"
                )

    if errors:
        for error in errors:
            print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(
        f"Documentation validation passed: {len(markdown_files)} Markdown files, "
        "15 ordered P0 task specifications."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
