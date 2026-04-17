# verify_flags.cmake — validates the M1 CMake flag contract.
#
# Invoked via ctest (see tests/cmake_flags/CMakeLists.txt). Runs a fresh
# out-of-source `cmake` configure and checks:
#   1. -DSTURM_TRANSPILE=ON: the transpiler subdirectory is entered (the
#      configure message emitted by transpiler/CMakeLists.txt appears in
#      stdout). This check is skipped gracefully if LLVM/Clang are not
#      installed on the host.
#
# Required driver variables (passed via `cmake -D` by the ctest add_test call):
#   SOURCE_DIR  — path to the STURM source tree (the dir containing the
#                 top-level CMakeLists.txt).
#   BUILD_ROOT  — a scratch directory under CMAKE_CURRENT_BINARY_DIR where
#                 per-check subbuilds are created and torn down.
#   CMAKE_BIN   — absolute path to the `cmake` executable (usually
#                 ${CMAKE_COMMAND} at invocation time).

if(NOT SOURCE_DIR)
    message(FATAL_ERROR "verify_flags.cmake: SOURCE_DIR is not set")
endif()
if(NOT BUILD_ROOT)
    message(FATAL_ERROR "verify_flags.cmake: BUILD_ROOT is not set")
endif()
if(NOT CMAKE_BIN)
    set(CMAKE_BIN "${CMAKE_COMMAND}")
endif()

# ──────────────────────────────────────────────────────────────────────────────
# Helper: fresh out-of-source configure. Returns the cmake stdout in <result>.
# ──────────────────────────────────────────────────────────────────────────────
function(_sturm_fresh_configure tag extra_args output_var success_var)
    set(_b "${BUILD_ROOT}/${tag}")
    file(REMOVE_RECURSE "${_b}")
    file(MAKE_DIRECTORY "${_b}")
    execute_process(
        COMMAND "${CMAKE_BIN}"
                -S "${SOURCE_DIR}"
                -B "${_b}"
                -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
                ${extra_args}
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE  _err
        RESULT_VARIABLE _rc)
    set(${output_var} "${_out}\n${_err}" PARENT_SCOPE)
    if(_rc EQUAL 0)
        set(${success_var} TRUE PARENT_SCOPE)
    else()
        set(${success_var} FALSE PARENT_SCOPE)
    endif()
endfunction()

# ──────────────────────────────────────────────────────────────────────────────
# Check: -DSTURM_TRANSPILE=ON → transpiler/CMakeLists.txt is entered.
# This check is best-effort: if LLVM/Clang development packages are not
# installed on the host, the configure will fail inside transpiler/, which
# is the documented expected behavior ("expected configure error until M4"
# per the issue AC). For M1 purposes we only need evidence that the guard
# triggers add_subdirectory(transpiler) — which is visible in stdout as the
# find_package(LLVM) diagnostic message (either success or the enriched
# FATAL_ERROR text). Either outcome confirms the guard fired.
# ──────────────────────────────────────────────────────────────────────────────
_sturm_fresh_configure("transpile_on" "-DSTURM_TRANSPILE=ON"
                       _out_tr _ok_tr)
# We do NOT require _ok_tr — a failure inside transpiler/ (e.g. missing LLVM)
# is expected on hosts without LLVM. What we require is that the configure
# reached transpiler/CMakeLists.txt, which emits one of the sentinel lines
# below regardless of whether find_package succeeded.
set(_tr_sentinels
    "sturm-transpile: using LLVM"           # success path from transpiler/
    "sturm-transpile requires LLVM"         # failure path from transpiler/
    "requires LLVM >= 17"                   # alt failure text
    "find_package\\(LLVM"                   # raw find_package failure
)
set(_tr_hit FALSE)
foreach(s ${_tr_sentinels})
    if(_out_tr MATCHES "${s}")
        set(_tr_hit TRUE)
        break()
    endif()
endforeach()
if(NOT _tr_hit)
    message(FATAL_ERROR
        "verify_flags: -DSTURM_TRANSPILE=ON did not appear to enter "
        "add_subdirectory(transpiler) — none of the expected sentinel "
        "messages from transpiler/CMakeLists.txt were seen.\n"
        "Configure output:\n${_out_tr}")
endif()
message(STATUS "verify_flags: [PASS] TRANSPILE=ON → add_subdirectory(transpiler) fired")

message(STATUS "verify_flags: ALL CHECKS PASSED")
