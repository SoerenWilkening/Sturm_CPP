# run_snapshot_with_diagnostic.cmake — PH-3 snapshot-with-stderr driver.
#
# Companion to run_snapshot.cmake: same byte-level output comparison, but
# also asserts that the transpiler's stderr contains a required substring
# (typically the PH-3 "STURM: qbool/qint '<name>'" diagnostic). Used for
# fixtures whose "expected" file is byte-identical to the input (i.e. the
# transpiler correctly refused to inject an automatic uncompute and
# instead flagged the mutation).
#
# Parameters:
#   -DTRANSPILE_BIN=<abs path>        — sturm-transpile binary
#   -DINPUT=<abs path>                — input fixture
#   -DEXPECTED=<abs path>             — golden file (must equal INPUT contents)
#   -DOUTPUT_DIR=<abs path>           — scratch output dir
#   -DREQUIRED_STDERR=<substring>     — substring that must appear in stderr

if(NOT DEFINED TRANSPILE_BIN)
    message(FATAL_ERROR "run_snapshot_with_diagnostic: TRANSPILE_BIN not set")
endif()
if(NOT DEFINED INPUT)
    message(FATAL_ERROR "run_snapshot_with_diagnostic: INPUT not set")
endif()
if(NOT DEFINED EXPECTED)
    message(FATAL_ERROR "run_snapshot_with_diagnostic: EXPECTED not set")
endif()
if(NOT DEFINED OUTPUT_DIR)
    message(FATAL_ERROR "run_snapshot_with_diagnostic: OUTPUT_DIR not set")
endif()
if(NOT DEFINED REQUIRED_STDERR)
    message(FATAL_ERROR "run_snapshot_with_diagnostic: REQUIRED_STDERR not set")
endif()

if(NOT EXISTS "${TRANSPILE_BIN}")
    message(FATAL_ERROR "run_snapshot_with_diagnostic: sturm-transpile binary "
                        "not found at ${TRANSPILE_BIN}")
endif()
if(NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "run_snapshot_with_diagnostic: INPUT fixture not "
                        "found: ${INPUT}")
endif()
if(NOT EXISTS "${EXPECTED}")
    message(FATAL_ERROR "run_snapshot_with_diagnostic: EXPECTED golden not "
                        "found: ${EXPECTED}")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

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
        "run_snapshot_with_diagnostic: sturm-transpile exited non-zero "
        "(rc=${transpile_rc}).\n"
        "stdout:\n${transpile_out}\n"
        "stderr:\n${transpile_err}")
endif()

set(GENERATED "${OUTPUT_DIR}/${INPUT_NAME}")
if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "run_snapshot_with_diagnostic: sturm-transpile exited 0 but did not "
        "produce ${GENERATED}.\nstdout:\n${transpile_out}\n"
        "stderr:\n${transpile_err}")
endif()

# Byte-level comparison. Same signal as run_snapshot.cmake.
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E compare_files "${GENERATED}" "${EXPECTED}"
    RESULT_VARIABLE compare_rc)

if(NOT compare_rc EQUAL 0)
    find_program(DIFF_BIN diff)
    if(DIFF_BIN)
        execute_process(
            COMMAND "${DIFF_BIN}" -u "${EXPECTED}" "${GENERATED}"
            OUTPUT_VARIABLE diff_out
            ERROR_VARIABLE  diff_err)
        if(diff_out)
            message(STATUS "run_snapshot_with_diagnostic: unified diff "
                           "(expected vs generated):")
            message(STATUS "${diff_out}")
        endif()
    endif()
    message(FATAL_ERROR
        "run_snapshot_with_diagnostic: byte-level mismatch.\n"
        "  expected:  ${EXPECTED}\n"
        "  generated: ${GENERATED}\n"
        "stderr:\n${transpile_err}")
endif()

# Stderr substring check. The PH-3 matcher prints a multi-line diagnostic
# that includes the qualifier "STURM: qbool/qint" — REQUIRED_STDERR should
# be substring-contained anywhere in the tool's stderr.
string(FIND "${transpile_err}" "${REQUIRED_STDERR}" _found)
if(_found EQUAL -1)
    message(FATAL_ERROR
        "run_snapshot_with_diagnostic: stderr did not contain the required "
        "substring.\n  required: ${REQUIRED_STDERR}\n"
        "stderr:\n${transpile_err}")
endif()

message(STATUS "run_snapshot_with_diagnostic: OK — ${GENERATED} matches "
               "${EXPECTED} and stderr contains ${REQUIRED_STDERR}")
