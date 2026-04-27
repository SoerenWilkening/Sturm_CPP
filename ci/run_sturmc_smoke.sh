#!/usr/bin/env bash
# ci/run_sturmc_smoke.sh — STURM E6.M3 (sturm-795j.3). Second smoke
# variant: drives tests/external_consumer/main.cpp through the `sturmc`
# Python CLI (PRD §3.5 / Plan §E6.M3) instead of through the
# `add_quantum_executable` CMake helper that E6.M2 exercises. Same source,
# same golden — proves both delivery paths in PRD §3.5 work against an
# installed STURM prefix. Flow: build STURM at --parallel 6, install into
# a temp prefix, run `${prefix}/bin/sturmc <source> -o <out>` (--cxx,
# --include-dirs, --link sturm — last is a known no-op today per
# sturmc.py D3 comment), execute <out>, diff stdout vs. golden.txt. Per
# CLAUDE.md: every cmake invocation hard-capped at --parallel 6.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
CMAKE_BIN="${STURMC_CMAKE:-cmake}"
CXX_BIN="${STURMC_CXX:-c++}"
SOURCE="${REPO_ROOT}/tests/external_consumer/main.cpp"
GOLDEN="${REPO_ROOT}/tests/external_consumer/golden.txt"
command -v "${CMAKE_BIN}" >/dev/null || { echo "error: cmake not found" >&2; exit 2; }
command -v python3       >/dev/null || { echo "error: python3 not found" >&2; exit 2; }
TMP_ROOT="$(mktemp -d -t sturmc-smoke.XXXXXX)"
trap 'rm -rf "${TMP_ROOT}"' EXIT
BUILD_DIR="${TMP_ROOT}/build"
PREFIX="${TMP_ROOT}/prefix"
echo "[ci/run_sturmc_smoke] configure + build at --parallel 6"
"${CMAKE_BIN}" -S "${REPO_ROOT}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="${CXX_BIN}"
"${CMAKE_BIN}" --build "${BUILD_DIR}" --parallel 6 \
    --target sturm-transpile --target sturm-transpile-plugin
echo "[ci/run_sturmc_smoke] install into ${PREFIX}"
"${CMAKE_BIN}" --install "${BUILD_DIR}" --prefix "${PREFIX}"
[ -x "${PREFIX}/bin/sturmc" ] || { echo "error: sturmc not installed" >&2; exit 1; }
OUT="${TMP_ROOT}/external_consumer"
echo "[ci/run_sturmc_smoke] driving sturmc end-to-end"
python3 "${PREFIX}/bin/sturmc" "${SOURCE}" -o "${OUT}" \
    --cxx "${CXX_BIN}" --include-dirs "${PREFIX}/include" --link sturm --verbose
ACTUAL="$("${OUT}")"
EXPECTED="$(cat "${GOLDEN}")"
if [ "${ACTUAL}" != "${EXPECTED}" ]; then
    echo "error: stdout drift vs. golden" >&2
    echo "  got:      ${ACTUAL}" >&2
    echo "  expected: ${EXPECTED}" >&2
    exit 1
fi
echo "[ci/run_sturmc_smoke] sturmc smoke: PASS"
