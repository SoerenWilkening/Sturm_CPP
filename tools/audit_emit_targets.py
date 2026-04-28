#!/usr/bin/env python3
"""audit_emit_targets.py — STURM Epic E1.M1 (sturm-xq05.1).

Walk transpiler/src/matcher_*.cpp and *_emitter.cpp; regex-extract emitted
symbol names from string literals (the C++ identifiers the transpiler splices
into rewritten user code); grep include/sturm/** to locate the header that
defines each symbol; print a TSV with three columns:

    matcher_file<TAB>emitted_symbol<TAB>defining_header

The TSV is the source-of-truth side-table for E1.M2's classification doc and
E1.M3's drift-check CI step. Output is sorted on (matcher_file, emitted_symbol)
so byte-identical reruns are stable across machines.

LOC budget: <=200.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Iterable

# ---------------------------------------------------------------------------
# Comment-stripping. The clang sources are heavy on `// sturm::...` design
# notes; emitting those as fake "emit targets" would drown the real signal.
# We strip line- and block-comments before regex extraction. Naive but
# adequate — string literals containing `//` are vanishingly rare in our
# emitter helpers (the few that exist do not embed `sturm::` patterns).
# ---------------------------------------------------------------------------
_BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.DOTALL)
_LINE_COMMENT = re.compile(r"//[^\n]*")


def _strip_comments(src: str) -> str:
    return _LINE_COMMENT.sub("", _BLOCK_COMMENT.sub("", src))


# ---------------------------------------------------------------------------
# Symbol extraction. Two rules cover every emit shape we currently use in
# transpiler/src:
#
#   1. Qualified `sturm::IDENT` references inside string literals — e.g.
#      `os << "sturm::qint_t<" << W << ">"`,
#      `os << "sturm::invert<&::sturm::lib_mul_dsl>()"`.
#      These cover modular_rewrite_emitter.cpp and lossy_rewrite_emitter.cpp.
#
#   2. Bare-identifier helpers — string literals that ARE just an identifier
#      and match a known emit-target naming convention:
#         - lib_<X>_dsl     (lossy_scope_exit_emitter dsl_name)
#         - <X>_oop / <X>_oop_adj (lossy_*_emitter oop_name / oop_adj_name)
#         - <X>_mod         (modular family helpers)
#         - divide_oop      (lossy_rewrite_emitter divide kernel)
#      The convention test guards us against false positives like the
#      `kind_tag` helper that returns plain "mul" / "div".
# ---------------------------------------------------------------------------
_STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
_QUALIFIED_RE = re.compile(r"(?:::)?sturm::([A-Za-z_][A-Za-z0-9_]*)")
_BARE_IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
_BARE_CONVENTIONS = (
    re.compile(r"^lib_[A-Za-z0-9_]+_dsl$"),
    re.compile(r"^[A-Za-z0-9_]+_oop(?:_adj)?$"),
    re.compile(r"^[A-Za-z0-9_]+_mod$"),
    re.compile(r"^divide_oop$"),
)


def _matches_bare_convention(ident: str) -> bool:
    return any(rx.match(ident) for rx in _BARE_CONVENTIONS)


def extract_symbols(source_text: str) -> set[str]:
    """Return the set of emitted symbol names found in `source_text`."""
    stripped = _strip_comments(source_text)
    symbols: set[str] = set()
    for m in _STRING_LITERAL.finditer(stripped):
        body = m.group(1)
        # Rule 1: qualified sturm::IDENT references.
        for q in _QUALIFIED_RE.finditer(body):
            symbols.add(q.group(1))
        # Rule 2: the literal IS a bare identifier that fits our naming
        # conventions. Skip if rule 1 already ran on this literal — that
        # would mean `sturm::` was present, in which case rule 1 covers it.
        if "sturm::" in body:
            continue
        if _BARE_IDENT_RE.match(body) and _matches_bare_convention(body):
            symbols.add(body)
    return symbols


# ---------------------------------------------------------------------------
# Header discovery. We look for any line in any include/sturm/**.hpp that
# declares or defines `symbol` at namespace scope. The matcher is broad on
# purpose — `void add_mod(...)`, `template<...> X add_mod(...)`,
# `using sturm::add_mod;`, `class qint_t {`, etc. all count. False positives
# (e.g. mention inside a comment) are filtered upstream by _strip_comments.
# ---------------------------------------------------------------------------

def _header_defines(symbol: str, header_text: str) -> bool:
    stripped = _strip_comments(header_text)
    # Word-boundary match — the symbol must appear as a stand-alone token.
    # Empirically every defining header contains the symbol followed by `(`,
    # `<`, ` ;`, ` =`, `{`, or end-of-line. We accept any of those.
    pattern = re.compile(
        r"\b" + re.escape(symbol) + r"\s*[\(<;=\{]"
    )
    return bool(pattern.search(stripped))


def find_defining_header(symbol: str, include_root: Path) -> str:
    """Return the include-root-relative path of the defining header,
    or "<unknown>" if no header in include/sturm/** matches."""
    if not include_root.exists():
        return "<unknown>"
    candidates: list[Path] = []
    for path in sorted(include_root.rglob("*.hpp")):
        try:
            text = path.read_text(errors="replace")
        except OSError:
            continue
        if _header_defines(symbol, text):
            candidates.append(path)
    if not candidates:
        return "<unknown>"
    # Prefer the shortest path (root-most header). Tie-break alphabetically
    # so output is deterministic when two headers share a definition.
    candidates.sort(key=lambda p: (len(p.parts), str(p)))
    return str(candidates[0].relative_to(include_root.parent.parent))


# ---------------------------------------------------------------------------
# File discovery + driver.
# ---------------------------------------------------------------------------

def _discover_sources(transpile_src: Path) -> Iterable[Path]:
    """Yield every matcher_*.cpp and *_emitter.cpp under `transpile_src`."""
    if not transpile_src.exists():
        return []
    # `*.cpp` only — matcher_common.hpp / matcher_lossy_op.hpp must NOT
    # appear in the audit (they don't emit; they're internal headers for
    # the matcher TUs).
    for path in sorted(transpile_src.iterdir()):
        if not path.is_file() or path.suffix != ".cpp":
            continue
        name = path.name
        if name.startswith("matcher_") or name.endswith("_emitter.cpp"):
            yield path


def audit(repo_root: Path) -> list[tuple[str, str, str]]:
    transpile_src = repo_root / "transpiler" / "src"
    include_root = repo_root / "include" / "sturm"
    rows: list[tuple[str, str, str]] = []
    for src in _discover_sources(transpile_src):
        text = src.read_text(errors="replace")
        rel_src = str(src.relative_to(repo_root))
        for symbol in sorted(extract_symbols(text)):
            header = find_defining_header(symbol, include_root)
            rows.append((rel_src, symbol, header))
    rows.sort()
    return rows


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--repo-root", type=Path,
                    default=Path(__file__).resolve().parents[1],
                    help="Repository root (defaults to script's parent).")
    args = ap.parse_args(argv)
    rows = audit(args.repo_root.resolve())
    out = sys.stdout
    out.write("matcher_file\temitted_symbol\tdefining_header\n")
    for matcher, symbol, header in rows:
        out.write(f"{matcher}\t{symbol}\t{header}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
