# check_idempotent.cmake — LP6 idempotency ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DTRANSPILE_BIN=<abs path>  — sturm-transpile binary (via $<TARGET_FILE:...>)
#   -DINPUT=<abs path>          — the already-transpiled file whose idempotency
#                                 we want to assert
#   -DOUTPUT_DIR=<abs path>     — scratch dir for the re-transpiled output
#
# Contract (PRD acceptance #4):
#   Running sturm-transpile on a previously-emitted output must produce a
#   byte-identical result. The skip-detection path in main.cpp is what
#   makes this true at runtime (the AUTO-GENERATED header is recognized
#   as a sentinel and the file is copied verbatim), but the invariant is
#   contract surface, not an implementation detail — this harness
#   regresses it.
#
#   1. Feeds INPUT back through sturm-transpile writing to OUTPUT_DIR.
#   2. Resolves the regenerated path the tool produces. The driver's
#      resolve_output_path() collapses an ABSOLUTE input to basename and
#      drops it under OUTPUT_DIR (see transpiler/src/io.cpp), so we
#      compute `OUTPUT_DIR/basename(INPUT)` here and use it as the
#      authoritative comparison target.
#   3. Byte-compares the regenerated file against INPUT with
#      `cmake -E compare_files`.
#   4. On mismatch, shells out to `diff -u` best-effort so the ctest log
#      surfaces the exact delta. The compare_files result remains
#      authoritative — a missing `diff` must not mask a real failure.
#
# The script exits 0 on success, 1 on any failure.

if(NOT DEFINED TRANSPILE_BIN)
    message(FATAL_ERROR "check_idempotent: TRANSPILE_BIN not set")
endif()
if(NOT DEFINED INPUT)
    message(FATAL_ERROR "check_idempotent: INPUT not set")
endif()
if(NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "check_idempotent: OUTPUT_DIR not set")
endif()

if(NOT EXISTS "${TRANSPILE_BIN}")
    message(FATAL_ERROR
        "check_idempotent: sturm-transpile binary not found at ${TRANSPILE_BIN}")
endif()
if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR
        "check_idempotent: INPUT not found: ${INPUT}\n"
        "This file is produced by a prior build step; ensure the\n"
        "corresponding target was built (e.g. `cmake --build build\n"
        "--target example_or_circuit` for the example test, or that the\n"
        "matching snapshot_* test has run for the fixture test).")
endif()

# Fresh scratch directory for each run — a stale byte-equal regenerated
# file from a previous run could otherwise mask a real regression.
file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

# Invoke sturm-transpile on the already-generated file. We pass the
# absolute path so the tool's resolve_output_path() collapses to
# <OUTPUT_DIR>/<basename(INPUT)> — see transpiler/src/io.cpp. This keeps
# the regenerated file path deterministic regardless of where INPUT
# lives on disk.
execute_process(
    COMMAND "${TRANSPILE_BIN}" "${INPUT}" --output-dir "${OUTPUT_DIR}"
    RESULT_VARIABLE transpile_rc
    OUTPUT_VARIABLE transpile_out
    ERROR_VARIABLE  transpile_err)

if(NOT transpile_rc EQUAL 0)
    message(FATAL_ERROR
        "check_idempotent: sturm-transpile exited non-zero (rc=${transpile_rc}).\n"
        "stdout:\n${transpile_out}\n"
        "stderr:\n${transpile_err}")
endif()

get_filename_component(_input_name "${INPUT}" NAME)
set(_regen "${OUTPUT_DIR}/${_input_name}")

if(NOT EXISTS "${_regen}")
    message(FATAL_ERROR
        "check_idempotent: sturm-transpile exited 0 but did not produce "
        "${_regen}.\nstdout:\n${transpile_out}\nstderr:\n${transpile_err}")
endif()

# Authoritative byte-level comparison. compare_files exits non-zero on
# any delta, including EOL differences — which is exactly the signal the
# idempotency contract demands.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${INPUT}" "${_regen}"
    RESULT_VARIABLE compare_rc)

if(compare_rc EQUAL 0)
    message(STATUS "check_idempotent: OK — ${_regen} matches ${INPUT}")
    return()
endif()

# Mismatch. Emit a unified diff for the human reading the ctest log.
find_program(DIFF_BIN diff)
if(DIFF_BIN)
    execute_process(
        COMMAND "${DIFF_BIN}" -u "${INPUT}" "${_regen}"
        OUTPUT_VARIABLE diff_out
        ERROR_VARIABLE  diff_err)
    if(diff_out)
        message(STATUS "check_idempotent: unified diff (input vs regenerated):")
        message(STATUS "${diff_out}")
    endif()
    if(diff_err)
        message(STATUS "${diff_err}")
    endif()
else()
    message(STATUS
        "check_idempotent: `diff` not found on PATH; skipping unified diff "
        "(compare_files still reports the mismatch).")
endif()

message(FATAL_ERROR
    "check_idempotent: re-transpile is NOT byte-identical.\n"
    "  input:       ${INPUT}\n"
    "  regenerated: ${_regen}\n"
    "See the unified diff above (if available) for the exact delta.")
