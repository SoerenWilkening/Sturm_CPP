# check_reversible_diagnostic.cmake — Phase P / P-5 + Phase Q / Q-3
# reversible-diagnostic probe.
#
# Clones the PN-6 `check_qbool_prep_diagnostic.cmake` pattern: run a
# known-behaviour fixture through the standalone `sturm-transpile`
# binary and assert on the resulting stderr substring set + exit code.
# Handles BOTH the positive (MUST_CONTAIN + EXPECTED_LINE — used once
# the Q-B / P-C validators wire into `transpile_consumer.cpp` and the
# diagnostic actually fires) and negative (MUST_NOT_CONTAIN — used
# today, while the validators live in their unit-test harness only
# and have not been glued into the TU-level driver yet) acceptance
# contracts in a single script.
#
# Parameters
# ----------
#   -DTRANSPILE_BIN=<abs path>     — sturm-transpile binary
#                                    (via $<TARGET_FILE:...>)
#   -DSOURCE=<abs path>            — the fixture (.cpp) to feed to the
#                                    driver. Typically a
#                                    `tests/transpiler/fixtures/
#                                    reversible_sig_*` or
#                                    `reversible_reject_*` file.
#   -DOUTPUT_DIR=<abs path>        — scratch directory for the
#                                    transpile output.
#   -DMUST_CONTAIN=<substring>     — (optional) substring that must
#                                    appear in stderr AND the
#                                    sturm-transpile exit MUST be
#                                    non-zero (Error severity — the
#                                    validators emit
#                                    `DiagnosticsEngine::Error` per
#                                    P9d, so compilation must abort).
#                                    Mutually exclusive with
#                                    MUST_NOT_CONTAIN.
#   -DMUST_NOT_CONTAIN=<substring> — (optional) substring that must
#                                    NOT appear in stderr AND the
#                                    sturm-transpile exit MUST be
#                                    zero (no diagnostic fires —
#                                    pass-through mode, used today
#                                    because the validators live in
#                                    their unit-test surface only).
#                                    Mutually exclusive with
#                                    MUST_CONTAIN and EMPTY_STDERR.
#   -DEMPTY_STDERR=<anything>      — (optional; Phase T T-4 sturm-xrob.5)
#                                    assert stderr is EMPTY AND
#                                    sturm-transpile exits zero. Used
#                                    by the P-C happy-path fixture
#                                    (`reversible_oracle.cpp`) once the
#                                    P-C / Q-B validators are wired
#                                    into `transpile_consumer.cpp`: a
#                                    clean positive body must trip no
#                                    validator, no warning, no Error.
#                                    The defining variable's VALUE is
#                                    ignored — its mere definition
#                                    selects the empty-stderr branch.
#                                    Mutually exclusive with both
#                                    MUST_CONTAIN and MUST_NOT_CONTAIN.
#   -DEXPECTED_LINE=<int>          — (optional; paired with MUST_CONTAIN)
#                                    1-based user-source line the
#                                    diagnostic MUST cite. When set,
#                                    the script also asserts that
#                                    stderr contains
#                                    `<basename(SOURCE)>:<line>:` —
#                                    mirrors the PM2-8 / PM3-2 / PM3-3
#                                    / PM3-4 / PN-6 source-map
#                                    plumbing check.
#
# Contract
# --------
# Positive fixture (MUST_CONTAIN + EXPECTED_LINE):
#   - sturm-transpile exits NON-ZERO (Error severity — compilation
#     aborts because P9d treats reversible-body rejects as hard
#     errors at the forward-function definition site).
#   - stderr contains MUST_CONTAIN (the locked-down format fragment
#     from the matching `DiagContext::report_reversible_*` method).
#   - stderr contains `<basename(SOURCE)>:<EXPECTED_LINE>:` — proving
#     the diagnostic cites the user's source file at the offending
#     decl/stmt line (NOT `<memory-buffer>`, NOT a synthesized buffer
#     line).
#   - stderr does NOT contain `<memory-buffer>` — the matcher's loc
#     funnels through `SourceManager::getFileLoc(...)` before the
#     DiagContext reports the Error, so the engine's Report path
#     MUST cite the user's file.
#
# Negative fixture (MUST_NOT_CONTAIN):
#   - sturm-transpile exits ZERO (no diagnostic — pass-through
#     because either (a) the fixture is a clean positive that passes
#     validation, or (b) the validator that would fire on this
#     shape has not yet been wired into `transpile_consumer.cpp`,
#     and the substring check regresses the "silent today" state).
#   - stderr does NOT contain MUST_NOT_CONTAIN.
#
# Script exits 0 on success, FATAL_ERROR on any contract violation.

