#!/usr/bin/env bash
# ci/run_external_smoke.sh — STURM E6.M2 (sturm-795j.2).
#
# CI driver for the external_consumer smoke test (PRD §3.6 / Plan §E6.M2).
# Drives `tests/external_consumer/` end-to-end against an INSTALLED STURM
# prefix:
#   1. Configure + build STURM (only the targets the install rules
#      reference) into a temp build tree at --parallel 6.
#   2. `cmake --install` the parent project into a temp prefix.
#   3. Configure + build the standalone `tests/external_consumer/` against
#      that prefix at --parallel 6.
#   4. Run the produced binary.
#   5. Diff stdout vs. tests/external_consumer/golden.txt.
#
# Steps 2-5 are owned by `tests/external_consumer/run_smoke.py` (the same
# driver ctest invokes via the `external_consumer_smoke` test from
# tests/packaging/CMakeLists.txt). This wrapper exists because the spec
# wants a shell entry point for CI; matching `tools/check_*_drift.sh`
# style (set -euo pipefail, REPO_ROOT from BASH_SOURCE) keeps the
# driver layer idiomatic to the repo while the bytewise install-and-diff
# logic stays in one place.
#
# Per project rule (CLAUDE.md): every cmake invocation is hard-capped at
# --parallel 6. NO EXCEPTIONS.
#
# Optional env:
#   STURMC_BUILD_DIR  Reuse an existing build tree (skip step 1).
#                     Must already contain the install-rule targets.
#   STURMC_CMAKE      cmake binary (default: `cmake` on PATH).
#   STURMC_CXX        C++ compiler (default: `c++` on PATH).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

CMAKE_BIN="${STURMC_CMAKE:-cmake}"
CXX_BIN="${STURMC_CXX:-c++}"

if ! command -v "${CMAKE_BIN}" >/dev/null 2>&1; then
    echo "error: cmake not found (looked for '${CMAKE_BIN}')" >&2
    exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "error: python3 not found (required to run run_smoke.py)" >&2
    exit 2
fi

# Temp scratch root: hosts the build tree (when we own it) and is the
# parent of the prefix/consumer-build dirs that run_smoke.py creates.
TMP_ROOT="$(mktemp -d -t sturm-ext-ci.XXXXXX)"
trap 'rm -rf "${TMP_ROOT}"' EXIT

# Step 1 — ensure a STURM build tree exists with the install-rule
# artifacts up-to-date. CI starts from a clean checkout so we always
# do a fresh configure-and-build here unless the caller supplied a
# pre-built tree via STURMC_BUILD_DIR.
if [ -n "${STURMC_BUILD_DIR:-}" ]; then
    BUILD_DIR="$(cd "${STURMC_BUILD_DIR}" && pwd)"
    echo "[ci/run_external_smoke] reusing build tree: ${BUILD_DIR}"
else
    BUILD_DIR="${TMP_ROOT}/build"
    echo "[ci/run_external_smoke] configuring fresh build tree: ${BUILD_DIR}"
    "${CMAKE_BIN}" -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_COMPILER="${CXX_BIN}"
fi

# `cmake --install` silently skips install(TARGETS ...) entries whose
# build step has not run, so build the install-rule targets explicitly.
# These mirror the FIXTURES_SETUP `sturmc_build_artifacts` test in
# tests/packaging/CMakeLists.txt (sturm-vr0v.2). Plain header install
# rules + install(PROGRAMS tools/sturmc.py) don't need a build target.
echo "[ci/run_external_smoke] building install-rule targets at --parallel 6"
"${CMAKE_BIN}" --build "${BUILD_DIR}" --parallel 6 \
    --target sturm-transpile --target sturm-transpile-plugin

# Steps 2-5 — delegate to the Python driver. It owns the install into a
# per-process temp prefix (which it creates under the system tempdir, NOT
# under TMP_ROOT — that's fine, it cleans up after itself), the consumer
# configure/build at --parallel 6, the binary run, and the byte-for-byte
# golden diff.
echo "[ci/run_external_smoke] handing off to run_smoke.py"
STURMC_BUILD_DIR="${BUILD_DIR}" \
STURMC_CXX="${CXX_BIN}" \
STURMC_CMAKE="${CMAKE_BIN}" \
    python3 "${REPO_ROOT}/tests/external_consumer/run_smoke.py"

echo "[ci/run_external_smoke] external_consumer smoke: PASS"
