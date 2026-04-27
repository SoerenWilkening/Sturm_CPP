# check_when_freevar_diagnostic.cmake — E7.M3 integration probe.
#
# Clones the PM3-4 `check_when_operand_mutation_diagnostic.cmake` shape
# (which itself clones PM3-3 `check_missing_adjoint_diagnostic.cmake`):
# run a known-behaviour fixture through the standalone `sturm-transpile`
# binary and assert on the resulting stderr substring set + exit code.
#
# Parameters
# ----------
#   -DTRANSPILE_BIN=<abs path>     — sturm-transpile binary
#                                    (via $<TARGET_FILE:...>)
#   -DSOURCE=<abs path>            — tests/transpiler/fixtures/
#                                    when_freevar_*_input.cpp (positive)
#                                    or when_freevar_clean.cpp (negative)
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
#                                    — pins the file-loc plumbing the
#                                    E7.M2 helper funnels through
#                                    `SourceManager::getFileLoc(...)`.
#
# Contract (E7.M3 acceptance)
# ---------------------------
# Positive fixture (MUST_CONTAIN + EXPECTED_LINE):
#   - sturm-transpile exits NON-ZERO. The E7.M2 helper registers its
#     custom diag-ID at `DiagnosticIDs::Error`, so `DiagnosticsEngine`'s
#     fatal-error counter increments and the standalone driver returns
#     a non-zero exit.
#   - stderr contains MUST_CONTAIN (a substring of the locked-down
#     E7.M2 format string).
#   - stderr contains `<basename(SOURCE)>:<EXPECTED_LINE>:` so the
#     diagnostic cites the user's source file at the mutation line
#     (not `<memory-buffer>`).
#   - stderr does NOT contain `<memory-buffer>` — the E7.M2 helper
#     uses `getFileLoc` to keep diagnostics anchored on the user's
#     filename.
#
# Negative fixture (MUST_NOT_CONTAIN):
#   - sturm-transpile exits ZERO.
#   - stderr does NOT contain MUST_NOT_CONTAIN.
#
# Script exits 0 on success, FATAL_ERROR on any contract violation.

foreach(var TRANSPILE_BIN SOURCE OUTPUT_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR
            "check_when_freevar_diagnostic: ${var} not set")
    endif()
endforeach()

if(NOT DEFINED MUST_CONTAIN AND NOT DEFINED MUST_NOT_CONTAIN)
    message(FATAL_ERROR
        "check_when_freevar_diagnostic: neither MUST_CONTAIN "
        "nor MUST_NOT_CONTAIN was supplied — caller must pick exactly "
        "one.")
endif()
if(DEFINED MUST_CONTAIN AND DEFINED MUST_NOT_CONTAIN)
    message(FATAL_ERROR
        "check_when_freevar_diagnostic: both MUST_CONTAIN and "
        "MUST_NOT_CONTAIN were supplied — caller must pick exactly one.")
endif()

foreach(path "${TRANSPILE_BIN}" "${SOURCE}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "check_when_freevar_diagnostic: required file "
            "missing: ${path}")
    endif()
endforeach()

# Fresh scratch dir so a stale output from a previous run does not race
# against the present assertion.
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
    if(_transpile_rc EQUAL 0)
        message(FATAL_ERROR
            "check_when_freevar_diagnostic: sturm-transpile exited "
            "zero on the positive fixture. The E7.M2 checker must "
            "emit DiagnosticsEngine::Error, NOT Warning — a zero "
            "exit indicates the severity was incorrectly demoted or "
            "the matcher did not fire at all.\n"
            "stdout:\n${_transpile_out}\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Negative `<memory-buffer>` check — same plumbing probe as
    # PM3-2 / PM3-3 / PM3-4. The E7.M2 helper funnels its loc through
    # `SourceManager::getFileLoc(...)` so diagnostics resolve to the
    # user's filename. If `<memory-buffer>` surfaces, that plumbing is
    # broken.
    string(FIND "${_transpile_err}" "<memory-buffer>" _memory_buf_pos)
    if(NOT _memory_buf_pos EQUAL -1)
        message(FATAL_ERROR
            "check_when_freevar_diagnostic: stderr references "
            "`<memory-buffer>` — `check_when_freevar_writes` lost "
            "the user's filename when converting the matched loc. "
            "Inspect `when_freevar_check.cpp`'s `WriteFinder::check` "
            "site: it must hand a `SourceManager::getFileLoc(...)` "
            "result to `engine.Report(...)` so the engine cites the "
            "user's file, not the plugin's nested MemoryBuffer.\n"
            "stderr:\n${_transpile_err}")
    endif()

    string(FIND "${_transpile_err}" "${MUST_CONTAIN}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR
            "check_when_freevar_diagnostic: stderr did NOT contain "
            "the required substring `${MUST_CONTAIN}`.\n"
            "stderr:\n${_transpile_err}")
    endif()

    if(DEFINED EXPECTED_LINE)
        set(_required_loc "${_src_name}:${EXPECTED_LINE}:")
        string(FIND "${_transpile_err}" "${_required_loc}" _found_loc)
        if(_found_loc EQUAL -1)
            message(FATAL_ERROR
                "check_when_freevar_diagnostic: stderr does not "
                "cite the expected user-source location.\n"
                "  required substring: ${_required_loc}\n"
                "  source file:        ${SOURCE}\n"
                "  expected line:      ${EXPECTED_LINE}\n"
                "stderr:\n${_transpile_err}")
        endif()
    endif()

    message(STATUS
        "check_when_freevar_diagnostic: OK — stderr contained "
        "`${MUST_CONTAIN}`, exit code was non-zero, and "
        "`<memory-buffer>` did not surface.")
else()
    # Negative branch: expect zero exit AND substring absent.
    if(NOT _transpile_rc EQUAL 0)
        message(FATAL_ERROR
            "check_when_freevar_diagnostic: sturm-transpile exited "
            "non-zero (rc=${_transpile_rc}) on the negative fixture. "
            "The E7.M2 checker's body-local exclusion must skip "
            "writes targeting variables declared inside the WHEN "
            "body.\n"
            "stdout:\n${_transpile_out}\n"
            "stderr:\n${_transpile_err}")
    endif()

    string(FIND "${_transpile_err}" "${MUST_NOT_CONTAIN}" _pos)
    if(NOT _pos EQUAL -1)
        message(FATAL_ERROR
            "check_when_freevar_diagnostic: stderr contained the "
            "forbidden substring `${MUST_NOT_CONTAIN}` at offset "
            "${_pos}. The E7.M2 checker fired on a body-local shape "
            "it should have rejected.\n"
            "stderr:\n${_transpile_err}")
    endif()

    message(STATUS
        "check_when_freevar_diagnostic: OK — stderr did NOT contain "
        "`${MUST_NOT_CONTAIN}` and exit code was zero.")
endif()
