# check_missing_adjoint_diagnostic.cmake — PM3-3 Class 3 diagnostic probe.
#
# Clones the PM2-8 `check_source_map_diagnostic.cmake` / PM3-2
# `check_outer_var_guard_diagnostic.cmake` pattern: run a known-behaviour
# fixture through the standalone `sturm-transpile` binary and assert on
# the resulting stderr substring set + exit code.
#
# Parameters
# ----------
#   -DTRANSPILE_BIN=<abs path>     — sturm-transpile binary
#                                    (via $<TARGET_FILE:...>)
#   -DSOURCE=<abs path>            — tests/transpiler/fixtures/
#                                    missing_adjoint_diagnostic_input.cpp
#                                    (positive) or
#                                    missing_adjoint_clean.cpp
#                                    (negative)
#   -DOUTPUT_DIR=<abs path>        — scratch directory for the transpile
#                                    output
#   -DMUST_CONTAIN=<substring>     — (optional) substring that must appear
#                                    in stderr AND exit code MUST be
#                                    non-zero (Error severity).
#                                    Mutually exclusive with
#                                    MUST_NOT_CONTAIN.
#   -DMUST_NOT_CONTAIN=<substring> — (optional) substring that must NOT
#                                    appear in stderr AND exit code MUST
#                                    be zero (no error). Mutually
#                                    exclusive with MUST_CONTAIN.
#   -DEXPECTED_LINE=<int>          — (optional; paired with MUST_CONTAIN)
#                                    1-based user-source line the
#                                    diagnostic MUST cite. When set, the
#                                    script also asserts that stderr
#                                    contains `<basename(SOURCE)>:<line>:`
#                                    — mirrors the PM2-8 "did the
#                                    source-map plumbing survive?" check.
#
# Contract (PM3-3 acceptance)
# ---------------------------
# Positive fixture (MUST_CONTAIN + EXPECTED_LINE):
#   - sturm-transpile exits NON-ZERO (Error severity — compilation must
#     abort, not continue).
#   - stderr contains MUST_CONTAIN (the locked-down PM3-3 format
#     fragment).
#   - stderr contains `<basename(SOURCE)>:<EXPECTED_LINE>:` — proving the
#     diagnostic cites the user's source file at the user's originating
#     CallExpr line (NOT `<memory-buffer>`, NOT a synthesized buffer
#     line).
#   - stderr does NOT contain `<memory-buffer>` — same plumbing probe as
#     PM2-8's check.
#
# Negative fixture (MUST_NOT_CONTAIN):
#   - sturm-transpile exits ZERO (no Error — the silent-early-return
#     branch still applies when outputs_mask == 0 on a registry miss).
#   - stderr does NOT contain MUST_NOT_CONTAIN.
#
# Script exits 0 on success, FATAL_ERROR on any contract violation.

foreach(var TRANSPILE_BIN SOURCE OUTPUT_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR
            "check_missing_adjoint_diagnostic: ${var} not set")
    endif()
endforeach()

if(NOT DEFINED MUST_CONTAIN AND NOT DEFINED MUST_NOT_CONTAIN)
    message(FATAL_ERROR
        "check_missing_adjoint_diagnostic: neither MUST_CONTAIN nor "
        "MUST_NOT_CONTAIN was supplied — caller must pick exactly one.")
endif()
if(DEFINED MUST_CONTAIN AND DEFINED MUST_NOT_CONTAIN)
    message(FATAL_ERROR
        "check_missing_adjoint_diagnostic: both MUST_CONTAIN and "
        "MUST_NOT_CONTAIN were supplied — caller must pick exactly one.")
endif()

