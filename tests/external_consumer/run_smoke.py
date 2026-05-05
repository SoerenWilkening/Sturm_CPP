#!/usr/bin/env python3
"""run_smoke.py — sturm-795j.1 / E6.M1 acceptance test driver.

Drives the `tests/external_consumer/` smoke project end-to-end against
an INSTALLED STURM prefix. Mirrors the E5.M2 `test_sturmc.py` install
pattern: `cmake --install` into a per-test tempdir, configure +
build the standalone consumer there, run the produced binary, diff
its stdout vs. `golden.txt`. PRD §3.6 / Plan §E6.M1.

Flow:
  1. `cmake --install ${BUILD_DIR} --prefix <tmp>/prefix`
  2. `cmake -S tests/external_consumer -B <tmp>/build
            -DCMAKE_PREFIX_PATH=<tmp>/prefix`
  3. `cmake --build <tmp>/build --parallel 6`
  4. `<tmp>/build/external_consumer`
  5. assertEqual(stdout, golden.txt contents)

Owning the install inside this Python test (rather than via a ctest
fixture writing into a shared prefix) keeps the install→configure→
build→run ordering bytewise verifiable from a single rc-checked
subprocess chain, and scopes the temp prefix to this test's lifetime.

Hard-capped at `--parallel 6` per CLAUDE.md.
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SOURCE_DIR = Path(__file__).resolve().parent
GOLDEN_PATH = SOURCE_DIR / "golden.txt"


def _env(name: str) -> str:
    v = os.environ.get(name)
    if not v:
        raise RuntimeError(
            f"run_smoke: environment variable {name!r} is required "
            "(set by tests/packaging/CMakeLists.txt under ctest)."
        )
    return v


class ExternalConsumerSmokeTest(unittest.TestCase):
    """Single test — one install + one configure + one build + one run.
    Splitting would force multiple `cmake --install`s against the same
    prefix; the install rule's overwrite semantics are not perfectly
    idempotent so we keep it to a single run, matching `test_sturmc.py`."""

    def test_smoke_against_installed_prefix(self) -> None:
        build_dir = Path(_env("STURMC_BUILD_DIR")).resolve()
        cxx = _env("STURMC_CXX")
        cmake_bin = os.environ.get("STURMC_CMAKE", "cmake")

        self.assertTrue(build_dir.is_dir(),
                        f"build dir does not exist: {build_dir}")
        self.assertTrue(GOLDEN_PATH.is_file(),
                        f"golden missing: {GOLDEN_PATH}")
        expected = GOLDEN_PATH.read_text()

        with tempfile.TemporaryDirectory(prefix="sturm-ext-") as tmp:
            tmp_root = Path(tmp)
            prefix = tmp_root / "prefix"
            consumer_build = tmp_root / "consumer-build"

            # Step 1 — install STURM into the temp prefix.
            ir = subprocess.run(
                [cmake_bin, "--install", str(build_dir),
                 "--prefix", str(prefix)],
                capture_output=True, text=True,
            )
            self.assertEqual(
                ir.returncode, 0,
                f"cmake --install failed (rc={ir.returncode})\n"
                f"stdout:\n{ir.stdout}\nstderr:\n{ir.stderr}\n"
            )

            # Sanity: the artifacts the consumer's `find_package(sturm)`
            # and `add_quantum_executable` will reach for must exist.
            for rel in ("lib/cmake/sturm/sturmConfig.cmake",
                        "lib/cmake/sturm/SturmTranspile.cmake",
                        "include/sturm/prelude.hpp",
                        "bin/sturm-transpile"):
                self.assertTrue(
                    (prefix / rel).is_file(),
                    f"installed prefix missing required artifact: {rel}",
                )

            # Step 2 — configure the standalone consumer against the
            # installed prefix. CMAKE_PREFIX_PATH is the documented entry
            # point for `find_package` resolution.
            #
            # `-DSTURM_EXTERNAL_CONSUMER_SMOKE_TEST=ON` (sturm-nfmu) arms
            # the consumer's stale-install shadow precheck. AppleClang's
            # default include-search order on macOS places /usr/local/
            # include BEFORE -isystem flags, so a months-old `cmake
            # --install` (or a Homebrew install of sturm-transpile) at
            # /usr/local/include/sturm/ would silently shadow this temp
            # prefix and break the build with a misleading "missing
            # add_mod / WHEN" error. The sentinel-gated precheck in
            # tests/external_consumer/CMakeLists.txt fails configure
            # with an actionable diagnostic instead. Genuine downstream
            # consumers (no sentinel) are not affected.
            cr = subprocess.run(
                [cmake_bin,
                 "-S", str(SOURCE_DIR),
                 "-B", str(consumer_build),
                 f"-DCMAKE_PREFIX_PATH={prefix}",
                 f"-DCMAKE_CXX_COMPILER={cxx}",
                 "-DSTURM_EXTERNAL_CONSUMER_SMOKE_TEST=ON"],
                capture_output=True, text=True,
            )
            self.assertEqual(
                cr.returncode, 0,
                f"consumer cmake configure failed (rc={cr.returncode})\n"
                f"stdout:\n{cr.stdout}\nstderr:\n{cr.stderr}\n"
            )

            # Step 3 — build (hard-cap parallel at 6 per CLAUDE.md).
            br = subprocess.run(
                [cmake_bin, "--build", str(consumer_build),
                 "--parallel", "6"],
                capture_output=True, text=True,
            )
            self.assertEqual(
                br.returncode, 0,
                f"consumer cmake --build failed (rc={br.returncode})\n"
                f"stdout:\n{br.stdout}\nstderr:\n{br.stderr}\n"
            )

            # Step 4 — locate and run the produced binary. CMake's default
            # output layout drops executables at the build-tree root for
            # single-config generators (Makefiles / Ninja); we glob to be
            # robust to multi-config builds (Xcode / VS) without forcing a
            # specific generator on the test driver.
            candidates = (list(consumer_build.glob("external_consumer"))
                          + list(consumer_build.glob("*/external_consumer")))
            self.assertTrue(
                candidates,
                f"could not locate external_consumer binary under "
                f"{consumer_build}; build output above")
            binary = candidates[0]

            rr = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(
                rr.returncode, 0,
                f"external_consumer failed (rc={rr.returncode})\n"
                f"stdout:\n{rr.stdout}\nstderr:\n{rr.stderr}\n"
            )

            # Step 5 — diff stdout vs. the golden. The trailing newline
            # in the golden file matches the `\n` `printf` writes; we
            # compare byte-for-byte rather than via `splitlines()` so a
            # silent trailing-newline regression surfaces.
            self.assertEqual(
                rr.stdout, expected,
                f"stdout drift vs. golden:\n  got:      {rr.stdout!r}\n"
                f"  expected: {expected!r}\n",
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
