#!/usr/bin/env python3
"""extract_md_code_blocks.py — E8.M2 (sturm-8gvh.2).

Extract the FIRST C++ fenced code block from a markdown file. With one
positional arg, print the block to stdout. With two args, diff the
extracted block against the second file's contents and exit non-zero on
drift, so the walkthrough's first cpp block stays byte-for-byte in
lockstep with `tests/external_consumer/main.cpp`. <= 80 LOC, stdlib.

Usage:
    python3 tools/extract_md_code_blocks.py <markdown>
    python3 tools/extract_md_code_blocks.py <markdown> <expected-source>
"""
from __future__ import annotations
import difflib, re, sys
from pathlib import Path

_OPEN = re.compile(r"^\s*```(cpp|c\+\+)\s*$", re.IGNORECASE)
_CLOSE = re.compile(r"^\s*```\s*$")


def extract_first_cpp_block(text: str) -> str | None:
    """Return body of the first ```cpp / ```c++ fenced block, or None."""
    in_block, out = False, []
    for line in text.splitlines(keepends=True):
        if not in_block:
            if _OPEN.match(line.rstrip("\n")):
                in_block = True
            continue
        if _CLOSE.match(line.rstrip("\n")):
            return "".join(out)
        out.append(line)
    return None


def main(argv: list[str]) -> int:
    if len(argv) not in (2, 3):
        print(f"usage: {argv[0]} <markdown> [<expected-source>]", file=sys.stderr)
        return 2
    md_path = Path(argv[1])
    if not md_path.is_file():
        print(f"error: markdown file not found: {md_path}", file=sys.stderr)
        return 2
    block = extract_first_cpp_block(md_path.read_text())
    if block is None:
        print(f"error: no ```cpp / ```c++ fenced block found in {md_path}",
              file=sys.stderr)
        return 1
    if len(argv) == 2:
        sys.stdout.write(block)
        return 0
    src_path = Path(argv[2])
    if not src_path.is_file():
        print(f"error: expected-source not found: {src_path}", file=sys.stderr)
        return 2
    expected = src_path.read_text()
    if block == expected:
        print(f"extract_md_code_blocks: {md_path} first cpp block matches "
              f"{src_path} byte-for-byte.")
        return 0
    sys.stderr.writelines(difflib.unified_diff(
        expected.splitlines(keepends=True), block.splitlines(keepends=True),
        fromfile=str(src_path), tofile=f"{md_path}::first-cpp-block"))
    print(f"\nerror: drift between {md_path} first cpp block and {src_path}.",
          file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
