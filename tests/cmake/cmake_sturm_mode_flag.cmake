# cmake_sturm_mode_flag.cmake — sturm-mixe ctest driver (Plan §3 P1).
#
# Verifies the `STURM_MODE` cache variable contract introduced by
# `cmake/SturmFrontend.cmake`:
#
#   1. An invalid value (e.g. STURM_MODE=GARBAGE) produces a FATAL_ERROR
#      whose message names `STURM_MODE` and the offending token, so a
#      typo at the command line is caught immediately rather than
#      silently accepted.
#   2. A valid value (each of APPEND / COUNT / SIMULATE) propagates
#      `STURM_MODE_DEFAULT=STURM_MODE_<X>` as a compile definition on
#      the `sturm::frontend` INTERFACE library; a probe TU that
#      static_asserts on the value compiles cleanly.
#
# Method: the test writes a minimal scratch CMake project that
#   - includes the parent's `cmake/SturmFrontend.cmake` directly (so the
#     test exercises the real source file, not a copy);
#   - declares a probe executable linked to `sturm::frontend`;
#   - runs four out-of-tree configures (one per case + 1 valid build).
# Configure-only for the invalid case; configure+build for the valid
# case (the static_assert lives in the probe TU's source).
#
# Inputs (passed via `-D` from `tests/CMakeLists.txt`):
#   STURM_SOURCE_DIR — absolute path to the STURM source tree.
#   SCRATCH_ROOT     — absolute path to a writable scratch root.

cmake_minimum_required(VERSION 3.16)

foreach(_required_arg STURM_SOURCE_DIR SCRATCH_ROOT)
    if(NOT DEFINED ${_required_arg})
        message(FATAL_ERROR
            "cmake_sturm_mode_flag: ${_required_arg} not set.")
    endif()
endforeach()
if(NOT EXISTS "${STURM_SOURCE_DIR}/cmake/SturmFrontend.cmake")
    message(FATAL_ERROR
        "cmake_sturm_mode_flag: missing cmake/SturmFrontend.cmake under "
        "${STURM_SOURCE_DIR}; the contract under test does not exist.")
endif()

# ── Build a tiny scratch project that includes SturmFrontend.cmake ────────
# The probe project carries `static_assert(STURM_MODE_DEFAULT == K)` for
# the integer value K of the configured mode. The integer values come
# from `include/sturm/core/core.h`:
#   STURM_MODE_COUNT_ONLY = 0
#   STURM_MODE_APPEND     = 1
#   STURM_MODE_SIMULATE   = 2
# (Hardcoded here so the probe does not transitively pull the umbrella.)
set(_proj_dir "${SCRATCH_ROOT}/proj")
file(REMOVE_RECURSE "${_proj_dir}")
file(MAKE_DIRECTORY "${_proj_dir}")

