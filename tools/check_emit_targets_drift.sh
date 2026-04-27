#!/usr/bin/env bash
# check_emit_targets_drift.sh — STURM Epic E1.M3 (sturm-xq05.3).
#
# Re-runs tools/audit_emit_targets.py and diffs its stdout against the
# committed side-table docs/transpiler_emit_targets.tsv. Exit 0 on
# byte-identical match, non-zero + diff on drift. LOC budget: <=50.

set -euo pipefail

# Resolve repo root from this script's location so the check works from
# any cwd (CI, dev shell, git hook).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

AUDIT="${REPO_ROOT}/tools/audit_emit_targets.py"
COMMITTED="${REPO_ROOT}/docs/transpiler_emit_targets.tsv"

if [ ! -f "${AUDIT}" ]; then
    echo "error: audit script not found at ${AUDIT}" >&2
    exit 2
fi
if [ ! -f "${COMMITTED}" ]; then
    echo "error: committed TSV not found at ${COMMITTED}" >&2
    echo "       regenerate with: python3 ${AUDIT} > ${COMMITTED}" >&2
    exit 2
fi

CURRENT="$(mktemp)"
trap 'rm -f "${CURRENT}"' EXIT

python3 "${AUDIT}" --repo-root "${REPO_ROOT}" > "${CURRENT}"

if diff -u "${COMMITTED}" "${CURRENT}"; then
    echo "emit-target audit: no drift (matches docs/transpiler_emit_targets.tsv)."
    exit 0
fi

echo "" >&2
echo "error: emit-target drift detected." >&2
echo "       The committed docs/transpiler_emit_targets.tsv is out of sync" >&2
echo "       with the current transpiler/src tree. Regenerate with:" >&2
echo "           python3 tools/audit_emit_targets.py > docs/transpiler_emit_targets.tsv" >&2
echo "       and update docs/transpiler_emit_targets.md classifications." >&2
exit 1
