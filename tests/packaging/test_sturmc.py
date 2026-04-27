#!/usr/bin/env python3
"""test_sturmc.py — sturm-vr0v.2 / E5.M2 acceptance test.

End-to-end check of the sturmc CLI driver against an *installed* STURM
prefix. The flow is:

  1. cmake --install ${BUILD_DIR} --prefix <tmp>      (gated by ctest
     fixture `sturmc_install_prefix`, see tests/packaging/CMakeLists.txt).
  2. <tmp>/bin/sturmc <fixture.cpp> -o <tmp>/hello \
         --cxx <C++ compiler> --include-dirs <tmp>/include --link sturm
  3. <tmp>/hello                              (must run, exit 0)
  4. assertEqual(stdout, "sturmc-ok\n")

The test uses stdlib `unittest` because this repo does not vendor
pytest. `BUILD_DIR`, `CXX`, and `FIXTURE_CPP` are passed in via the
environment by the ctest invocation (see tests/packaging/CMakeLists.txt).
Running the file directly outside ctest still works as long as those
three env vars are set.
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


def _env(name: str) -> str:
    v = os.environ.get(name)
    if not v:
        raise RuntimeError(
            f"test_sturmc: environment variable {name!r} is required "
            "(set by tests/packaging/CMakeLists.txt under ctest, or by "
            "the developer when running this file directly)."
        )
    return v


class SturmcInstalledPrefixTest(unittest.TestCase):
    """One single test — fully exercises the install→transpile→compile→
    link→run pipeline. Splitting into multiple tests would force multiple
    cmake --install runs against the same prefix, which is wasteful and
    invites flakiness from the install rule's not-truly-idempotent file
    overwrites."""

    def test_sturmc_against_installed_prefix(self) -> None:
        build_dir = Path(_env("STURMC_BUILD_DIR")).resolve()
        cxx = _env("STURMC_CXX")
        fixture = Path(_env("STURMC_FIXTURE_CPP")).resolve()
        # Optional cmake binary override; falls back to PATH lookup.
        cmake_bin = os.environ.get("STURMC_CMAKE", "cmake")

        self.assertTrue(build_dir.is_dir(),
                        f"build dir does not exist: {build_dir}")
        self.assertTrue(fixture.is_file(),
                        f"fixture missing: {fixture}")

        with tempfile.TemporaryDirectory(prefix="sturmc-prefix-") as tmp:
            prefix = Path(tmp)

            # Step 1 — populate the installed tree.
            install_cmd = [
                cmake_bin, "--install", str(build_dir),
                "--prefix", str(prefix),
            ]
            ir = subprocess.run(install_cmd, capture_output=True, text=True)
            self.assertEqual(
                ir.returncode, 0,
                f"cmake --install failed (rc={ir.returncode})\n"
                f"stdout:\n{ir.stdout}\nstderr:\n{ir.stderr}\n"
            )

            sturmc = prefix / "bin" / "sturmc"
            self.assertTrue(sturmc.is_file(),
                            f"sturmc not installed at {sturmc}")
            self.assertTrue(os.access(str(sturmc), os.X_OK),
                            f"sturmc not executable: {sturmc}")

            transpiler = prefix / "bin" / "sturm-transpile"
            self.assertTrue(transpiler.is_file(),
                            f"sturm-transpile not installed at {transpiler}")

            include_dir = prefix / "include"
            self.assertTrue(include_dir.is_dir(),
                            f"include tree missing: {include_dir}")
            self.assertTrue(
                (include_dir / "sturm" / "routines" / "invert.hpp").is_file(),
                "expected public header sturm/routines/invert.hpp under "
                f"{include_dir}",
            )

            # Step 2 — run sturmc end-to-end.
            output = prefix / "hello"
            run_cmd = [
                sys.executable, str(sturmc),
                str(fixture),
                "-o", str(output),
                "--cxx", cxx,
                "--include-dirs", str(include_dir),
                "--link", "sturm",
                "--verbose",
            ]
            sr = subprocess.run(run_cmd, capture_output=True, text=True)
            self.assertEqual(
                sr.returncode, 0,
                f"sturmc failed (rc={sr.returncode})\n"
                f"cmd: {' '.join(run_cmd)}\n"
                f"stdout:\n{sr.stdout}\nstderr:\n{sr.stderr}\n"
            )
            self.assertTrue(output.is_file(),
                            f"sturmc did not produce binary at {output}")

            # Step 3 — run the produced binary and capture stdout.
            br = subprocess.run([str(output)], capture_output=True, text=True)
            self.assertEqual(
                br.returncode, 0,
                f"produced binary failed (rc={br.returncode})\n"
                f"stdout:\n{br.stdout}\nstderr:\n{br.stderr}\n"
            )
            # Step 4 — assert stdout.
            self.assertEqual(br.stdout, "sturmc-ok\n",
                             f"unexpected stdout: {br.stdout!r}")


if __name__ == "__main__":
    # Verbose by default so ctest --output-on-failure shows useful info.
    unittest.main(verbosity=2)