foreach(path "${TRANSPILE_BIN}" "${SOURCE}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "check_missing_adjoint_diagnostic: required file missing: "
            "${path}")
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
    # Positive branch: expect non-zero exit (Error severity) AND
    # substring present.

    # Contract #1 (positive): non-zero exit. The PM3-3 matcher emits a
    # `DiagnosticsEngine::Error`, so the standalone driver MUST finish
    # with rc != 0 — any zero exit would indicate the severity was
    # incorrectly demoted to Warning and the "compilation aborts"
    # acceptance criterion would be broken.
    if(_transpile_rc EQUAL 0)
        message(FATAL_ERROR
            "check_missing_adjoint_diagnostic: sturm-transpile exited "
            "zero on the positive fixture. The PM3-3 matcher must emit "
            "DiagnosticsEngine::Error, NOT Warning — a zero exit "
            "indicates the severity was incorrectly demoted.\n"
            "stdout:\n${_transpile_out}\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Contract #2 (positive): negative `<memory-buffer>` check. Same
    # plumbing probe as PM2-8 / PM3-2 — the PM3-3 matcher funnels its
    # loc through `SourceManager::getFileLoc(...)` so diagnostics resolve
    # to the user's filename. If `<memory-buffer>` surfaces, that
    # plumbing is broken.
    string(FIND "${_transpile_err}" "<memory-buffer>" _memory_buf_pos)
    if(NOT _memory_buf_pos EQUAL -1)
        message(FATAL_ERROR
            "check_missing_adjoint_diagnostic: stderr references "
            "`<memory-buffer>` — the PM3-0 `DiagContext` lost the "
            "user's filename when converting the matched loc. Inspect "
            "`matcher_user_routine.cpp`'s PM3-3 detection site: it "
            "must hand a `SourceManager::getFileLoc(...)` result to "
            "`diag.report_missing_adjoint(...)` so the engine's "
            "Report path cites the user's file, not the plugin's "
            "nested MemoryBuffer.\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Contract #3 (positive): substring MUST_CONTAIN present.
    string(FIND "${_transpile_err}" "${MUST_CONTAIN}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR
            "check_missing_adjoint_diagnostic: stderr did NOT contain "
            "the required substring `${MUST_CONTAIN}`.\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Contract #4 (positive; optional): EXPECTED_LINE. When supplied,
    # the stderr MUST cite `<basename>:<line>:` so source-map plumbing
    # correctness is pinned alongside the message-content check.
    if(DEFINED EXPECTED_LINE)
        set(_required_loc "${_src_name}:${EXPECTED_LINE}:")
        string(FIND "${_transpile_err}" "${_required_loc}" _found_loc)
        if(_found_loc EQUAL -1)
            message(FATAL_ERROR
                "check_missing_adjoint_diagnostic: stderr does not "
                "cite the expected user-source location.\n"
                "  required substring: ${_required_loc}\n"
                "  source file:        ${SOURCE}\n"
                "  expected line:      ${EXPECTED_LINE}\n"
                "stderr:\n${_transpile_err}")
        endif()
    endif()

    message(STATUS
        "check_missing_adjoint_diagnostic: OK — stderr contained "
        "`${MUST_CONTAIN}`, exit code was non-zero, and "
        "`<memory-buffer>` did not surface.")
else()
    # Negative branch: expect zero exit AND substring absent.

    # Contract #1 (negative): zero exit. The unregistered-but-INPUT-only
    # call path is a silent early-return; no diagnostic, no error. A
    # non-zero exit indicates the matcher fired on a shape it should
    # have rejected.
    if(NOT _transpile_rc EQUAL 0)
        message(FATAL_ERROR
            "check_missing_adjoint_diagnostic: sturm-transpile exited "
            "non-zero (rc=${_transpile_rc}) on the negative fixture. "
            "The PM3-3 matcher's silent early-return (registry miss + "
            "outputs_mask == 0) must NOT surface a diagnostic.\n"
            "stdout:\n${_transpile_out}\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Contract #2 (negative): forbidden substring absent.
    string(FIND "${_transpile_err}" "${MUST_NOT_CONTAIN}" _pos)
    if(NOT _pos EQUAL -1)
        message(FATAL_ERROR
            "check_missing_adjoint_diagnostic: stderr contained the "
            "forbidden substring `${MUST_NOT_CONTAIN}` at offset "
            "${_pos}. The matcher fired on a shape it should have "
            "rejected (const qbool& / const qint& parameters only — "
            "outputs_mask == 0 — should take the silent early-return).\n"
            "stderr:\n${_transpile_err}")
    endif()

    message(STATUS
        "check_missing_adjoint_diagnostic: OK — stderr did NOT "
        "contain `${MUST_NOT_CONTAIN}` and exit code was zero.")
endif()
