"""Tests for tools/check_public_api_drift.sh + tools/extract_public_api.py.

Issue: sturm-8gvh.1 (E8.M1).

The drift-check script re-runs `tools/extract_public_api.py` against
the real repo and diffs stdout against the committed snapshot at
`docs/public_api.txt`. We exercise both modes:

  1. Unmodified repo + unmodified snapshot  → exit 0, "no drift" msg.
  2. Mutated snapshot (committed copy out-of-sync) → exit non-zero,
     stderr mentions the regenerate command.

To exercise mode 2 without mutating the on-disk snapshot, we copy the
relevant pieces of the repo into a temp tree (just `tools/`,
`docs/public_api.txt`, `include/sturm/`) and mutate the copy's
snapshot. The shell script resolves REPO_ROOT relative to its own
location, so a copy under tmp/ resolves to that tmp/.

Run with::

    python3 -m unittest tests.tools.test_public_api_drift
"""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "tools" / "check_public_api_drift.sh"
EXTRACTOR = REPO_ROOT / "tools" / "extract_public_api.py"
SNAPSHOT = REPO_ROOT / "docs" / "public_api.txt"
DOC = REPO_ROOT / "docs" / "public_api.md"


def _stage_repo_copy(dst: Path) -> Path:
    """Copy the bits of REPO_ROOT the drift-check needs into ``dst``.

    Returns the path to the staged check_public_api_drift.sh, which
    resolves its own REPO_ROOT to ``dst`` via ``$BASH_SOURCE/..``.
    """
    shutil.copytree(REPO_ROOT / "tools", dst / "tools")
    (dst / "docs").mkdir()
    shutil.copy2(SNAPSHOT, dst / "docs" / "public_api.txt")
    shutil.copytree(REPO_ROOT / "include" / "sturm",
                    dst / "include" / "sturm")
    return dst / "tools" / "check_public_api_drift.sh"


class PublicApiDriftTest(unittest.TestCase):
    def test_files_exist(self) -> None:
        self.assertTrue(SCRIPT.is_file(), msg=f"missing: {SCRIPT}")
        self.assertTrue(os.access(SCRIPT, os.X_OK),
                        msg=f"not executable: {SCRIPT}")
        self.assertTrue(EXTRACTOR.is_file(), msg=f"missing: {EXTRACTOR}")
        self.assertTrue(SNAPSHOT.is_file(), msg=f"missing: {SNAPSHOT}")
        self.assertTrue(DOC.is_file(), msg=f"missing: {DOC}")

    def test_total_loc_budget_under_80(self) -> None:
        # Plan §E8.M1 caps the script set at <=80 LOC total.
        n = sum(1 for _ in SCRIPT.read_text().splitlines())
        n += sum(1 for _ in EXTRACTOR.read_text().splitlines())
        self.assertLessEqual(n, 80, msg=f"combined LOC={n}, budget is 80")

    def test_extractor_emits_nonempty_sorted_list(self) -> None:
        proc = subprocess.run(
            ["python3", str(EXTRACTOR)],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(proc.returncode, 0,
                         msg=f"stderr={proc.stderr!r}")
        lines = [ln for ln in proc.stdout.splitlines() if ln]
        self.assertGreater(len(lines), 10,
                           msg="extractor emitted suspiciously few symbols")
        self.assertEqual(lines, sorted(lines),
                         msg="extractor output not sorted")
        # Spot-check a few headline symbols from PRD §3.3.
        for required in ("qint_t", "qbool", "WHEN", "add_mod",
                         "mul_mod", "pow_mod", "invert",
                         "STURM_REGISTER_ADJOINT", "STURM_VERSION_MAJOR"):
            self.assertIn(required, lines,
                          msg=f"missing required symbol: {required}")

    def test_passes_against_committed_snapshot(self) -> None:
        # Mode 1: real repo, real snapshot. Must exit 0.
        proc = subprocess.run(
            [str(SCRIPT)], capture_output=True, text=True, check=False,
        )
        self.assertEqual(
            proc.returncode, 0,
            msg=f"stdout={proc.stdout!r}\nstderr={proc.stderr!r}",
        )
        self.assertIn("no drift", proc.stdout)

    def test_fails_on_mutated_snapshot(self) -> None:
        # Mode 2: stage a copy of the repo, mutate the staged snapshot,
        # run the staged drift-check. It should exit non-zero and the
        # stderr should mention the regenerate command.
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            staged = _stage_repo_copy(tmp_path / "stage")
            snap_copy = tmp_path / "stage" / "docs" / "public_api.txt"
            text = snap_copy.read_text()
            # Mutate: prepend a bogus symbol to the (sorted) snapshot.
            snap_copy.write_text("AAA_drifted_symbol\n" + text)

            proc = subprocess.run(
                [str(staged)], capture_output=True, text=True, check=False,
            )
            self.assertNotEqual(proc.returncode, 0,
                                msg="expected non-zero exit on drift")
            self.assertIn("public-api drift detected", proc.stderr)
            self.assertIn("extract_public_api.py", proc.stderr)


if __name__ == "__main__":
    unittest.main()
