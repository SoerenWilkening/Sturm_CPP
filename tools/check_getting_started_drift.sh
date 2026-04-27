#!/usr/bin/env bash
# check_getting_started_drift.sh — STURM E8.M2 (sturm-8gvh.2).
# Drives tools/extract_md_code_blocks.py against docs/getting_started.md
# and verifies the FIRST C++ fenced code block matches
# tests/external_consumer/main.cpp byte-for-byte. The walkthrough doc IS
# the smoke test; a drift here means the doc and the test would silently
# diverge. Plan §E8.M2.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${SCRIPT_DIR}/.." && pwd)"
EX="${REPO}/tools/extract_md_code_blocks.py"
DOC="${REPO}/docs/getting_started.md"
SRC="${REPO}/tests/external_consumer/main.cpp"
[ -f "${EX}" ]  || { echo "error: extractor missing: ${EX}" >&2; exit 2; }
[ -f "${DOC}" ] || { echo "error: doc missing: ${DOC}" >&2; exit 2; }
[ -f "${SRC}" ] || { echo "error: source missing: ${SRC}" >&2; exit 2; }
if python3 "${EX}" "${DOC}" "${SRC}"; then
    echo "getting_started drift: no drift (first cpp block matches main.cpp)."
    exit 0
fi
echo "" >&2
echo "error: docs/getting_started.md first cpp block has drifted from" >&2
echo "       tests/external_consumer/main.cpp. Update the doc to match" >&2
echo "       the source byte-for-byte." >&2
exit 1
