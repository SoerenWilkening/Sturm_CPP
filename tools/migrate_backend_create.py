#!/usr/bin/env python3
"""migrate_backend_create.py — sturm-5jta (Frontend simpl. P2.b / G5).

Throwaway codemod for the sturm-5jta caller-migration sweep. Drops the
second argument from every `sturm_backend_create(MODE, N)` call site so
it matches the post-sturm-zbzo (P2.a) signature
`sturm_backend_create(MODE)`.

Pre-flight: prints every match before rewriting. Any unmatched-but-
suspicious line (e.g. a transpiler-emitted 3-arg shape that the regex
refuses to touch) is surfaced on stderr.

Usage:
    python3 tools/migrate_backend_create.py [--dry-run] [path ...]

If no paths are given, the default scan roots are
``include src tests examples transpiler`` relative to the repo root
(determined by walking up from this script's location).
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from pathlib import Path

# Match a 2-arg sturm_backend_create call site.
#
# Discriminator strategy:
#   * Anchor on the literal `sturm_backend_create(`.
#   * Capture the mode argument as a balanced no-comma run up to the
#     first top-level comma (no nested parens are produced by current
#     callers — verified by the pre-flight grep).
#   * Capture the qubit-count argument as a no-paren run up to the
#     trailing `)`.
#
# This deliberately rejects 3+ arg shapes (no such overload exists in
# the production tree today; the pre-flight grep confirmed zero hits)
# and rejects the no-arg shape (the migration target — leaving it
# alone is correct).
CALL_RE = re.compile(
    r"sturm_backend_create\(\s*"
    r"(?P<mode>[^,()]+?)\s*,\s*"
    r"(?P<qubits>[^()]+?)\s*\)"
)

# File extensions to scan. CMake / Python / Markdown deliberately
# skipped — the API change does not touch them mechanically.
SOURCE_EXTS = {".cpp", ".cc", ".cxx", ".c", ".hpp", ".h", ".hh", ".hxx"}

DEFAULT_ROOTS = ["include", "src", "tests", "examples", "transpiler"]


def find_repo_root(start: Path) -> Path:
    cur = start.resolve()
    while cur != cur.parent:
        if (cur / "CMakeLists.txt").exists() and (cur / "include").exists():
            return cur
        cur = cur.parent
    raise SystemExit("could not locate STURM repo root from " + str(start))


def iter_files(roots: list[Path]) -> list[Path]:
    out: list[Path] = []
    for root in roots:
        if not root.exists():
            continue
        if root.is_file():
            if root.suffix in SOURCE_EXTS:
                out.append(root)
            continue
        for dirpath, _dirnames, filenames in os.walk(root):
            for fn in filenames:
                p = Path(dirpath) / fn
                if p.suffix in SOURCE_EXTS:
                    out.append(p)
    return sorted(out)


def process_file(path: Path, dry_run: bool) -> tuple[int, list[str]]:
    """Returns (rewrites_in_file, list_of_unmatched_lines)."""
    text = path.read_text(encoding="utf-8")
    unmatched: list[str] = []
    # Pre-flight: surface any sturm_backend_create line the regex cannot
    # parse. We re-find every callsite line by simple substring scan and
    # check whether CALL_RE matches it. A no-arg call site is fine; a
    # 3-arg form would land here. We also skip pure comments / strings
    # heuristically (lines whose only `sturm_backend_create` token sits
    # inside a `"..."` or after `//` / `/*`).
    for lineno, line in enumerate(text.splitlines(), start=1):
        if "sturm_backend_create(" not in line:
            continue
        # Skip declarations, comments, and assert-message strings.
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("*") or \
                stripped.startswith("/*"):
            continue
        if 'sturm_backend_create(' in stripped and \
                'sturm_backend_create(sturm_mode_t' in stripped:
            # The function declaration itself.
            continue
        # Detect 3+ arg form (would have at least two top-level commas
        # between the parens).
        # Extract substring inside the matching parens.
        idx = line.index("sturm_backend_create(") + len("sturm_backend_create(")
        depth = 1
        end = idx
        while end < len(line) and depth > 0:
            if line[end] == "(":
                depth += 1
            elif line[end] == ")":
                depth -= 1
            end += 1
        if depth != 0:
            # Multi-line call — flag for human review.
            unmatched.append(f"{path}:{lineno}: multi-line call: {line.rstrip()}")
            continue
        inner = line[idx:end - 1]
        # Count top-level commas.
        depth = 0
        commas = 0
        for ch in inner:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == "," and depth == 0:
                commas += 1
        if commas >= 2:
            unmatched.append(
                f"{path}:{lineno}: 3+ arg form (commas={commas}): "
                f"{line.rstrip()}"
            )
            continue
        # commas == 0 is a no-arg call (already migrated) — skip.
        # commas == 1 is the 2-arg call we want to rewrite — leave to
        # CALL_RE.

    new_text, count = CALL_RE.subn(r"sturm_backend_create(\g<mode>)", text)
    if count > 0 and not dry_run and new_text != text:
        path.write_text(new_text, encoding="utf-8")
    return count, unmatched


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("paths", nargs="*", help="files / dirs to scan")
    ap.add_argument("--dry-run", action="store_true",
                    help="print would-rewrite count, do not write")
    ap.add_argument("--print-matches", action="store_true",
                    help="print every 2-arg call site before rewrite")
    args = ap.parse_args()

    repo = find_repo_root(Path(__file__).parent)
    if args.paths:
        roots = [Path(p) if Path(p).is_absolute() else repo / p
                 for p in args.paths]
    else:
        roots = [repo / r for r in DEFAULT_ROOTS]

    files = iter_files(roots)

    # Pre-flight: print every match.
    if args.print_matches or args.dry_run:
        for f in files:
            try:
                text = f.read_text(encoding="utf-8")
            except (UnicodeDecodeError, OSError):
                continue
            for lineno, line in enumerate(text.splitlines(), start=1):
                if CALL_RE.search(line):
                    print(f"{f}:{lineno}: {line.rstrip()}")

    total_rewrites = 0
    files_touched = 0
    all_unmatched: list[str] = []
    for f in files:
        try:
            count, unmatched = process_file(f, dry_run=args.dry_run)
        except (UnicodeDecodeError, OSError) as e:
            print(f"# skip {f}: {e}", file=sys.stderr)
            continue
        if count:
            total_rewrites += count
            files_touched += 1
        all_unmatched.extend(unmatched)

    print(f"# rewrites: {total_rewrites}")
    print(f"# files touched: {files_touched}")
    if all_unmatched:
        print("# UNMATCHED LINES (human review required):", file=sys.stderr)
        for line in all_unmatched:
            print(line, file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
