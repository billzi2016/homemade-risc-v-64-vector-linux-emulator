#!/usr/bin/env python
"""检查文档站入口命名、相对 symlink 和仓库边界。"""

from __future__ import annotations

import os
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
DOCS_ROOT = REPO_ROOT / "docs-site" / "docs"
LANG_DIRS = {
    "zh": ".zh.md",
    "en": ".md",
}


def fail(message: str) -> None:
    print(f"docs check failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def relative_to_repo(path: Path) -> str:
    return str(path.relative_to(REPO_ROOT))


def ensure_inside_repo(path: Path) -> None:
    resolved = path.resolve()
    try:
        resolved.relative_to(REPO_ROOT)
    except ValueError:
        fail(f"{relative_to_repo(path)} resolves outside repository: {resolved}")


def check_language_tree(language: str, suffix: str) -> None:
    root = DOCS_ROOT / language
    if not root.is_dir():
        fail(f"missing language directory: {relative_to_repo(root)}")

    markdown_entries = sorted(root.rglob("*.md"))
    if not markdown_entries:
        fail(f"no Markdown entries under {relative_to_repo(root)}")

    for entry in markdown_entries:
        name = entry.name
        if language == "zh" and not name.endswith(suffix) and name != "index.md":
            fail(f"Chinese entry must end with .zh.md: {relative_to_repo(entry)}")
        if language == "en" and name.endswith(".zh.md"):
            fail(f"English entry must not use .zh.md suffix: {relative_to_repo(entry)}")
        if language == "en" and name.endswith(".en.md"):
            fail(f"English entry must not use .en.md suffix: {relative_to_repo(entry)}")
        if not entry.is_symlink():
            fail(f"entry must be a relative symlink: {relative_to_repo(entry)}")

        raw_target = os.readlink(entry)
        if Path(raw_target).is_absolute():
            fail(f"symlink target must be relative: {relative_to_repo(entry)} -> {raw_target}")
        ensure_inside_repo(entry)


def check_language_pairs() -> None:
    zh_files = {
        path.relative_to(DOCS_ROOT / "zh").with_name(path.name.removesuffix(".zh.md") + (".md" if path.name.endswith(".zh.md") else ""))
        for path in (DOCS_ROOT / "zh").rglob("*.md")
    }
    en_files = {path.relative_to(DOCS_ROOT / "en") for path in (DOCS_ROOT / "en").rglob("*.md")}
    missing_en = sorted(zh_files - en_files)
    extra_en = sorted(en_files - zh_files)
    if missing_en:
        fail("missing English entries: " + ", ".join(str(path) for path in missing_en))
    if extra_en:
        fail("extra English entries: " + ", ".join(str(path) for path in extra_en))


def main() -> int:
    for language, suffix in LANG_DIRS.items():
        check_language_tree(language, suffix)
    check_language_pairs()
    print("docs check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
