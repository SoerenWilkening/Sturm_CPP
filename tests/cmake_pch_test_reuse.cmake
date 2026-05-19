# cmake_pch_test_reuse.cmake — sturm-bs8s ctest driver.
#
# Verifies that the per-test PCH-reuse contract introduced by sturm-bs8s
# is in force across the transpiler test suite. The contract has three
# parts:
#
#   1. The helper `sturm_use_transpile_pch(<target>)` is defined and
#      available to `transpiler/tests/CMakeLists.txt`.
#
#   2. A representative heavy test target (`test_matcher_qint_alias_subst`
#      — the target the per-TU measurement experiment used) carries
#      `-fPIC` in its per-target CXX_FLAGS line. Without `-fPIC` the
#      consumer's PIE state mismatches the PCH producer's and clang
#      rejects the PCH with `is pie differs in PCH file vs. current
#      file` (sturm-2vbr).
#
#   3. The same target's build rules carry a `cmake_pch.hxx` reuse
#      reference pointing at `sturm-transpile.dir/cmake_pch.hxx` — i.e.
#      `target_precompile_headers(... REUSE_FROM sturm-transpile)`
#      was actually applied. The build-output channel is the
#      target-specific `flags.make` plus the `*_pch.hxx*` files in
#      the target's CMakeFiles directory: when REUSE_FROM is set,
#      CMake adds a `-Xclang -include-pch` argument pointing at the
#      producer's `.pch` artefact path.
#
# Method
# ------
# Re-uses the same out-of-tree configure pattern as
# `cmake_pch_pie_match.cmake` — forwards the parent's resolved
# LLVM_DIR / Clang_DIR / CMAKE_CXX_COMPILER hints, generates the
# transpiler test subdir, then reads the generated `flags.make` and
# `build.make` for the representative test target. Configure-only — no
# compilation — so the test stays fast (~30 s).
#
# Inputs (passed via `-D` from `tests/CMakeLists.txt`):
#   STURM_SOURCE_DIR     — absolute path to the STURM source tree.
#   SCRATCH_ROOT         — absolute path to a writable scratch root.
#   PARENT_LLVM_DIR      — value to pass through as `-DLLVM_DIR=...`.
#   PARENT_CLANG_DIR     — value to pass through as `-DClang_DIR=...`.
#   PARENT_CXX_COMPILER  — value to pass through as `-DCMAKE_CXX_COMPILER=...`.
#   PARENT_C_COMPILER    — value to pass through as `-DCMAKE_C_COMPILER=...`.
#
# Exit discipline
# ---------------
# message(FATAL_ERROR ...) on any contract violation. Configure logs
# land in `<scratch>/configure.log` for post-mortem.

cmake_minimum_required(VERSION 3.16)

# ── Argument validation ────────────────────────────────────────────────────
foreach(_required_arg STURM_SOURCE_DIR SCRATCH_ROOT)
    if(NOT DEFINED ${_required_arg})
        message(FATAL_ERROR
            "cmake_pch_test_reuse: ${_required_arg} not set. The ctest "
            "wiring in tests/CMakeLists.txt must pass "
            "-D${_required_arg}=<path>.")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${STURM_SOURCE_DIR}")
    message(FATAL_ERROR
        "cmake_pch_test_reuse: STURM_SOURCE_DIR does not exist or is "
        "not a directory: ${STURM_SOURCE_DIR}")
endif()

# ── Contract 1: helper file exists ────────────────────────────────────────
set(_helper_path "${STURM_SOURCE_DIR}/cmake/SturmTranspilePch.cmake")
if(NOT EXISTS "${_helper_path}")
    message(FATAL_ERROR
        "cmake_pch_test_reuse: helper file missing at ${_helper_path}.\n"
        "sturm-bs8s defines `sturm_use_transpile_pch(<target>)` in "
        "cmake/SturmTranspilePch.cmake. Restore the file.")
endif()

file(READ "${_helper_path}" _helper_content)
if(NOT _helper_content MATCHES "function *\\( *sturm_use_transpile_pch")
    message(FATAL_ERROR
        "cmake_pch_test_reuse: ${_helper_path} does not define a "
        "function named `sturm_use_transpile_pch`. The helper signature "
        "must remain `sturm_use_transpile_pch(<target>)`.")
endif()

# ── Run one configure forwarding the parent toolchain ─────────────────────
set(_build_dir "${SCRATCH_ROOT}/configure")
file(REMOVE_RECURSE "${_build_dir}")
file(MAKE_DIRECTORY "${_build_dir}")

set(_args
    "-S" "${STURM_SOURCE_DIR}"
    "-B" "${_build_dir}"
    "-G" "Unix Makefiles"
)
if(PARENT_LLVM_DIR)
    list(APPEND _args "-DLLVM_DIR=${PARENT_LLVM_DIR}")
endif()
if(PARENT_CLANG_DIR)
    list(APPEND _args "-DClang_DIR=${PARENT_CLANG_DIR}")
endif()
if(PARENT_CXX_COMPILER)
    list(APPEND _args "-DCMAKE_CXX_COMPILER=${PARENT_CXX_COMPILER}")
endif()
if(PARENT_C_COMPILER)
    list(APPEND _args "-DCMAKE_C_COMPILER=${PARENT_C_COMPILER}")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" ${_args}
    WORKING_DIRECTORY "${_build_dir}"
    OUTPUT_VARIABLE _stdout
    ERROR_VARIABLE  _stderr
    RESULT_VARIABLE _rc)

