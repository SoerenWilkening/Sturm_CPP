"""Tests for tools/lint_public_headers.py.

Issue: sturm-nalq.2 (E2.M2).

Builds a synthetic include/sturm/ tree in a temp directory exercising
each violation class plus a clean baseline, runs the linter via
subprocess against ``--repo-root <tmp>``, and asserts exit code +
expected violation lines.

Run with::

    python3 -m unittest tests.tools.test_lint_public_headers
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "tools" / "lint_public_headers.py"


def _w(p: Path, body: str) -> None:
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(textwrap.dedent(body).lstrip("\n"))


def _run(repo_root: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [sys.executable, str(SCRIPT), "--repo-root", str(repo_root)],
        capture_output=True, text=True, check=False,
    )


class LintPublicHeadersTest(unittest.TestCase):
    def setUp(self) -> None:
        self._tmp = tempfile.TemporaryDirectory()
        self.root = Path(self._tmp.name)
        (self.root / "include" / "sturm").mkdir(parents=True)
        (self.root / "include" / "sturm" / "detail").mkdir(parents=True)

    def tearDown(self) -> None:
        self._tmp.cleanup()

    # ------------------------------------------------------------------
    # 1. Clean baseline: existing public-only includes + system headers.
    # ------------------------------------------------------------------
    def test_clean_baseline_passes(self) -> None:
        _w(self.root / "include" / "sturm" / "core" / "a.hpp", """
            #pragma once
            #include <vector>
            #include <cstdint>
            #include "sturm/core/b.hpp"
        """)
        _w(self.root / "include" / "sturm" / "core" / "b.hpp",
           "#pragma once\n")
        # detail/ headers can be wild — they are not linted.
        _w(self.root / "include" / "sturm" / "detail" / "x.hpp",
           "#pragma once\n#include \"random/garbage.hpp\"\n")
        proc = _run(self.root)
        self.assertEqual(proc.returncode, 0,
                         msg=f"stdout={proc.stdout!r} stderr={proc.stderr!r}")
        self.assertEqual(proc.stdout.strip(), "")

    # ------------------------------------------------------------------
    # 2. Non-existent project include is flagged.
    # ------------------------------------------------------------------
    def test_nonexistent_path_flagged(self) -> None:
        _w(self.root / "include" / "sturm" / "a.hpp", """
            #pragma once
            #include "sturm/does_not_exist.hpp"
        """)
        proc = _run(self.root)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("does not exist", proc.stdout)
        self.assertIn("sturm/does_not_exist.hpp", proc.stdout)

    # ------------------------------------------------------------------
    # 3. Reach into sturm/detail/ from a public header is flagged.
    # ------------------------------------------------------------------
    def test_disallowed_detail_include_flagged(self) -> None:
        _w(self.root / "include" / "sturm" / "a.hpp", """
            #pragma once
            #include "sturm/detail/secret.hpp"
        """)
        _w(self.root / "include" / "sturm" / "detail" / "secret.hpp",
           "#pragma once\n")
        proc = _run(self.root)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("reaches into detail/", proc.stdout)
        self.assertIn("sturm/detail/secret.hpp", proc.stdout)

    # ------------------------------------------------------------------
    # 4. Quoted include outside include/sturm/ is flagged.
    # ------------------------------------------------------------------
    def test_quoted_outside_sturm_flagged(self) -> None:
        _w(self.root / "include" / "sturm" / "a.hpp", """
            #pragma once
            #include "vendor/foo.hpp"
        """)
        proc = _run(self.root)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("outside include/sturm/", proc.stdout)
        self.assertIn("vendor/foo.hpp", proc.stdout)

    # ------------------------------------------------------------------
    # 5. Headers under detail/ are NOT linted (free pass).
    # ------------------------------------------------------------------
    def test_detail_headers_skipped(self) -> None:
        _w(self.root / "include" / "sturm" / "detail" / "deep" / "x.hpp", """
            #pragma once
            #include "sturm/does_not_exist.hpp"
            #include "vendor/anything.hpp"
        """)
        proc = _run(self.root)
        self.assertEqual(proc.returncode, 0,
                         msg=f"stdout={proc.stdout!r}")

    # ------------------------------------------------------------------
    # 6. Multiple violation classes in a single tree are all reported.
    # ------------------------------------------------------------------
    def test_multiple_violations_all_reported(self) -> None:
        _w(self.root / "include" / "sturm" / "a.hpp", """
            #pragma once
            #include "sturm/missing.hpp"
            #include "sturm/detail/x.hpp"
            #include "vendor/y.hpp"
            #include <vector>
        """)
        _w(self.root / "include" / "sturm" / "detail" / "x.hpp",
           "#pragma once\n")
        proc = _run(self.root)
        self.assertEqual(proc.returncode, 1)
        # Three distinct violations on lines 2, 3, 4.
        self.assertIn("a.hpp:2", proc.stdout)
        self.assertIn("a.hpp:3", proc.stdout)
        self.assertIn("a.hpp:4", proc.stdout)
        self.assertIn("3 violation(s)", proc.stderr)

    # ------------------------------------------------------------------
    # 7. <sturm/...> angle-bracket project includes are validated too.
    # ------------------------------------------------------------------
    def test_angle_bracket_sturm_validated(self) -> None:
        _w(self.root / "include" / "sturm" / "a.hpp", """
            #pragma once
            #include <sturm/missing.hpp>
        """)
        proc = _run(self.root)
        self.assertEqual(proc.returncode, 1)
        self.assertIn("does not exist", proc.stdout)

    # ------------------------------------------------------------------
    # 8. <vendor> non-sturm angle-bracket includes are accepted (system).
    # ------------------------------------------------------------------
    def test_angle_bracket_non_sturm_accepted(self) -> None:
        _w(self.root / "include" / "sturm" / "a.hpp", """
            #pragma once
            #include <orkan/orkan.hpp>
            #include <cstdint>
        """)
        proc = _run(self.root)
        self.assertEqual(proc.returncode, 0,
                         msg=f"stdout={proc.stdout!r}")


if __name__ == "__main__":
    unittest.main()