file(WRITE "${_proj_dir}/CMakeLists.txt"
"cmake_minimum_required(VERSION 3.16)
project(sturm_mode_flag_probe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(\"${STURM_SOURCE_DIR}/cmake/SturmFrontend.cmake\")
add_executable(probe probe.cpp)
target_link_libraries(probe PRIVATE sturm::frontend)
")

file(WRITE "${_proj_dir}/probe.cpp"
"// Integer values mirror include/sturm/core/core.h:
//   STURM_MODE_COUNT_ONLY = 0, STURM_MODE_APPEND = 1, STURM_MODE_SIMULATE = 2.
enum sturm_mode_probe {
    STURM_MODE_COUNT_ONLY = 0,
    STURM_MODE_APPEND     = 1,
    STURM_MODE_SIMULATE   = 2
};
#ifndef STURM_MODE_DEFAULT
#  error \"STURM_MODE_DEFAULT not defined — sturm::frontend compile def missing\"
#endif
static_assert(STURM_MODE_DEFAULT == STURM_MODE_APPEND ||
              STURM_MODE_DEFAULT == STURM_MODE_COUNT_ONLY ||
              STURM_MODE_DEFAULT == STURM_MODE_SIMULATE,
              \"STURM_MODE_DEFAULT must equal one of the C ABI enum values\");
#ifdef STURM_MODE_PROBE_EXPECT_APPEND
static_assert(STURM_MODE_DEFAULT == STURM_MODE_APPEND, \"expected APPEND\");
#endif
#ifdef STURM_MODE_PROBE_EXPECT_COUNT
static_assert(STURM_MODE_DEFAULT == STURM_MODE_COUNT_ONLY, \"expected COUNT\");
#endif
#ifdef STURM_MODE_PROBE_EXPECT_SIMULATE
static_assert(STURM_MODE_DEFAULT == STURM_MODE_SIMULATE, \"expected SIMULATE\");
#endif
int main() { return 0; }
")

# ── Helper: run one configure (with optional build) and capture output ────
function(_csmf_configure_and_build build_dir do_build expect_rc result_var
                                   stdout_var stderr_var)
    file(REMOVE_RECURSE "${build_dir}")
    file(MAKE_DIRECTORY "${build_dir}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-S" "${_proj_dir}" "-B" "${build_dir}"
                ${ARGN}
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE  _stderr
        RESULT_VARIABLE _rc)
    file(WRITE "${build_dir}/configure.log"
        "STDOUT:\n${_stdout}\n\nSTDERR:\n${_stderr}\n")
    set(${result_var} "${_rc}"     PARENT_SCOPE)
    set(${stdout_var} "${_stdout}" PARENT_SCOPE)
    set(${stderr_var} "${_stderr}" PARENT_SCOPE)
    if(do_build AND _rc EQUAL 0)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" --build "${build_dir}" --parallel 2
            OUTPUT_VARIABLE _bout ERROR_VARIABLE _berr RESULT_VARIABLE _brc)
        file(APPEND "${build_dir}/configure.log"
            "BUILD STDOUT:\n${_bout}\n\nBUILD STDERR:\n${_berr}\n")
        if(NOT _brc EQUAL 0)
            message(FATAL_ERROR
                "cmake_sturm_mode_flag: build under ${build_dir} FAILED "
                "(exit ${_brc}). Configure was OK; the static_assert in "
                "probe.cpp must have tripped.\n"
                "Build stderr (first 4KB): ${_berr}")
        endif()
    endif()
endfunction()

# ── Case 1: invalid mode → FATAL_ERROR ────────────────────────────────────
_csmf_configure_and_build("${SCRATCH_ROOT}/invalid" FALSE 1
    _rc _out _err
    "-DSTURM_MODE=GARBAGE")
if(_rc EQUAL 0)
    message(FATAL_ERROR
        "cmake_sturm_mode_flag: invalid STURM_MODE=GARBAGE configure "
        "succeeded — the validation block in cmake/SturmFrontend.cmake "
        "must FATAL_ERROR on unknown values.")
endif()
set(_combined "${_out}\n${_err}")
foreach(_needle "STURM_MODE" "GARBAGE")
    string(FIND "${_combined}" "${_needle}" _hit)
    if(_hit EQUAL -1)
        message(FATAL_ERROR
            "cmake_sturm_mode_flag: invalid-mode FATAL_ERROR is missing "
            "needle `${_needle}`. The diagnostic must name the cache "
            "variable AND the offending token so a typo is actionable.\n"
            "Captured output:\n${_combined}")
    endif()
endforeach()
message(STATUS "cmake_sturm_mode_flag: invalid-mode case OK.")

# ── Case 2: valid modes → static_assert passes (configure + build) ────────
foreach(_mode_pair "APPEND;STURM_MODE_PROBE_EXPECT_APPEND"
                   "COUNT;STURM_MODE_PROBE_EXPECT_COUNT"
                   "SIMULATE;STURM_MODE_PROBE_EXPECT_SIMULATE")
    list(GET _mode_pair 0 _mode)
    list(GET _mode_pair 1 _expect_macro)
    _csmf_configure_and_build("${SCRATCH_ROOT}/${_mode}" TRUE 0
        _rc _out _err
        "-DSTURM_MODE=${_mode}"
        "-DCMAKE_CXX_FLAGS=-D${_expect_macro}")
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            "cmake_sturm_mode_flag: valid STURM_MODE=${_mode} configure "
            "FAILED (exit ${_rc}). See ${SCRATCH_ROOT}/${_mode}/configure.log\n"
            "Stderr: ${_err}")
    endif()
    message(STATUS "cmake_sturm_mode_flag: valid mode ${_mode} — "
                   "static_assert green.")
endforeach()

message(STATUS "cmake_sturm_mode_flag: PASS — invalid → FATAL_ERROR, "
               "valid → STURM_MODE_DEFAULT propagated and static_assert "
               "passes (sturm-mixe).")
