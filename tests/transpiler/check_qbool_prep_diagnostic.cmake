# check_qbool_prep_diagnostic.cmake — PN-6 PN-5 qbool(p) prep diagnostic probe.
#
# Clones the PM3-2 `check_outer_var_guard_diagnostic.cmake` pattern: run a
# known-behaviour fixture through the standalone `sturm-transpile`
# binary and assert on the resulting stderr substring set + exit code.
# Unlike PM3-2 (which is positive-only), this harness handles BOTH the
# positive (MUST_CONTAIN + EXPECTED_LINE) and negative (MUST_NOT_CONTAIN)
# acceptance contracts in a single script — mirroring the shape used by
# `check_when_operand_mutation_diagnostic.cmake` / `check_quantum_to_
# classical_cond_diagnostic.cmake`.
#
# Parameters
# ----------
#   -DTRANSPILE_BIN=<abs path>     — sturm-transpile binary
#                                    (via $<TARGET_FILE:...>)
#   -DSOURCE=<abs path>            — tests/transpiler/fixtures/
#                                    qbool_prep_in_when.cpp (positive) or
#                                    qbool_prep_top_level_clean.cpp
#                                    (negative)
#   -DOUTPUT_DIR=<abs path>        — scratch directory for the transpile
#                                    output
#   -DMUST_CONTAIN=<substring>     — (optional) substring that must appear
#                                    in stderr AND exit code MUST be
#                                    ZERO (Warning severity, not Error —
#                                    compilation continues so every PN-5
#                                    shape is surfaced in a single pass).
#                                    Mutually exclusive with
#                                    MUST_NOT_CONTAIN.
#   -DMUST_NOT_CONTAIN=<substring> — (optional) substring that must NOT
#                                    appear in stderr AND exit code MUST
#                                    be zero (no warning). Mutually
#                                    exclusive with MUST_CONTAIN.
#   -DEXPECTED_LINE=<int>          — (optional; paired with MUST_CONTAIN)
#                                    1-based user-source line the
#                                    diagnostic MUST cite. When set, the
#                                    script also asserts that stderr
#                                    contains `<basename(SOURCE)>:<line>:`
#                                    — mirrors the PM2-8 / PM3-2 /
#                                    PM3-3 / PM3-4 "did the source-map
#                                    plumbing survive?" check.
#
# Contract (PN-6 acceptance)
# ---------------------------
# Positive fixture (MUST_CONTAIN + EXPECTED_LINE):
#   - sturm-transpile exits ZERO. Per the PN-5 plan §5, the matcher
#     emits `DiagnosticsEngine::Warning` (NOT Error) — compilation must
#     continue so every PN-5 violation in the TU is surfaced in a single
#     pass. Any non-zero exit would indicate the severity was
#     incorrectly promoted to Error and the "compilation continues"
#     acceptance criterion would be broken.
#   - stderr contains MUST_CONTAIN (the locked-down PN-5 format
#     fragment from `DiagContext::report_prep_in_uncompute_scope`).
#   - stderr contains `<basename(SOURCE)>:<EXPECTED_LINE>:` — proving
#     the diagnostic cites the user's source file at the VarDecl line
#     (NOT `<memory-buffer>`).
#   - stderr does NOT contain `<memory-buffer>` — same plumbing probe
#     as PM2-8 / PM3-2 / PM3-3 / PM3-4. The PN-5 matcher funnels its
#     loc through `SourceManager::getFileLoc(...)` before handing it to
#     `DiagContext::report_prep_in_uncompute_scope(...)`, so
#     diagnostics resolve to the user's filename rather than an
#     anonymous memory buffer.
#
# Negative fixture (MUST_NOT_CONTAIN):
#   - sturm-transpile exits ZERO (no warning — top-level prep is a
#     valid P5 item 1 use; the matcher's `classify_scope_kind` pathway
#     early-returns on `ScopeKind::Function`).
#   - stderr does NOT contain MUST_NOT_CONTAIN.
#
# Script exits 0 on success, FATAL_ERROR on any contract violation.

foreach(var TRANSPILE_BIN SOURCE OUTPUT_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR
            "check_qbool_prep_diagnostic: ${var} not set")
    endif()
endforeach()

if(NOT DEFINED MUST_CONTAIN AND NOT DEFINED MUST_NOT_CONTAIN)
    message(FATAL_ERROR
        "check_qbool_prep_diagnostic: neither MUST_CONTAIN nor "
        "MUST_NOT_CONTAIN was supplied — caller must pick exactly "
        "one.")
endif()
if(DEFINED MUST_CONTAIN AND DEFINED MUST_NOT_CONTAIN)
    message(FATAL_ERROR
        "check_qbool_prep_diagnostic: both MUST_CONTAIN and "
        "MUST_NOT_CONTAIN were supplied — caller must pick exactly one.")
endif()

