"""Tests for tools/check_emit_targets_drift.sh.

Issue: sturm-xq05.3 (E1.M3).

The drift-check script re-runs tools/audit_emit_targets.py against the
real repo and diffs stdout against the committed side-table at
docs/transpiler_emit_targets.tsv. We exercise both modes:

  1. Unmodified repo + unmodified TSV  → exit 0, "no drift" message.
  2. Mutated TSV (committed copy out-of-sync) → exit non-zero, message
     mentions the regenerate command.

To exercise mode 2 without mutating the real on-disk TSV, we copy the
relevant pieces of the repo into a temp tree (just `tools/`,
`docs/transpiler_emit_targets.tsv`, `transpiler/src/`, `include/sturm/`)
and mutate the copy's TSV. The script resolves REPO_ROOT relative to
its own location, so a copy under tmp/ resolves to that tmp/.

Run with::

    python3 -m unittest tests.tools.test_emit_targets_drift

(no pytest dependency — the project does not vendor one).
"""

from __future__ import annotations

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SCRIPT = REPO_ROOT / "tools" / "check_emit_targets_drift.sh"
TSV = REPO_ROOT / "docs" / "transpiler_emit_targets.tsv"


def _stage_repo_copy(dst: Path) -> Path:
    """Copy the bits of REPO_ROOT the drift-check needs into `dst`.

    Returns the path to the staged check_emit_targets_drift.sh, which
    resolves its own REPO_ROOT to `dst` via $BASH_SOURCE/..
    """
    # tools/ — the drift script + the audit script it shells out to.
    shutil.copytree(REPO_ROOT / "tools", dst / "tools")
    # docs/transpiler_emit_targets.tsv — the committed side-table.
    (dst / "docs").mkdir()
    shutil.copy2(TSV, dst / "docs" / "transpiler_emit_targets.tsv")
    # transpiler/src + include/sturm — what audit walks.
    shutil.copytree(REPO_ROOT / "transpiler" / "src",
                    dst / "transpiler" / "src")
    shutil.copytree(REPO_ROOT / "include" / "sturm",
                    dst / "include" / "sturm")
    return dst / "tools" / "check_emit_targets_drift.sh"


class EmitTargetsDriftTest(unittest.TestCase):
    def test_script_exists_and_is_executable(self) -> None:
        self.assertTrue(SCRIPT.is_file(),
                        msg=f"missing drift-check script: {SCRIPT}")
        self.assertTrue(os.access(SCRIPT, os.X_OK),
                        msg=f"drift-check script not executable: {SCRIPT}")

    def test_loc_budget_under_50(self) -> None:
        # Plan §E1.M3 caps the script at <=50 LOC.
        n = sum(1 for _ in SCRIPT.read_text().splitlines())
        self.assertLessEqual(n, 50, msg=f"LOC={n}, budget is 50")

    def test_passes_against_committed_tsv(self) -> None:
        # Mode 1: real repo, real TSV. Must exit 0.
        proc = subprocess.run(
            [str(SCRIPT)], capture_output=True, text=True,
        )
        self.assertEqual(
            proc.returncode, 0,
            msg=f"stdout={proc.stdout!r}\nstderr={proc.stderr!r}",
        )
        self.assertIn("no drift", proc.stdout)

    def test_fails_on_mutated_tsv(self) -> None:
        # Mode 2: stage a copy of the repo, mutate the staged TSV, run
        # the staged drift-check. It should exit non-zero and the
        # stderr should mention the regenerate command.
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            staged = _stage_repo_copy(tmp_path / "stage")
            tsv_copy = tmp_path / "stage" / "docs" / "transpiler_emit_targets.tsv"
            # Mutate: prepend a bogus row right after the header.
            text = tsv_copy.read_text()
            head, _, body = text.partition("\n")
            tsv_copy.write_text(
                head + "\n"
                + "transpiler/src/fake.cpp\tfake_symbol\t<unknown>\n"
                + body
            )

            proc = subprocess.run(
                [str(staged)], capture_output=True, text=True,
            )
            self.assertNotEqual(proc.returncode, 0,
                                msg="expected non-zero exit on drift")
            self.assertIn("emit-target drift detected", proc.stderr)
            self.assertIn("audit_emit_targets.py", proc.stderr)


if __name__ == "__main__":
    unittest.main()
