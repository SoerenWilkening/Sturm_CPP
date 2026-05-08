# run_a2_a3_fresh_build.cmake — sturm-f8ib / Frontend simpl. P9 /
# PRD A2+A3 fresh-build acceptance gates.
#
# A2 contract: a clean configure of the STURM source tree with
#   `-DSTURM_MODE=APPEND` (or no flag at all — APPEND is the default)
#   builds and `ctest --parallel 6` passes end-to-end.
# A3 contract: same, with `-DSTURM_MODE=SIMULATE`, restricted to the
#   SIMULATE-mode test subset (matched by `-R 'simulate'`).
#
# Both gates spawn a fresh out-of-tree build dir under SCRATCH_ROOT/_AX,
# so they exercise the full configure + build pipeline (catching any
# guard that depended on incremental-build state from the parent build
# dir). Cost: ~3-5 minutes wall time per gate; both are gated behind
# STURM_FULL_TEST_SUITE in the aggregator.
#
# Inputs (passed via -D from add_test):
#   STURM_SOURCE_DIR — absolute path to the STURM source tree.
#   SCRATCH_ROOT     — absolute path to a writable scratch root.
#   GATE_LABEL       — one of "A2" / "A3" — used to pick STURM_MODE
#                      and the optional -R test filter.
#   STURM_MODE_VALUE — "APPEND" (A2) or "SIMULATE" (A3).
#   PARENT_LLVM_DIR  — passed through so the child configure picks up
#                      the same toolchain (host-clang invariant).
#   PARENT_CLANG_DIR — same.
#   PARENT_CXX_COMPILER / PARENT_C_COMPILER — same.

cmake_minimum_required(VERSION 3.16)

foreach(_required STURM_SOURCE_DIR SCRATCH_ROOT GATE_LABEL STURM_MODE_VALUE
                  PARENT_CXX_COMPILER PARENT_C_COMPILER PARENT_LLVM_DIR
                  PARENT_CLANG_DIR)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "run_a2_a3_fresh_build: ${_required} not set.")
    endif()
endforeach()

set(_build_dir "${SCRATCH_ROOT}/${GATE_LABEL}_build")
file(REMOVE_RECURSE "${_build_dir}")
file(MAKE_DIRECTORY "${_build_dir}")

set(_configure_args
    "-S" "${STURM_SOURCE_DIR}" "-B" "${_build_dir}"
    "-DSTURM_MODE=${STURM_MODE_VALUE}"
    "-DCMAKE_CXX_COMPILER=${PARENT_CXX_COMPILER}"
    "-DCMAKE_C_COMPILER=${PARENT_C_COMPILER}"
    "-DLLVM_DIR=${PARENT_LLVM_DIR}"
    "-DClang_DIR=${PARENT_CLANG_DIR}")

message(STATUS "run_a2_a3_fresh_build (${GATE_LABEL}): configuring "
               "${_build_dir} with STURM_MODE=${STURM_MODE_VALUE}.")
execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_configure_args}
    OUTPUT_VARIABLE _cfg_out ERROR_VARIABLE _cfg_err
    RESULT_VARIABLE _cfg_rc)
if(NOT _cfg_rc EQUAL 0)
    file(WRITE "${_build_dir}/configure.log"
        "STDOUT:\n${_cfg_out}\n\nSTDERR:\n${_cfg_err}\n")
    message(FATAL_ERROR
        "run_a2_a3_fresh_build (${GATE_LABEL}): configure FAILED "
        "(exit ${_cfg_rc}). See ${_build_dir}/configure.log.\n"
        "Stderr (first 4KB): ${_cfg_err}")
endif()

message(STATUS "run_a2_a3_fresh_build (${GATE_LABEL}): building (--parallel 6).")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --build "${_build_dir}" --parallel 6
    OUTPUT_VARIABLE _bld_out ERROR_VARIABLE _bld_err
    RESULT_VARIABLE _bld_rc)
if(NOT _bld_rc EQUAL 0)
    file(WRITE "${_build_dir}/build.log"
        "STDOUT:\n${_bld_out}\n\nSTDERR:\n${_bld_err}\n")
    message(FATAL_ERROR
        "run_a2_a3_fresh_build (${GATE_LABEL}): build FAILED "
        "(exit ${_bld_rc}). See ${_build_dir}/build.log.\n"
        "Stderr (first 4KB): ${_bld_err}")
endif()

# A3 narrows the ctest run to the SIMULATE-mode subset (any test whose
# name contains "simulate"). A2 runs the full default subset.
set(_ctest_args "--test-dir" "${_build_dir}" "--parallel" "6"
                "--output-on-failure")
if(GATE_LABEL STREQUAL "A3")
    list(APPEND _ctest_args "-R" "simulate")
endif()

message(STATUS "run_a2_a3_fresh_build (${GATE_LABEL}): running ctest "
               "${_ctest_args}.")
execute_process(
    COMMAND ctest ${_ctest_args}
    WORKING_DIRECTORY "${_build_dir}"
    OUTPUT_VARIABLE _ct_out ERROR_VARIABLE _ct_err
    RESULT_VARIABLE _ct_rc)
file(WRITE "${_build_dir}/ctest.log"
    "STDOUT:\n${_ct_out}\n\nSTDERR:\n${_ct_err}\n")
if(NOT _ct_rc EQUAL 0)
    message(FATAL_ERROR
        "run_a2_a3_fresh_build (${GATE_LABEL}): ctest FAILED "
        "(exit ${_ct_rc}). See ${_build_dir}/ctest.log.\n"
        "Stdout tail (last 4KB): ${_ct_out}")
endif()

message(STATUS "run_a2_a3_fresh_build (${GATE_LABEL}): PASS — "
               "configure + build + ctest green for STURM_MODE=${STURM_MODE_VALUE}.")
