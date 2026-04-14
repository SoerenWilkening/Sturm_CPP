# run_snapshot.cmake — M11 snapshot ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DTRANSPILE_BIN=<abs path>  — sturm-transpile binary (via $<TARGET_FILE:...>)
#   -DINPUT=<abs path>          — input fixture (e.g. .../fixtures/or_single.cpp)
#   -DEXPECTED=<abs path>       — golden file (.../fixtures/or_single.expected.cpp)
#   -DOUTPUT_DIR=<abs path>     — scratch dir for the transpiler's output
#
# Contract:
#   1. Runs sturm-transpile on INPUT, writing the rewritten file to
#      <OUTPUT_DIR>/<basename(INPUT)>. To keep the emitted `Source:` line
#      free of build-tree noise, we invoke the tool from the fixture's
#      parent directory with a relative input path — the emitter writes
#      the passed-in path verbatim into the header, so a relative input
#      produces a clean, reproducible Source line.
#   2. Compares the generated file against EXPECTED using
#      `cmake -E compare_files`. This is the actual pass/fail signal;
#      byte-level equality is the MVP acceptance criterion (PRD AC #2 and
#      the M11 exit gate).
#   3. On mismatch, also shells out to `diff -u` (when available) so the
#      ctest log shows a human-readable unified diff pinpointing the
#      exact byte delta. The compare_files signal remains authoritative —
#      a flaky/missing diff tool must not mask a real mismatch.
#
# The script exits 0 on success, 1 on any failure (missing inputs,
# transpiler non-zero exit, or a compare_files mismatch).

if(NOT DEFINED TRANSPILE_BIN)
    message(FATAL_ERROR "run_snapshot: TRANSPILE_BIN not set")
endif()
if(NOT DEFINED INPUT)
    message(FATAL_ERROR "run_snapshot: INPUT not set")
endif()
if(NOT DEFINED EXPECTED)
    message(FATAL_ERROR "run_snapshot: EXPECTED not set")
endif()
if(NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "run_snapshot: OUTPUT_DIR not set")
endif()

if(NOT EXISTS "${TRANSPILE_BIN}")
    message(FATAL_ERROR "run_snapshot: sturm-transpile binary not found at "
                        "${TRANSPILE_BIN}")
endif()
if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "run_snapshot: INPUT fixture not found: ${INPUT}")
endif()
if(NOT EXISTS "${EXPECTED}")
    message(FATAL_ERROR "run_snapshot: EXPECTED golden not found: ${EXPECTED}")
endif()

# Fresh scratch directory for each run, otherwise a stale file from a
# previous transpile could mask a regression.
file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

# Invoke the transpiler from the fixture's parent dir with a relative
# input path so the `Source:` header line is deterministic.
get_filename_component(INPUT_DIR  "${INPUT}" DIRECTORY)
get_filename_component(INPUT_NAME "${INPUT}" NAME)

execute_process(
    COMMAND "${TRANSPILE_BIN}" "${INPUT_NAME}" --output-dir "${OUTPUT_DIR}"
    WORKING_DIRECTORY "${INPUT_DIR}"
    RESULT_VARIABLE transpile_rc
    OUTPUT_VARIABLE transpile_out
    ERROR_VARIABLE  transpile_err)

if(NOT transpile_rc EQUAL 0)
    message(FATAL_ERROR
        "run_snapshot: sturm-transpile exited non-zero (rc=${transpile_rc}).\n"
        "stdout:\n${transpile_out}\n"
        "stderr:\n${transpile_err}")
endif()

set(GENERATED "${OUTPUT_DIR}/${INPUT_NAME}")
if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "run_snapshot: sturm-transpile exited 0 but did not produce "
        "${GENERATED}.\nstdout:\n${transpile_out}\nstderr:\n${transpile_err}")
endif()

# Authoritative byte-level comparison. compare_files exits non-zero on any
# byte delta, including EOL differences, which is exactly the signal we
# want for a locked snapshot.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${GENERATED}" "${EXPECTED}"
    RESULT_VARIABLE compare_rc)

if(compare_rc EQUAL 0)
    message(STATUS "run_snapshot: OK — ${GENERATED} matches ${EXPECTED}")
    return()
endif()

# Mismatch. Emit a unified diff for the human reading the ctest log. The
# `diff -u` shell-out is best-effort: missing `diff` should not convert
# a real mismatch into a pass, so we still surface a fatal error regardless.
find_program(DIFF_BIN diff)
if(DIFF_BIN)
    execute_process(
        COMMAND "${DIFF_BIN}" -u "${EXPECTED}" "${GENERATED}"
        OUTPUT_VARIABLE diff_out
        ERROR_VARIABLE  diff_err)
    if(diff_out)
        message(STATUS "run_snapshot: unified diff (expected vs generated):")
        message(STATUS "${diff_out}")
    endif()
    if(diff_err)
        message(STATUS "${diff_err}")
    endif()
else()
    message(STATUS
        "run_snapshot: `diff` not found on PATH; skipping unified diff "
        "(compare_files still reports the mismatch).")
endif()

message(FATAL_ERROR
    "run_snapshot: byte-level mismatch.\n"
    "  expected:  ${EXPECTED}\n"
    "  generated: ${GENERATED}\n"
    "See the unified diff above (if available) for the exact delta.")