foreach(path "${TRANSPILE_BIN}" "${SOURCE}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "check_qbool_prep_diagnostic: required file missing: ${path}")
    endif()
endforeach()

# Fresh scratch dir so a stale output from a previous run does not
# race against the present assertion.
file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

get_filename_component(_src_dir  "${SOURCE}" DIRECTORY)
get_filename_component(_src_name "${SOURCE}" NAME)

# Run the standalone driver from the fixture's own directory with a
# relative input path — matches the `run_snapshot.cmake` convention so
# any stderr filename the Clang diagnostic prints is a clean basename,
# not an absolute build-tree path. Keeps the required-substring
# assertion stable across CI runners whose workspaces sit at different
# absolute paths.
execute_process(
    COMMAND "${TRANSPILE_BIN}" "${_src_name}" --output-dir "${OUTPUT_DIR}"
    WORKING_DIRECTORY "${_src_dir}"
    RESULT_VARIABLE _transpile_rc
    OUTPUT_VARIABLE _transpile_out
    ERROR_VARIABLE  _transpile_err)

if(DEFINED MUST_CONTAIN)
    # Positive branch: expect ZERO exit (Warning severity) AND
    # substring present.

    # Contract #1 (positive): zero exit. The PN-5 matcher emits a
    # `DiagnosticsEngine::Warning`, NOT Error — compilation must
    # continue so every PN-5 violation in the TU is surfaced in a
    # single pass. A non-zero exit would indicate the severity was
    # incorrectly promoted to Error and the "compilation continues"
    # acceptance criterion would be broken.
    if(NOT _transpile_rc EQUAL 0)
        message(FATAL_ERROR
            "check_qbool_prep_diagnostic: sturm-transpile exited "
            "non-zero (rc=${_transpile_rc}) on the positive fixture. "
            "The PN-5 matcher must emit DiagnosticsEngine::Warning, "
            "NOT Error; a non-zero exit indicates the severity was "
            "incorrectly promoted.\n"
            "stdout:\n${_transpile_out}\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Contract #2 (positive): negative `<memory-buffer>` check. Same
    # plumbing probe as PM2-8 / PM3-2 / PM3-3 / PM3-4 — the PN-5
    # matcher funnels its loc through `SourceManager::getFileLoc(...)`
    # before handing it to `DiagContext::report_prep_in_uncompute_
    # scope(...)`, so diagnostics resolve to the user's filename. If
    # `<memory-buffer>` surfaces, that plumbing is broken.
    string(FIND "${_transpile_err}" "<memory-buffer>" _memory_buf_pos)
    if(NOT _memory_buf_pos EQUAL -1)
        message(FATAL_ERROR
            "check_qbool_prep_diagnostic: stderr references "
            "`<memory-buffer>` — the PM3-0 `DiagContext` lost the "
            "user's filename when converting the matched loc. Inspect "
            "`matcher_qbool_prep.cpp`'s detection site: it must hand "
            "a `SourceManager::getFileLoc(...)` result to "
            "`diag.report_prep_in_uncompute_scope(...)` so the "
            "engine's Report path cites the user's file, not the "
            "plugin's nested MemoryBuffer.\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Contract #3 (positive): substring MUST_CONTAIN present.
    string(FIND "${_transpile_err}" "${MUST_CONTAIN}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR
            "check_qbool_prep_diagnostic: stderr did NOT contain the "
            "required substring `${MUST_CONTAIN}`. The PN-5 matcher "
            "either did not fire or the format string was changed "
            "without updating this test.\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Contract #4 (positive; optional): EXPECTED_LINE. When supplied,
    # stderr MUST cite `<basename>:<line>:` so source-map plumbing
    # correctness is pinned alongside the message-content check.
    if(DEFINED EXPECTED_LINE)
        set(_required_loc "${_src_name}:${EXPECTED_LINE}:")
        string(FIND "${_transpile_err}" "${_required_loc}" _found_loc)
        if(_found_loc EQUAL -1)
            message(FATAL_ERROR
                "check_qbool_prep_diagnostic: stderr does not cite "
                "the expected user-source location.\n"
                "  required substring: ${_required_loc}\n"
                "  source file:        ${SOURCE}\n"
                "  expected line:      ${EXPECTED_LINE}\n"
                "stderr:\n${_transpile_err}")
        endif()
    endif()

    message(STATUS
        "check_qbool_prep_diagnostic: OK — stderr contained "
        "`${MUST_CONTAIN}`, exit code was zero, and "
        "`<memory-buffer>` did not surface.")
else()
    # Negative branch: expect zero exit AND substring absent.

    # Contract #1 (negative): zero exit. The clean fixture has a prep
    # at function-body top-level scope; `classify_scope_kind` returns
    # `ScopeKind::Function` and the matcher early-returns silently.
    # A non-zero exit indicates the matcher fired on a shape it should
    # have rejected (top-level prep is a valid P5 item 1 use).
    if(NOT _transpile_rc EQUAL 0)
        message(FATAL_ERROR
            "check_qbool_prep_diagnostic: sturm-transpile exited "
            "non-zero (rc=${_transpile_rc}) on the negative fixture. "
            "The PN-5 matcher's scope classification must skip "
            "top-level prep (ScopeKind::Function is a valid P5 item 1 "
            "use).\n"
            "stdout:\n${_transpile_out}\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Contract #2 (negative): forbidden substring absent.
    string(FIND "${_transpile_err}" "${MUST_NOT_CONTAIN}" _pos)
    if(NOT _pos EQUAL -1)
        message(FATAL_ERROR
            "check_qbool_prep_diagnostic: stderr contained the "
            "forbidden substring `${MUST_NOT_CONTAIN}` at offset "
            "${_pos}. The matcher fired on a shape it should have "
            "rejected — top-level prep is a valid P5 item 1 use "
            "and the PN-5 matcher's `ScopeKind::Function` pathway "
            "must early-return silently.\n"
            "stderr:\n${_transpile_err}")
    endif()

    message(STATUS
        "check_qbool_prep_diagnostic: OK — stderr did NOT contain "
        "`${MUST_NOT_CONTAIN}` and exit code was zero.")
endif()
