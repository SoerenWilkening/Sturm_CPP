# check_dropped_quantum_return_diagnostic.cmake — PM3-6 diagnostic probe.
#
# Drives `bin/sturm-transpile` against one of the PM3-6 fixtures
# (dropped_quantum_return_input / _bound / _void_cast) and asserts the
# stderr substring behaviour AND the zero-exit contract:
#
#   - Positive fixture (MUST_CONTAIN set): the stderr MUST contain the
#     required substring AND the process MUST exit zero (Warning, not
#     Error — compilation continues so the user sees every leak in one
#     pass, not just the first-to-fire).
#   - Negative fixture (MUST_NOT_CONTAIN set): the stderr MUST NOT
#     contain the forbidden substring AND the process MUST exit zero.
#
# Parameters
# ----------
#   -DTRANSPILE_BIN=<abs path>        — sturm-transpile binary
#                                       (via $<TARGET_FILE:...>)
#   -DSOURCE=<abs path>               — tests/transpiler/fixtures/
#                                       dropped_quantum_return_*.cpp
#   -DOUTPUT_DIR=<abs path>           — scratch directory for the
#                                       transpile output
#   -DMUST_CONTAIN=<substring>        — (optional) substring that must
#                                       appear in stderr. Mutually
#                                       exclusive with MUST_NOT_CONTAIN.
#   -DMUST_NOT_CONTAIN=<substring>    — (optional) substring that must
#                                       NOT appear in stderr.
#
# Contract
# --------
# The PM3-6 matcher emits `DiagnosticsEngine::Warning` — NOT `Error` —
# so clang (i.e. the sturm-transpile tool under libTooling) exits zero.
# The `transpile_diagnostic_surfaces` PM3-1 CTest already covers "the
# TextDiagnosticPrinter is wired up and stderr is non-empty on a hard
# error"; this CTest family pins the Warning severity class.
#
# Script exits 0 on success, FATAL_ERROR on any contract violation.

foreach(var TRANSPILE_BIN SOURCE OUTPUT_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR
            "check_dropped_quantum_return_diagnostic: ${var} not set")
    endif()
endforeach()

if(NOT DEFINED MUST_CONTAIN AND NOT DEFINED MUST_NOT_CONTAIN)
    message(FATAL_ERROR
        "check_dropped_quantum_return_diagnostic: neither MUST_CONTAIN "
        "nor MUST_NOT_CONTAIN was supplied — caller must pick exactly "
        "one.")
endif()
if(DEFINED MUST_CONTAIN AND DEFINED MUST_NOT_CONTAIN)
    message(FATAL_ERROR
        "check_dropped_quantum_return_diagnostic: both MUST_CONTAIN and "
        "MUST_NOT_CONTAIN were supplied — caller must pick exactly one.")
endif()

foreach(path "${TRANSPILE_BIN}" "${SOURCE}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "check_dropped_quantum_return_diagnostic: required file "
            "missing: ${path}")
    endif()
endforeach()

# Fresh scratch dir so a stale output from a previous run does not
# race against the present assertion.
file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

get_filename_component(_src_dir  "${SOURCE}" DIRECTORY)
get_filename_component(_src_name "${SOURCE}" NAME)

execute_process(
    COMMAND "${TRANSPILE_BIN}" "${_src_name}" --output-dir "${OUTPUT_DIR}"
    WORKING_DIRECTORY "${_src_dir}"
    RESULT_VARIABLE _transpile_rc
    OUTPUT_VARIABLE _transpile_out
    ERROR_VARIABLE  _transpile_err)

# Contract #1: zero exit. The PM3-6 matcher emits a Warning, so the
# standalone driver MUST finish with rc == 0 — any non-zero would
# indicate the severity promoted to Error, breaking the "compilation
# continues" acceptance criterion.
if(NOT _transpile_rc EQUAL 0)
    message(FATAL_ERROR
        "check_dropped_quantum_return_diagnostic: sturm-transpile "
        "exited non-zero (rc=${_transpile_rc}). The PM3-6 matcher must "
        "emit DiagnosticsEngine::Warning, NOT Error; a non-zero exit "
        "indicates the severity was incorrectly promoted.\n"
        "stdout:\n${_transpile_out}\n"
        "stderr:\n${_transpile_err}")
endif()

# Contract #2: stderr substring.
if(DEFINED MUST_CONTAIN)
    string(FIND "${_transpile_err}" "${MUST_CONTAIN}" _pos)
    if(_pos EQUAL -1)
        message(FATAL_ERROR
            "check_dropped_quantum_return_diagnostic: stderr did NOT "
            "contain the required substring `${MUST_CONTAIN}`.\n"
            "stderr:\n${_transpile_err}")
    endif()
    message(STATUS
        "check_dropped_quantum_return_diagnostic: OK - stderr "
        "contained `${MUST_CONTAIN}`.")
else()
    string(FIND "${_transpile_err}" "${MUST_NOT_CONTAIN}" _pos)
    if(NOT _pos EQUAL -1)
        message(FATAL_ERROR
            "check_dropped_quantum_return_diagnostic: stderr contained "
            "the forbidden substring `${MUST_NOT_CONTAIN}` at offset "
            "${_pos}. The matcher fired on a shape it should have "
            "rejected.\n"
            "stderr:\n${_transpile_err}")
    endif()
    message(STATUS
        "check_dropped_quantum_return_diagnostic: OK - stderr did "
        "NOT contain `${MUST_NOT_CONTAIN}`.")
endif()
