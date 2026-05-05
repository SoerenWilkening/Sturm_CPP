#!/usr/bin/env python3
"""test_shadow_detection.py — sturm-nfmu acceptance test.

Pins the configure-time precheck that `tests/external_consumer/CMakeLists.txt`
runs to detect a STALE STURM install at /usr/local/include/sturm or
/opt/homebrew/include/sturm shadowing the freshly-installed temp prefix the
external_consumer_smoke test uses.

Background (issue sturm-nfmu):
  AppleClang's default include-search order on macOS places /usr/local/include
  BEFORE -isystem flags, so a months-old `cmake --install` (or a Homebrew
  install of `sturm-transpile`) at /usr/local/include/sturm/ silently wins
  over the smoke test's per-test temp prefix. The compile failure that
  results mentions missing identifiers (`add_mod`, `WHEN`) instead of "a
  stale install is shadowing your freshly-installed prefix", which is
  actively misleading for future contributors. The precheck makes that
  failure mode loud and actionable at consumer-configure time.

What this test pins:
  1. The precheck FIRES when STURM_EXTERNAL_CONSUMER_SMOKE_TEST=ON and at
     least one of the listed shadow paths exists. The configure must FAIL
     with FATAL_ERROR (rc != 0) and the stderr must contain the actionable
     diagnostic that names the offending path AND the remediation
     (`brew uninstall` / `sudo rm -rf`).
  2. The precheck is SILENT when STURM_EXTERNAL_CONSUMER_SMOKE_TEST is
     unset or OFF (genuine downstream consumers who knowingly accept the
     shadow path must not be blocked by this check).
  3. The precheck is SILENT when no shadow path exists, even when the
     sentinel is ON.

The test drives the consumer's CMakeLists.txt directly via
`cmake -S tests/external_consumer -B <tmp>` with `STURM_SHADOW_INCLUDE_PATHS`
overridden to point at a tmp directory we control, so the assertions do
not depend on the actual /usr/local/include/sturm state of the host.

A `find_package(sturm REQUIRED)` is wired into the consumer CMakeLists,
which would normally fail without an installed prefix. To keep this test
configure-only (no install) the test passes -DCMAKE_PREFIX_PATH pointing
at the build tree's exported sturmConfig.cmake (mirrors what
run_smoke.py does). The precheck fires BEFORE find_package, so even when
CMAKE_PREFIX_PATH is missing the precheck assertions still hold; the
prefix is supplied only to keep the negative-case configures clean.

Hard-capped at --parallel 6 per CLAUDE.md (no build step here, but any
follow-up cmake --build call must respect the cap).
"""
from __future__ import annotations

import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


SOURCE_DIR = Path(__file__).resolve().parent
CONSUMER_CMAKELISTS = SOURCE_DIR / "CMakeLists.txt"


def _env(name: str) -> str:
    v = os.environ.get(name)
    if not v:
        raise RuntimeError(
            f"test_shadow_detection: environment variable {name!r} is "
            "required (set by tests/packaging/CMakeLists.txt under ctest)."
        )
    return v


