"""Tests for tools/audit_emit_targets.py.

Issue: sturm-xq05.1 (E1.M1).

The audit script walks transpiler/src/matcher_*.cpp and *_emitter.cpp,
regex-extracts emitted symbol names (the identifiers the transpiler
splices into rewritten C++), and grep's include/sturm/** to locate the
defining header. Output is a TSV with columns:

    matcher_file<TAB>emitted_symbol<TAB>defining_header

The fixture-based tests below build a minimal repo skeleton in a tmp
directory that mirrors the on-disk layout the script inspects, run the
script with --repo-root pointed at the skeleton, and assert TSV row
count and contents.

Run with::

    python3 -m unittest tests.tools.test_audit_emit_targets

(no pytest dependency — the project does not vendor one).
"""

from __future__ import annotations

import io
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "tools" / "audit_emit_targets.py"


def _write(p: Path, body: str) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(textwrap.dedent(body).lstrip("\n"))


def _run(repo_root: Path) -> str:
    """Invoke the script in TSV-emit mode; return stdout."""
    proc = subprocess.run(
        [sys.executable, str(SCRIPT), "--repo-root", str(repo_root)],
        capture_output=True, text=True, check=True,
    )
    return proc.stdout


def _parse(tsv: str) -> list[tuple[str, str, str]]:
    """Parse TSV stdout into a list of (matcher, symbol, header) rows.

    Skips the header row and any blank lines.
    """
    rows: list[tuple[str, str, str]] = []
    for line in tsv.splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        if len(parts) != 3:
            continue
        if parts == ["matcher_file", "emitted_symbol", "defining_header"]:
            continue
        rows.append((parts[0], parts[1], parts[2]))
    return rows


class AuditEmitTargetsTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        # Required minimum layout: transpiler/src + include/sturm.
        (self.root / "transpiler" / "src").mkdir(parents=True)
        (self.root / "include" / "sturm").mkdir(parents=True)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    # ------------------------------------------------------------------
    # 1. Single-emitter fixture: extract qualified-name string literals.
    # ------------------------------------------------------------------
    def test_extracts_sturm_qualified_symbols_from_emitter(self) -> None:
        _write(self.root / "transpiler" / "src" / "modular_rewrite_emitter.cpp", """
            #include <sstream>
            namespace sturm::transpile {
            std::string render() {
                std::ostringstream os;
                os << "sturm::qint_t<8>" << " r = ::sturm::add_mod(a, b, n);";
                return os.str();
            }
            }
        """)
        # Defining headers for the two emitted symbols.
        _write(self.root / "include" / "sturm" / "qtypes" / "qint_core.hpp", """
            namespace sturm { template<int W> class qint_t {}; }
        """)
        _write(self.root / "include" / "sturm" / "ops" / "qint_modular.hpp", """
            namespace sturm { void add_mod(int, int, int); }
        """)

        rows = _parse(_run(self.root))
        # Expect 2 rows — one per emitted symbol, both anchored on the
        # one matcher_file we wrote.
        self.assertEqual(len(rows), 2, msg=f"rows={rows!r}")
        symbols = {r[1] for r in rows}
        self.assertEqual(symbols, {"qint_t", "add_mod"})
        for matcher, symbol, header in rows:
            self.assertEqual(matcher, "transpiler/src/modular_rewrite_emitter.cpp")
            if symbol == "qint_t":
                self.assertEqual(header, "include/sturm/qtypes/qint_core.hpp")
            else:
                self.assertEqual(header, "include/sturm/ops/qint_modular.hpp")

    # ------------------------------------------------------------------
    # 2. Bare-identifier helpers (return "lib_mul_dsl";).
    # ------------------------------------------------------------------
    def test_extracts_bare_identifier_helpers(self) -> None:
        _write(self.root / "transpiler" / "src" / "lossy_scope_exit_emitter.cpp", """
            namespace sturm::transpile {
            const char* dsl_name() { return "lib_mul_dsl"; }
            const char* oop_adj_name() { return "mul_oop_adj"; }
            }
        """)
        _write(self.root / "include" / "sturm" / "lib" / "mul_dsl.hpp", """
            namespace sturm { template<int W> void lib_mul_dsl(); }
        """)
        _write(self.root / "include" / "sturm" / "qtypes" / "lossy_oop.hpp", """
            namespace sturm { template<int W> void mul_oop_adj(); }
        """)

        rows = _parse(_run(self.root))
        symbols = {r[1] for r in rows}
        self.assertIn("lib_mul_dsl", symbols)
        self.assertIn("mul_oop_adj", symbols)
        by_sym = {r[1]: r[2] for r in rows}
        self.assertEqual(by_sym["lib_mul_dsl"],
                         "include/sturm/lib/mul_dsl.hpp")
        self.assertEqual(by_sym["mul_oop_adj"],
                         "include/sturm/qtypes/lossy_oop.hpp")

    # ------------------------------------------------------------------
    # 3. Matcher and emitter both walked; comments are ignored.
    # ------------------------------------------------------------------
    def test_walks_matchers_and_skips_comments(self) -> None:
        _write(self.root / "transpiler" / "src" / "matcher_modular_op.cpp", """
            // The matcher recognises sturm::pow(a, x) % n and rewrites to
            // sturm::pow_mod(a, x, n) — but this is in a comment so it
            // must NOT be picked up.
            namespace sturm::transpile {
            void register_pow_mod() {
                emit("sturm::pow_mod");
            }
            }
        """)
        _write(self.root / "include" / "sturm" / "ops" / "qint_modular.hpp", """
            namespace sturm { void pow_mod(int, int, int); }
        """)

        rows = _parse(_run(self.root))
        # Comments must be excluded — only one row from the real
        # `emit("sturm::pow_mod")` call.
        self.assertEqual(len(rows), 1, msg=f"rows={rows!r}")
        self.assertEqual(rows[0][0],
                         "transpiler/src/matcher_modular_op.cpp")
        self.assertEqual(rows[0][1], "pow_mod")
        self.assertEqual(rows[0][2],
                         "include/sturm/ops/qint_modular.hpp")

    # ------------------------------------------------------------------
    # 4. Unknown defining header → "<unknown>".
    # ------------------------------------------------------------------
    def test_unknown_header_marker(self) -> None:
        _write(self.root / "transpiler" / "src" / "adjoint_emitter.cpp", """
            namespace sturm::transpile {
            void f() { emit("sturm::nonexistent_symbol"); }
            }
        """)

        rows = _parse(_run(self.root))
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][1], "nonexistent_symbol")
        self.assertEqual(rows[0][2], "<unknown>")

    # ------------------------------------------------------------------
    # 5. Files outside scope (matcher_common.hpp, plain .hpp, foo.cpp)
    #    are ignored.
    # ------------------------------------------------------------------
    def test_ignores_unrelated_sources(self) -> None:
        _write(self.root / "transpiler" / "src" / "matcher_common.hpp", """
            // header — must NOT be walked even though name matches matcher_*.
            const char* x() { return "sturm::should_not_appear"; }
        """)
        _write(self.root / "transpiler" / "src" / "io.cpp", """
            // not a matcher_*.cpp and not a *_emitter.cpp — must be ignored.
            const char* x() { return "sturm::also_not_appear"; }
        """)
        # One real emitter to give the run something to emit.
        _write(self.root / "transpiler" / "src" / "auto_register_emitter.cpp", """
            namespace sturm::transpile { void f() { emit("sturm::add_mod"); } }
        """)
        _write(self.root / "include" / "sturm" / "ops" / "qint_modular.hpp", """
            namespace sturm { void add_mod(); }
        """)

        rows = _parse(_run(self.root))
        symbols = {r[1] for r in rows}
        self.assertIn("add_mod", symbols)
        self.assertNotIn("should_not_appear", symbols)
        self.assertNotIn("also_not_appear", symbols)

    # ------------------------------------------------------------------
    # 6. Output is deterministic (sorted) & header is present.
    # ------------------------------------------------------------------
    def test_output_is_sorted_with_header(self) -> None:
        _write(self.root / "transpiler" / "src" / "matcher_x.cpp", """
            void f() {
                emit("sturm::zeta");
                emit("sturm::alpha");
            }
        """)
        _write(self.root / "include" / "sturm" / "a.hpp",
               "namespace sturm { void alpha(); }")
        _write(self.root / "include" / "sturm" / "z.hpp",
               "namespace sturm { void zeta(); }")

        out = _run(self.root)
        lines = [ln for ln in out.splitlines() if ln.strip()]
        self.assertEqual(lines[0],
                         "matcher_file\temitted_symbol\tdefining_header")
        rows = _parse(out)
        # alpha sorts before zeta on the (matcher, symbol) key.
        self.assertEqual([r[1] for r in rows], ["alpha", "zeta"])


if __name__ == "__main__":
    unittest.main()
