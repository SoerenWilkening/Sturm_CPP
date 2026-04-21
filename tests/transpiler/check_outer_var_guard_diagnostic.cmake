# check_outer_var_guard_diagnostic.cmake — PM3-2 PH-3 guard diagnostic probe.
#
# Clones the PM2-8 `check_source_map_diagnostic.cmake` pattern: run a
# known-ill-behaved fixture through the transpiler and assert on the
# resulting stderr substring set. Unlike PM2-8 (which drives `clang++`
# with the in-process plugin and expects a parse-side type error), this
# harness drives the standalone `sturm-transpile` binary so the fixture
# can exercise the PH-3 outer-variable-mutation guard directly without
# having to plant a compile-side type error.
#
# Invoked by `tests/transpiler/CMakeLists.txt` with:
#   -DTRANSPILE_BIN=<abs path> — sturm-transpile binary
#                                (via $<TARGET_FILE:...>)
#   -DSOURCE=<abs path>        — tests/transpiler/fixtures/
#                                for_outer_xor_reject.cpp (reused from the
#                                PH-3 snapshot tests)
#   -DOUTPUT_DIR=<abs path>    — scratch directory for the transpile output
#   -DEXPECTED_LINE=<int>      — 1-based user-source line that the PH-3
#                                diagnostic MUST cite (the `a ^= b;`
#                                mutation line inside the fixture's
#                                for-body)
#
# Contract (PM3-2 acceptance)
# ---------------------------
# Running `sturm-transpile SOURCE --output-dir OUTPUT_DIR` MUST succeed
# (rc == 0 — the PH-3 diagnostic is DiagnosticsEngine::Warning severity,
# NOT Error; compilation continues so the user sees every flagged
# mutation in a single pass). The captured stderr MUST:
#
#   1. contain the exact substring
#      `<basename(SOURCE)>:<EXPECTED_LINE>:`
#      — proving that the `DiagnosticsEngine::Report` path (wired via
#      the PM3-0 `DiagContext` from the parent CompilerInstance, routed
#      through the PM3-1 `TextDiagnosticPrinter`) cites the user's
#      source file and the user's originating mutation line, NOT a
#      synthesized rewrite-buffer line and NOT Clang's anonymous
#      `<memory-buffer>` pseudo-path.
#
#   2. contain the `modified inside` substring — the locked-down
#      format string (registered in
#      `DiagContext::report_outer_var_mutation`) embeds this phrase in
#      the fragment `is modified inside a for/while/if/WHEN body`.
#      Asserting on the phrase guards against a drift in the format
#      string that would still land a diagnostic but silently change
#      its contents.
#
#   3. NOT contain the substring `<memory-buffer>` — Clang's fallback
#      filename when a `MemoryBuffer` is given no explicit path. The
#      PM3-0 `DiagContext` funnels the matched loc through
#      `SourceManager::getFileLoc(...)` before reporting so diagnostic
#      locations resolve to the user's filename rather than any
#      intermediate buffer. If stderr surfaces `<memory-buffer>`, that
#      plumbing is broken.
#
# Script exits 0 on success, FATAL_ERROR on any contract violation.

foreach(var TRANSPILE_BIN SOURCE OUTPUT_DIR EXPECTED_LINE)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR
            "check_outer_var_guard_diagnostic: ${var} not set")
    endif()
endforeach()

foreach(path "${TRANSPILE_BIN}" "${SOURCE}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "check_outer_var_guard_diagnostic: required file missing: ${path}")
    endif()
endforeach()

# Fresh scratch dir. A stale output from a previous run must not bias
# the present assertion.
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

# Contract #1: zero exit. The PM3-2 / PH-3 matcher emits a Warning,
# so the standalone driver MUST finish with rc == 0 — any non-zero
# would indicate the severity was incorrectly promoted to Error and
# the "compilation continues" acceptance criterion would be broken.
if(NOT _transpile_rc EQUAL 0)
    message(FATAL_ERROR
        "check_outer_var_guard_diagnostic: sturm-transpile exited "
        "non-zero (rc=${_transpile_rc}). The PM3-2 matcher must emit "
        "DiagnosticsEngine::Warning, NOT Error; a non-zero exit "
        "indicates the severity was incorrectly promoted.\n"
        "stdout:\n${_transpile_out}\n"
        "stderr:\n${_transpile_err}")
endif()

# Contract #2: negative `<memory-buffer>` check. If the diagnostic's
# loc was not funnelled through `SourceManager::getFileLoc(...)` the
# plugin path would surface Clang's anonymous MemoryBuffer pseudo-path
# instead of the user's filename. This assertion runs ahead of the
# positive filename check so a regression in the loc-resolution
# pipeline surfaces a precise error message rather than a generic
# "substring not found".
string(FIND "${_transpile_err}" "<memory-buffer>" _memory_buf_pos)
if(NOT _memory_buf_pos EQUAL -1)
    message(FATAL_ERROR
        "check_outer_var_guard_diagnostic: stderr references "
        "`<memory-buffer>` — the PM3-0 `DiagContext` lost the user's "
        "filename when converting the matched loc. Inspect "
        "`matcher_outer_var_guard.cpp`'s `emit_diagnostic` helper: "
        "it must hand a `SourceManager::getFileLoc(...)` result to "
        "`diag.report_outer_var_mutation(...)` so the engine's Report "
        "path cites the user's file, not the plugin's nested "
        "MemoryBuffer.\n"
        "stderr:\n${_transpile_err}")
endif()

# Contract #3: positive filename + line. The diagnostic MUST cite the
# user's source file at the expected line.
set(_required_loc "${_src_name}:${EXPECTED_LINE}:")
string(FIND "${_transpile_err}" "${_required_loc}" _found_loc)
if(_found_loc EQUAL -1)
    message(FATAL_ERROR
        "check_outer_var_guard_diagnostic: stderr does not cite the "
        "expected user-source location.\n"
        "  required substring: ${_required_loc}\n"
        "  source file:        ${SOURCE}\n"
        "  expected line:      ${EXPECTED_LINE}\n"
        "stderr:\n${_transpile_err}")
endif()

# Contract #4: positive format-string check. The locked-down format
# fragment `modified inside` pins the message contents so a silent
# drift in `DiagContext::report_outer_var_mutation`'s format string
# surfaces here rather than in downstream user code.
set(_required_msg "modified inside")
string(FIND "${_transpile_err}" "${_required_msg}" _found_msg)
if(_found_msg EQUAL -1)
    message(FATAL_ERROR
        "check_outer_var_guard_diagnostic: stderr does not contain "
        "the expected format fragment `${_required_msg}`. The PM3-2 "
        "matcher either did not fire or the format string was "
        "changed without updating this test.\n"
        "stderr:\n${_transpile_err}")
endif()

message(STATUS
    "check_outer_var_guard_diagnostic: OK — stderr cited "
    "`${_required_loc}`, contained `${_required_msg}`, and did not "
    "surface `<memory-buffer>`.")