class ShadowDetectionTest(unittest.TestCase):
    """Three assertions: fires on shadow + sentinel, silent without sentinel,
    silent without shadow. All three are configure-only (no build, no run)."""

    def setUp(self) -> None:
        self.build_dir = Path(_env("STURMC_BUILD_DIR")).resolve()
        self.cmake_bin = os.environ.get("STURMC_CMAKE", "cmake")
        self.assertTrue(
            CONSUMER_CMAKELISTS.is_file(),
            f"consumer CMakeLists.txt missing: {CONSUMER_CMAKELISTS}",
        )

    # ── helper ──────────────────────────────────────────────────────────────
    def _configure(self, tmp: Path, *,
                   sentinel: bool,
                   shadow_paths: list[Path] | None) -> subprocess.CompletedProcess:
        """Run the consumer's configure once with the given knobs; return the
        completed-process for assertion. -DCMAKE_PREFIX_PATH is wired to the
        in-tree build directory so find_package(sturm) resolves; the parent
        ctest fixture sturmc_artifacts_built guarantees the build dir is
        populated, but we do not require an INSTALL step (the precheck fires
        before find_package, and a clean build tree carries the
        sturmConfig.cmake we need)."""
        consumer_build = tmp / "consumer-build"
        cmd = [
            self.cmake_bin,
            "-S", str(SOURCE_DIR),
            "-B", str(consumer_build),
            f"-DCMAKE_PREFIX_PATH={self.build_dir}",
        ]
        if sentinel:
            cmd.append("-DSTURM_EXTERNAL_CONSUMER_SMOKE_TEST=ON")
        if shadow_paths is not None:
            joined = ";".join(str(p) for p in shadow_paths)
            cmd.append(f"-DSTURM_SHADOW_INCLUDE_PATHS={joined}")
        return subprocess.run(cmd, capture_output=True, text=True)

    # ── (1) shadow + sentinel ⇒ FATAL_ERROR with actionable diagnostic ───────
    def test_shadow_present_with_sentinel_fails_with_diagnostic(self) -> None:
        with tempfile.TemporaryDirectory(prefix="sturm-shadow-") as tmp:
            tmp_root = Path(tmp)
            fake_shadow = tmp_root / "fake-stale-prefix" / "include" / "sturm"
            fake_shadow.mkdir(parents=True)
            # Drop a stub header so the directory is plausibly a STURM
            # install (the precheck only checks that the directory *exists*,
            # but a stub keeps the fake closer to the real failure mode).
            (fake_shadow / "sturm.hpp").write_text("// stale stub\n")

            cp = self._configure(
                tmp_root, sentinel=True, shadow_paths=[fake_shadow])

            self.assertNotEqual(
                cp.returncode, 0,
                "consumer configure unexpectedly succeeded with shadow + "
                f"sentinel ON.\nstdout:\n{cp.stdout}\nstderr:\n{cp.stderr}\n",
            )
            combined = cp.stdout + cp.stderr
            # Diagnostic must name the offending path so the contributor
            # knows WHICH stale install to remove.
            self.assertIn(
                str(fake_shadow), combined,
                "diagnostic does not name the shadow path; without it the "
                "remediation step is ambiguous.\n"
                f"stdout:\n{cp.stdout}\nstderr:\n{cp.stderr}\n",
            )
            # Remediation hints — at least one of the two recipes from the
            # bd-nfmu acceptance criteria must appear (brew uninstall OR
            # rm -rf), so contributors can act on the message immediately.
            lc = combined.lower()
            self.assertTrue(
                ("brew uninstall" in lc) or ("rm -rf" in lc),
                "diagnostic missing actionable remediation "
                "(brew uninstall / rm -rf).\n"
                f"stdout:\n{cp.stdout}\nstderr:\n{cp.stderr}\n",
            )
            # Diagnostic must explain WHY it triggered (clang include-search
            # order) so the failure is self-documenting and not mistaken
            # for a build-system bug.
            self.assertTrue(
                ("isystem" in lc) or ("include" in lc and "search" in lc) or
                ("shadow" in lc) or ("stale" in lc),
                "diagnostic does not explain the root cause "
                "(include-search ordering / shadow / stale).\n"
                f"stdout:\n{cp.stdout}\nstderr:\n{cp.stderr}\n",
            )

    # ── (2) shadow present but NO sentinel ⇒ precheck silent ────────────────
    def test_shadow_present_without_sentinel_does_not_fire(self) -> None:
        """Genuine downstream consumers (no sentinel) must not be blocked."""
        with tempfile.TemporaryDirectory(prefix="sturm-shadow-") as tmp:
            tmp_root = Path(tmp)
            fake_shadow = tmp_root / "fake-stale-prefix" / "include" / "sturm"
            fake_shadow.mkdir(parents=True)
            (fake_shadow / "sturm.hpp").write_text("// stale stub\n")

            cp = self._configure(
                tmp_root, sentinel=False, shadow_paths=[fake_shadow])

            # The configure may succeed or fail for unrelated reasons (e.g.
            # find_package), but if it fails, it MUST NOT be because of the
            # shadow precheck. Look for the precheck's signature string.
            combined = cp.stdout + cp.stderr
            self.assertNotIn(
                "STURM stale-install precheck", combined,
                "shadow precheck fired without the sentinel — that would "
                "block genuine downstream consumers who knowingly accept the "
                "shadow path.\nstdout:\n{cp.stdout}\nstderr:\n{cp.stderr}\n",
            )

    # ── (3) sentinel ON but NO shadow ⇒ precheck silent ─────────────────────
    def test_no_shadow_with_sentinel_does_not_fire(self) -> None:
        """A clean machine with the sentinel on must still configure."""
        with tempfile.TemporaryDirectory(prefix="sturm-shadow-") as tmp:
            tmp_root = Path(tmp)
            non_existent = tmp_root / "definitely-does-not-exist" / "sturm"

            cp = self._configure(
                tmp_root, sentinel=True, shadow_paths=[non_existent])

            combined = cp.stdout + cp.stderr
            self.assertNotIn(
                "STURM stale-install precheck", combined,
                "shadow precheck fired against a non-existent path — that "
                "would break clean machines under run_smoke.py.\n"
                f"stdout:\n{cp.stdout}\nstderr:\n{cp.stderr}\n",
            )


if __name__ == "__main__":
    unittest.main(verbosity=2)
