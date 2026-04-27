"""Tests for tools/extract_md_code_blocks.py + check_getting_started_drift.sh.

Issue: sturm-8gvh.2 (E8.M2).

The extractor pulls the first ```cpp / ```c++ fenced block out of a
markdown file. With one arg it prints the block; with two args it diffs
the block against a source file and exits non-zero on drift. The
companion shell wrapper points it at docs/getting_started.md vs.
tests/external_consumer/main.cpp.

Run with::

    python3 -m unittest tests.tools.test_extract_md_code_blocks
"""
from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
EXTRACTOR = REPO_ROOT / "tools" / "extract_md_code_blocks.py"
DRIFT_SH = REPO_ROOT / "tools" / "check_getting_started_drift.sh"
DOC = REPO_ROOT / "docs" / "getting_started.md"
MAIN_CPP = REPO_ROOT / "tests" / "external_consumer" / "main.cpp"


def _run(args: list[str]) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, capture_output=True, text=True, check=False)


class ExtractMdCodeBlocksTest(unittest.TestCase):
    def test_files_exist_and_executable(self) -> None:
        self.assertTrue(EXTRACTOR.is_file(), msg=f"missing: {EXTRACTOR}")
        self.assertTrue(os.access(EXTRACTOR, os.X_OK),
                        msg=f"not executable: {EXTRACTOR}")
        self.assertTrue(DRIFT_SH.is_file(), msg=f"missing: {DRIFT_SH}")
        self.assertTrue(os.access(DRIFT_SH, os.X_OK),
                        msg=f"not executable: {DRIFT_SH}")
        self.assertTrue(DOC.is_file(), msg=f"missing: {DOC}")

    def test_extractor_loc_under_80(self) -> None:
        # Plan §E8.M2 caps the extractor at <=80 LOC.
        n = sum(1 for _ in EXTRACTOR.read_text().splitlines())
        self.assertLessEqual(n, 80, msg=f"extractor LOC={n}, budget is 80")

    def test_drift_sh_loc_under_30(self) -> None:
        n = sum(1 for _ in DRIFT_SH.read_text().splitlines())
        self.assertLessEqual(n, 30, msg=f"drift script LOC={n}, budget is 30")

    def test_extract_single_cpp_block(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            md = Path(tmp) / "in.md"
            md.write_text(
                "# title\n\nprose\n\n```cpp\nint x = 1;\nint y = 2;\n```\n"
                "trailing\n"
            )
            proc = _run(["python3", str(EXTRACTOR), str(md)])
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)
            self.assertEqual(proc.stdout, "int x = 1;\nint y = 2;\n")

    def test_extract_first_of_two_blocks(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            md = Path(tmp) / "in.md"
            md.write_text(
                "```cpp\nFIRST\n```\n\n```cpp\nSECOND\n```\n"
            )
            proc = _run(["python3", str(EXTRACTOR), str(md)])
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)
            self.assertEqual(proc.stdout, "FIRST\n")

    def test_extract_first_cpp_skips_other_languages(self) -> None:
        # A leading ```cmake fence must NOT be picked up; the extractor
        # selects the first cpp block (this is what lets the doc carry a
        # cmake snippet ahead of main.cpp without confusing the diff).
        with tempfile.TemporaryDirectory() as tmp:
            md = Path(tmp) / "in.md"
            md.write_text(
                "```cmake\nproject(x)\n```\n\n```cpp\nMAIN\n```\n"
            )
            proc = _run(["python3", str(EXTRACTOR), str(md)])
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)
            self.assertEqual(proc.stdout, "MAIN\n")

    def test_extract_handles_cplusplus_fence(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            md = Path(tmp) / "in.md"
            md.write_text("```c++\nHELLO\n```\n")
            proc = _run(["python3", str(EXTRACTOR), str(md)])
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)
            self.assertEqual(proc.stdout, "HELLO\n")

    def test_no_block_returns_nonzero(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            md = Path(tmp) / "empty.md"
            md.write_text("# just prose\nno fences here.\n")
            proc = _run(["python3", str(EXTRACTOR), str(md)])
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("no ```cpp", proc.stderr)

    def test_diff_equal_returns_zero(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            md = Path(tmp) / "in.md"
            src = Path(tmp) / "src.cpp"
            body = "int main(){return 0;}\n"
            md.write_text(f"```cpp\n{body}```\n")
            src.write_text(body)
            proc = _run(["python3", str(EXTRACTOR), str(md), str(src)])
            self.assertEqual(proc.returncode, 0, msg=proc.stderr)
            self.assertIn("byte-for-byte", proc.stdout)

    def test_diff_unequal_returns_nonzero(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            md = Path(tmp) / "in.md"
            src = Path(tmp) / "src.cpp"
            md.write_text("```cpp\nA\n```\n")
            src.write_text("B\n")
            proc = _run(["python3", str(EXTRACTOR), str(md), str(src)])
            self.assertNotEqual(proc.returncode, 0)
            self.assertIn("drift", proc.stderr)

    def test_missing_markdown_file_returns_two(self) -> None:
        proc = _run(["python3", str(EXTRACTOR), "/nonexistent/path.md"])
        self.assertEqual(proc.returncode, 2)
        self.assertIn("not found", proc.stderr)

    def test_missing_args_returns_two(self) -> None:
        proc = _run(["python3", str(EXTRACTOR)])
        self.assertEqual(proc.returncode, 2)
        self.assertIn("usage", proc.stderr)

    def test_drift_check_passes_against_real_tree(self) -> None:
        # The committed doc must match the committed main.cpp.
        proc = _run([str(DRIFT_SH)])
        self.assertEqual(
            proc.returncode, 0,
            msg=f"stdout={proc.stdout!r}\nstderr={proc.stderr!r}",
        )
        self.assertIn("no drift", proc.stdout)

    def test_doc_first_cpp_block_matches_main_cpp(self) -> None:
        # Direct check on the extractor against the real files.
        proc = _run(["python3", str(EXTRACTOR), str(DOC), str(MAIN_CPP)])
        self.assertEqual(
            proc.returncode, 0,
            msg=f"stdout={proc.stdout!r}\nstderr={proc.stderr!r}",
        )


if __name__ == "__main__":
    unittest.main()
