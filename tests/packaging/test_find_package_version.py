#!/usr/bin/env python3
"""test_find_package_version.py — sturm-mjr2.2 / E4.M2 acceptance test.

Pins the contract that `find_package(sturm REQUIRED)` populates the
`sturm_VERSION` CMake variable to the same value as the top-level
`project(... VERSION x.y.z)` declaration. Cf. PRD §3.8 / Plan §E4.M2.

Mechanism (CMake stdlib): `write_basic_package_version_file()` produces
`<prefix>/lib/cmake/sturm/sturmConfigVersion.cmake`; the side-by-side
sturmConfig.cmake is found by `find_package`, and CMake itself reads the
version-file and exposes `${sturm_VERSION}` to the consumer. Our
sturmConfig.cmake.in needs no version-specific code — but the *install
must ship sturmConfigVersion.cmake* (root CMakeLists.txt §3) and the
package name and file naming must match.

E6 will absorb this assertion into a richer external smoke test; for now
we own the install→find_package→assert loop in a single self-contained
Python test, mirroring the pattern in test_sturmc.py.

Flow:
  1. cmake --install ${BUILD_DIR} --prefix <tmp>
  2. write a minimal downstream CMakeLists.txt that does
     find_package(sturm REQUIRED) and message(FATAL_ERROR …) unless
     sturm_VERSION is non-empty AND equal to ${EXPECTED_VERSION}.
  3. cmake -S <downstream-src> -B <downstream-build>
         -Dsturm_DIR=<prefix>/lib/cmake/sturm
         -DEXPECTED_VERSION=<x.y.z>
     — the configure step IS the assertion. rc==0 ⇒ pass.
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path


def _env(name: str) -> str:
    v = os.environ.get(name)
    if not v:
        raise RuntimeError(
            f"test_find_package_version: environment variable {name!r} is "
            "required (set by tests/packaging/CMakeLists.txt under ctest)."
        )
    return v


# Minimal downstream CMakeLists.txt. Configure-time assertions only — no
# build step needed: if find_package() does not populate sturm_VERSION,
# `message(FATAL_ERROR …)` makes `cmake -S … -B …` exit non-zero.
DOWNSTREAM_CMAKELISTS = textwrap.dedent("""\
    cmake_minimum_required(VERSION 3.16)
    project(sturm_find_package_version_probe LANGUAGES CXX)

    find_package(sturm REQUIRED)

    if(NOT DEFINED sturm_VERSION OR sturm_VERSION STREQUAL "")
        message(FATAL_ERROR
            "find_package(sturm) did not populate sturm_VERSION. "
            "Expected the side-by-side sturmConfigVersion.cmake to "
            "set it to the project version.")
    endif()

    if(NOT sturm_VERSION STREQUAL "${EXPECTED_VERSION}")
        message(FATAL_ERROR
            "sturm_VERSION mismatch: got '${sturm_VERSION}', "
            "expected '${EXPECTED_VERSION}' (from PROJECT_VERSION).")
    endif()

    message(STATUS "sturm_VERSION=${sturm_VERSION} (matches expected)")
""")


class FindPackageVersionTest(unittest.TestCase):
    """Single test: install→configure-downstream→assert. Splitting would
    force multiple cmake --install runs against the same prefix."""

    def test_find_package_populates_sturm_VERSION(self) -> None:
        build_dir = Path(_env("STURMC_BUILD_DIR")).resolve()
        expected_version = _env("STURM_EXPECTED_VERSION")
        cmake_bin = os.environ.get("STURMC_CMAKE", "cmake")

        self.assertTrue(build_dir.is_dir(),
                        f"build dir does not exist: {build_dir}")

        with tempfile.TemporaryDirectory(prefix="sturm-fpv-") as tmp:
            tmp_root = Path(tmp)
            prefix = tmp_root / "prefix"
            downstream_src = tmp_root / "downstream"
            downstream_build = tmp_root / "downstream-build"
            downstream_src.mkdir()
            (downstream_src / "CMakeLists.txt").write_text(
                DOWNSTREAM_CMAKELISTS)

            # Step 1 — install STURM.
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

            sturm_cmake_dir = prefix / "lib" / "cmake" / "sturm"
            self.assertTrue(
                (sturm_cmake_dir / "sturmConfig.cmake").is_file(),
                f"sturmConfig.cmake missing under {sturm_cmake_dir}")
            self.assertTrue(
                (sturm_cmake_dir / "sturmConfigVersion.cmake").is_file(),
                f"sturmConfigVersion.cmake missing under {sturm_cmake_dir}; "
                "without it find_package will not populate sturm_VERSION.")

            # Step 2/3 — configure the downstream probe. Configure success
            # IS the assertion (the FATAL_ERROR guards above).
            cr = subprocess.run(
                [cmake_bin,
                 "-S", str(downstream_src),
                 "-B", str(downstream_build),
                 f"-Dsturm_DIR={sturm_cmake_dir}",
                 f"-DEXPECTED_VERSION={expected_version}"],
                capture_output=True, text=True,
            )
            self.assertEqual(
                cr.returncode, 0,
                f"downstream cmake configure failed (rc={cr.returncode})\n"
                f"stdout:\n{cr.stdout}\nstderr:\n{cr.stderr}\n"
            )
            # Sanity: the STATUS message we emitted on success appears.
            self.assertIn(
                f"sturm_VERSION={expected_version}",
                cr.stdout + cr.stderr,
                "expected STATUS line confirming version match was not "
                "emitted by the downstream configure",
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
