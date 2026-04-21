# check_standalone_diag_surfaces.cmake — PM3-1 standalone-driver stderr probe.
#
# Invoked by `tests/transpiler/CMakeLists.txt` with:
#   -DTRANSPILE_BIN=<abs path> — sturm-transpile binary (via $<TARGET_FILE:...>)
#   -DSOURCE=<abs path>        — tests/transpiler/fixtures/
#                                standalone_diag_surfaces_input.cpp
#   -DOUTPUT_DIR=<abs path>    — scratch directory for the transpile output
#
# Contract (PM3-1 acceptance)
# ---------------------------
# Running `sturm-transpile SOURCE --output-dir OUTPUT_DIR` MUST surface the
# fixture's `static_assert(false, "STURM_PM3_DIAG_SURFACES_OK")` diagnostic
# on stderr. Prior to PM3-1 the driver installed a
# `clang::IgnoringDiagConsumer` which swallowed every diagnostic; after
# PM3-1 the driver installs a `TextDiagnosticPrinter(llvm::errs(), ...)`
# mirroring the plugin path (`plugin.cpp:395-398`). The load-bearing
# assertion here is stderr non-empty AND containing our distinctive
# marker substring.
#
# We deliberately DO NOT assert on the return code — a fixture that plants
# a hard language-level error makes the tool's parse fail, so the driver
# will exit non-zero. The failure mode we are guarding against is the
# diagnostic being INVISIBLE, not the return code.
#
# Script exits 0 on success, FATAL_ERROR on any contract violation.

foreach(var TRANSPILE_BIN SOURCE OUTPUT_DIR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "check_standalone_diag_surfaces: ${var} not set")
    endif()
endforeach()

foreach(path "${TRANSPILE_BIN}" "${SOURCE}")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR
            "check_standalone_diag_surfaces: required file missing: ${path}")
    endif()
endforeach()

# Fresh scratch dir. A stale output from a previous run must not bias the
# present assertion (though we only inspect stderr, the transpiler would
# still race against itself on OUTPUT_DIR if we didn't isolate).
file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

get_filename_component(_src_dir  "${SOURCE}" DIRECTORY)
get_filename_component(_src_name "${SOURCE}" NAME)

# Run the standalone driver from the fixture's own directory with a
# relative input path — matches the `run_snapshot.cmake` convention so
# any stderr filename the emitter prints is a clean basename (not an
# absolute build-tree path).
execute_process(
    COMMAND "${TRANSPILE_BIN}" "${_src_name}" --output-dir "${OUTPUT_DIR}"
    WORKING_DIRECTORY "${_src_dir}"
    RESULT_VARIABLE _transpile_rc
    OUTPUT_VARIABLE _transpile_out
    ERROR_VARIABLE  _transpile_err)

# Primary assertion: stderr must contain our distinctive marker string.
# We use the literal `STURM_PM3_DIAG_SURFACES_OK` substring planted in
# the fixture's static_assert — no fancy regex needed, just CMake's
# string(FIND).
set(_marker "STURM_PM3_DIAG_SURFACES_OK")
string(FIND "${_transpile_err}" "${_marker}" _marker_pos)
if(_marker_pos EQUAL -1)
    message(FATAL_ERROR
        "check_standalone_diag_surfaces: stderr did not contain the "
        "expected diagnostic marker `${_marker}`. This indicates the "
        "standalone driver is still installing `IgnoringDiagConsumer` "
        "(see transpiler/src/main.cpp, the `tool.setDiagnosticConsumer` "
        "call). PM3-1 requires a `TextDiagnosticPrinter` so Clang's "
        "diagnostics reach the user on stderr.\n"
        "return code: ${_transpile_rc}\n"
        "stdout:\n${_transpile_out}\n"
        "stderr:\n${_transpile_err}")
endif()

# Secondary assertion: stderr must be non-empty. This is implied by the
# primary check (the marker is non-empty) but we keep it explicit so a
# future refactor that accidentally weakens the marker check still
# catches a silent-stderr regression.
string(LENGTH "${_transpile_err}" _err_len)
if(_err_len EQUAL 0)
    message(FATAL_ERROR
        "check_standalone_diag_surfaces: stderr was empty, but the "
        "fixture plants a static_assert(false, ...) that must produce "
        "diagnostic output. The standalone driver is suppressing "
        "diagnostics.")
endif()

message(STATUS
    "check_standalone_diag_surfaces: OK — stderr contained `${_marker}` "
    "(${_err_len} bytes total).")