foreach(var TRANSPILE_BIN SOURCE OUTPUT_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR
            "check_reversible_diagnostic: ${var} not set")
    endif()
endforeach()

set(_mode_count 0)
if(DEFINED MUST_CONTAIN)
    math(EXPR _mode_count "${_mode_count} + 1")
endif()
if(DEFINED MUST_NOT_CONTAIN)
    math(EXPR _mode_count "${_mode_count} + 1")
endif()
if(DEFINED EMPTY_STDERR)
    math(EXPR _mode_count "${_mode_count} + 1")
endif()
if(_mode_count EQUAL 0)
    message(FATAL_ERROR
        "check_reversible_diagnostic: one of MUST_CONTAIN, "
        "MUST_NOT_CONTAIN, or EMPTY_STDERR must be supplied.")
endif()
if(_mode_count GREATER 1)
    message(FATAL_ERROR
        "check_reversible_diagnostic: MUST_CONTAIN / MUST_NOT_CONTAIN "
        "/ EMPTY_STDERR are mutually exclusive — caller must pick "
        "exactly one.")
endif()

foreach(path "${TRANSPILE_BIN}" "${SOURCE}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "check_reversible_diagnostic: required file missing: "
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
#
# The trailing `--` tells LibTooling's `CommonOptionsParser` that there
# are no additional compile arguments to follow, suppressing the
# five-line "Could not auto-detect compilation database" preamble the
# tool otherwise emits on stderr. The preamble is tool-boilerplate —
# not a user-facing diagnostic — and keeping it out of the stderr
# stream lets the EMPTY_STDERR mode below assert a genuinely-empty
# diagnostic surface on positive fixtures.
execute_process(
    COMMAND "${TRANSPILE_BIN}" "${_src_name}" --output-dir "${OUTPUT_DIR}" --
    WORKING_DIRECTORY "${_src_dir}"
    RESULT_VARIABLE _transpile_rc
    OUTPUT_VARIABLE _transpile_out
    ERROR_VARIABLE  _transpile_err)

if(DEFINED EMPTY_STDERR)
    # Happy-path branch (Phase T T-4 sturm-xrob.5): assert stderr is
    # EMPTY and sturm-transpile exits ZERO. Used by the P-C happy-path
    # fixture (`reversible_oracle.cpp`) — with the P-C / Q-B
    # validators wired into `transpile_consumer.cpp`, a clean positive
    # body must trip no validator surface at all.
    if(NOT _transpile_rc EQUAL 0)
        message(FATAL_ERROR
            "check_reversible_diagnostic: sturm-transpile exited "
            "non-zero (rc=${_transpile_rc}) on the EMPTY_STDERR "
            "positive fixture. A clean positive body must pass "
            "validation without firing any diagnostic.\n"
            "stdout:\n${_transpile_out}\n"
            "stderr:\n${_transpile_err}")
    endif()
    if(NOT _transpile_err STREQUAL "")
        message(FATAL_ERROR
            "check_reversible_diagnostic: sturm-transpile emitted "
            "non-empty stderr on the EMPTY_STDERR positive fixture. "
            "A clean happy-path body must produce no validator "
            "diagnostics AND no warnings from sibling matchers. If "
            "an unrelated warning has become load-bearing, silence "
            "it in the fixture's stub (the fixture is not a byte-"
            "compare snapshot so stub hygiene is under its own "
            "control).\n"
            "stdout:\n${_transpile_out}\n"
            "stderr:\n${_transpile_err}")
    endif()

    message(STATUS
        "check_reversible_diagnostic: OK — stderr was empty and "
        "exit code was zero.")
elseif(DEFINED MUST_CONTAIN)
    # Positive branch: expect non-zero exit (Error severity) AND
    # substring present.

    # Contract #1 (positive): non-zero exit. The validator emits
    # `DiagnosticsEngine::Error` per P9d — a non-invertible body or a
    # signature Q-B rejects is a hard compile error. A zero exit
    # would indicate the severity was incorrectly demoted to Warning.
    if(_transpile_rc EQUAL 0)
        message(FATAL_ERROR
            "check_reversible_diagnostic: sturm-transpile exited zero "
            "on the positive fixture. The reversible-diagnostic matcher "
            "must emit DiagnosticsEngine::Error, NOT Warning — a zero "
            "exit indicates the severity was incorrectly demoted.\n"
            "stdout:\n${_transpile_out}\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Contract #2 (positive): negative `<memory-buffer>` check. The
    # matcher must funnel its loc through
    # `SourceManager::getFileLoc(...)` so diagnostics cite the user's
    # source file. If `<memory-buffer>` surfaces, that plumbing is
    # broken.
    string(FIND "${_transpile_err}" "<memory-buffer>" _memory_buf_pos)
    if(NOT _memory_buf_pos EQUAL -1)
        message(FATAL_ERROR
            "check_reversible_diagnostic: stderr references "
            "`<memory-buffer>` — the matcher lost the user's filename "
            "when converting the matched loc. Inspect the validator's "
            "detection site: it must hand a "
            "`SourceManager::getFileLoc(...)` result to the matching "
            "`DiagContext::report_reversible_*` method so the engine's "
            "Report path cites the user's file, not the plugin's "
            "nested MemoryBuffer.\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Contract #3 (positive): substring MUST_CONTAIN present.
    string(FIND "${_transpile_err}" "${MUST_CONTAIN}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR
            "check_reversible_diagnostic: stderr did NOT contain the "
            "required substring `${MUST_CONTAIN}`. The validator "
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
                "check_reversible_diagnostic: stderr does not cite "
                "the expected user-source location.\n"
                "  required substring: ${_required_loc}\n"
                "  source file:        ${SOURCE}\n"
                "  expected line:      ${EXPECTED_LINE}\n"
                "stderr:\n${_transpile_err}")
        endif()
    endif()

    message(STATUS
        "check_reversible_diagnostic: OK — stderr contained "
        "`${MUST_CONTAIN}`, exit code was non-zero, and "
        "`<memory-buffer>` did not surface.")
else()
    # Negative branch: expect zero exit AND substring absent.

    # Contract #1 (negative): zero exit. Either the fixture is a
    # clean positive that passes validation, or the validator that
    # would fire on this shape has not yet been wired into
    # `transpile_consumer.cpp`. A non-zero exit indicates the matcher
    # fired on a shape it should have either (a) cleanly passed or
    # (b) stayed silent on in pass-through mode.
    if(NOT _transpile_rc EQUAL 0)
        message(FATAL_ERROR
            "check_reversible_diagnostic: sturm-transpile exited "
            "non-zero (rc=${_transpile_rc}) on the negative fixture. "
            "Either the positive-pass silent path broke, or a "
            "validator that should have stayed in pass-through mode "
            "has been prematurely wired into the TU-level driver.\n"
            "stdout:\n${_transpile_out}\n"
            "stderr:\n${_transpile_err}")
    endif()

    # Contract #2 (negative): forbidden substring absent.
    string(FIND "${_transpile_err}" "${MUST_NOT_CONTAIN}" _pos)
    if(NOT _pos EQUAL -1)
        message(FATAL_ERROR
            "check_reversible_diagnostic: stderr contained the "
            "forbidden substring `${MUST_NOT_CONTAIN}` at offset "
            "${_pos}. Either the clean fixture regressed, or a "
            "validator whose wiring-in is still TODO was "
            "prematurely glued into the TU-level driver.\n"
            "stderr:\n${_transpile_err}")
    endif()

    message(STATUS
        "check_reversible_diagnostic: OK — stderr did NOT contain "
        "`${MUST_NOT_CONTAIN}` and exit code was zero.")
endif()
