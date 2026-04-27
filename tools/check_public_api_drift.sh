#!/usr/bin/env bash
# check_public_api_drift.sh — STURM E8.M1 (sturm-8gvh.1).
# Re-runs tools/extract_public_api.py and diffs its stdout against the
# committed snapshot at docs/public_api.txt. Exit 0 on byte-identical
# match, non-zero + diff on drift. The companion human doc is
# docs/public_api.md; the .txt snapshot is the machine-readable source
# of truth (matching the pattern of docs/transpiler_emit_targets.tsv).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "${SCRIPT_DIR}/.." && pwd)"
EX="${REPO}/tools/extract_public_api.py"
TXT="${REPO}/docs/public_api.txt"
[ -f "${EX}" ]  || { echo "error: extractor missing: ${EX}" >&2; exit 2; }
[ -f "${TXT}" ] || { echo "error: snapshot missing: ${TXT}; regenerate with python3 ${EX} > ${TXT}" >&2; exit 2; }
CUR="$(mktemp)"; trap 'rm -f "${CUR}"' EXIT
python3 "${EX}" > "${CUR}"
if diff -u "${TXT}" "${CUR}"; then
    echo "public-api drift: no drift (matches docs/public_api.txt)."
    exit 0
fi
echo "" >&2
echo "error: public-api drift detected." >&2
echo "       The committed docs/public_api.txt is out of sync with" >&2
echo "       the symbols reachable through include/sturm/sturm.hpp." >&2
echo "       Regenerate with: python3 tools/extract_public_api.py > docs/public_api.txt" >&2
echo "       and update docs/public_api.md to match the new symbol set." >&2
exit 1