file(WRITE "${_build_dir}/configure.log"
    "ARGS:\n${_args}\n\nSTDOUT:\n${_stdout}\n\nSTDERR:\n${_stderr}\n")

if(NOT _rc EQUAL 0)
    string(SUBSTRING "${_stderr}" 0 4096 _head)
    message(FATAL_ERROR
        "cmake_pch_test_reuse: parent-mirror configure FAILED (exit ${_rc}). "
        "Configure log: ${_build_dir}/configure.log\n"
        "Stderr (first 4KB):\n${_head}")
endif()

# ── Locate the representative heavy test target ──────────────────────────
# `test_bucket_qint_alias` is the bucket target (sturm-r8xu) that carries
# the matcher_qint_alias_subst member test the issue's experiment measured
# (12.10s -> 4.44s per-TU pre-bucket). After bucketing the binary lives at:
#   <build>/transpiler/tests/CMakeFiles/test_bucket_qint_alias.dir
set(_target_dir
    "${_build_dir}/transpiler/tests/CMakeFiles/test_bucket_qint_alias.dir")
if(NOT IS_DIRECTORY "${_target_dir}")
    message(FATAL_ERROR
        "cmake_pch_test_reuse: representative target dir missing at "
        "${_target_dir}. Configure did not produce the expected makefile "
        "layout — has the test target been renamed?")
endif()

# ── Contract 2: target carries -fPIC ──────────────────────────────────────
function(_pchr_read_cxx_flags target_dir out_flags)
    set(_flags_make "${target_dir}/flags.make")
    if(NOT EXISTS "${_flags_make}")
        message(FATAL_ERROR
            "cmake_pch_test_reuse: expected flags.make at ${_flags_make}.")
    endif()
    file(READ "${_flags_make}" _content)
    if(NOT _content MATCHES "CXX_FLAGS *= *([^\n]*)")
        message(FATAL_ERROR
            "cmake_pch_test_reuse: ${_flags_make} has no CXX_FLAGS line.")
    endif()
    set(${out_flags} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

_pchr_read_cxx_flags("${_target_dir}" _target_flags)
message(STATUS "cmake_pch_test_reuse: target CXX_FLAGS = ${_target_flags}")

string(REGEX REPLACE "[ \t]+" ";" _target_tokens "${_target_flags}")
list(FIND _target_tokens "-fPIC" _target_pic_idx)
if(_target_pic_idx EQUAL -1)
    message(FATAL_ERROR
        "cmake_pch_test_reuse: target `test_bucket_qint_alias` does "
        "NOT carry -fPIC in its per-target CXX_FLAGS. The `sturm-transpile` "
        "PCH was built with -fPIC; without -fPIC on the consumer, clang "
        "rejects the PCH with `is pie differs in PCH file vs. current "
        "file` (sturm-2vbr). Apply `sturm_use_transpile_pch(<target>)` "
        "from `cmake/SturmTranspilePch.cmake` to fix this.\n"
        "  flags.make path = ${_target_dir}/flags.make")
endif()

# ── Contract 3: target REUSE_FROMs the sturm-transpile PCH ────────────────
# CMake's Unix Makefiles generator emits per-source-file PCH-include
# options as comments in the consumer's `flags.make`, immediately
# below the CXX_FLAGS / CXX_DEFINES lines. The shape, for a target
# that REUSE_FROMs `sturm-transpile`, is:
#
#     # PCH options: <target>.dir/<src>.cpp.o_OPTIONS = -Winvalid-pch;
#       -Xclang;-include-pch;-Xclang;
#       <build>/transpiler/CMakeFiles/sturm-transpile.dir/cmake_pch.hxx.pch;
#       -Xclang;-include;-Xclang;
#       <build>/transpiler/CMakeFiles/sturm-transpile.dir/cmake_pch.hxx
#
# The presence of the substring `sturm-transpile.dir/cmake_pch.hxx`
# in the consumer's `flags.make` is the build-system-observable
# signal that:
#   (a) the consumer has a PCH wired in at all (`target_precompile_
#       headers(... REUSE_FROM ...)` was called), AND
#   (b) it is REUSE_FROM'd from the `sturm-transpile` producer (not
#       from some other target).
#
# Cmake versions <= 3.27 also wrote a small `<target>.dir/cmake_pch.hxx`
# wrapper file on disk; CMake 4.x emits the include-pch options
# inline in `flags.make` instead and skips the wrapper. The
# flags.make check is therefore the portable assertion.
set(_target_flags_make "${_target_dir}/flags.make")
file(READ "${_target_flags_make}" _target_flags_make_content)
if(NOT _target_flags_make_content MATCHES "sturm-transpile\\.dir/cmake_pch\\.hxx")
    message(FATAL_ERROR
        "cmake_pch_test_reuse: target `test_matcher_qint_alias_subst` "
        "flags.make does NOT reference the `sturm-transpile.dir/"
        "cmake_pch.hxx` producer path. Without that reference the "
        "consumer is not REUSE_FROM'ing the sturm-transpile PCH.\n"
        "  flags.make path = ${_target_flags_make}\n"
        "Fix: call `sturm_use_transpile_pch(test_matcher_qint_alias_subst)` "
        "from `transpiler/tests/CMakeLists.txt`.")
endif()

message(STATUS "cmake_pch_test_reuse: PASS — "
               "test target carries -fPIC AND REUSE_FROMs the "
               "sturm-transpile PCH (sturm-bs8s).")
