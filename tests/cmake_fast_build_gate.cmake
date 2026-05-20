# cmake_fast_build_gate.cmake — sturm-e9gj ctest driver.
#
# Verifies the configure-time fast-build gates added in the top-level
# `CMakeLists.txt` that refuse to proceed down any silent slow-build
# path. The gates exist because a Claude agent / CI / fresh dev would
# otherwise drift into ~10x slower cold builds without noticing
# (ccache missing, mold missing, Make generator, OOM-inducing -j).
#
# Contract pinned by this test (per sturm-e9gj acceptance criteria):
#
#   1. CCACHE_MISSING — configure with STURM_USE_CCACHE=ON (default) and
#      `CCACHE_PROGRAM=CCACHE_PROGRAM-NOTFOUND` pre-cached MUST
#      FATAL_ERROR with a message naming `sturm-e9gj`, `ccache`, and
#      `tools/setup_dev_env.sh` (the load-bearing fix hint).
#   2. MOLD_MISSING (Linux only) — analogous, pre-caching
#      `MOLD_PROGRAM=MOLD_PROGRAM-NOTFOUND`. SKIPped on macOS where
#      STURM_USE_MOLD is a no-op (ld64 is native).
#   3. NON_NINJA — configure with `-G "Unix Makefiles"` MUST FATAL_ERROR
#      with a message naming `sturm-e9gj`, `Ninja`, and
#      `STURM_ALLOW_NON_NINJA` (the documented escape hatch).
#   4. VALID — configure with `-G Ninja` and a working toolchain MUST
#      succeed AND the STATUS log MUST contain `JOB_POOLS clamp active`
#      so the parallelism cap is verifiably wired.
#
# Method
# ------
# Four out-of-tree configures of the STURM source tree under
# `${SCRATCH_ROOT}/{ccache_missing,mold_missing,non_ninja,valid}`.
# Configure-only — no compilation — so the test stays fast (~30 s
# total on the agent container, dominated by find_package(LLVM CONFIG)
# in the VALID case).
#
# Inputs (passed via `-D` from `tests/CMakeLists.txt`):
#   STURM_SOURCE_DIR     — absolute path to the STURM source tree.
#   SCRATCH_ROOT         — absolute path to a writable scratch root.
#   PARENT_LLVM_DIR      — value to pass as `-DLLVM_DIR=...` (required).
#   PARENT_CLANG_DIR     — value to pass as `-DClang_DIR=...` (required).
#   PARENT_CXX_COMPILER  — value to pass as `-DCMAKE_CXX_COMPILER=...`
#                          (required).
#   PARENT_C_COMPILER    — value to pass as `-DCMAKE_C_COMPILER=...`
#                          (required).
#
# Exit discipline
# ---------------
# message(FATAL_ERROR ...) on contract violation. message(STATUS ...)
# on SKIP. Each configure log lands in
# `<scratch>/<case>/configure.log` for post-mortem.

cmake_minimum_required(VERSION 3.16)

# ── Argument validation ────────────────────────────────────────────────────
foreach(_required_arg STURM_SOURCE_DIR SCRATCH_ROOT
                      PARENT_LLVM_DIR PARENT_CLANG_DIR
                      PARENT_CXX_COMPILER PARENT_C_COMPILER)
    if(NOT DEFINED ${_required_arg} OR "${${_required_arg}}" STREQUAL "")
        message(FATAL_ERROR
            "cmake_fast_build_gate: ${_required_arg} not set. The ctest "
            "wiring in tests/CMakeLists.txt must pass it as -D...=...")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${STURM_SOURCE_DIR}")
    message(FATAL_ERROR
        "cmake_fast_build_gate: STURM_SOURCE_DIR does not exist or is "
        "not a directory: ${STURM_SOURCE_DIR}")
endif()

if(NOT EXISTS "${STURM_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "cmake_fast_build_gate: STURM_SOURCE_DIR is missing the "
        "top-level CMakeLists.txt: ${STURM_SOURCE_DIR}")
endif()

# ── Helper: run one configure and capture rc / stdout / stderr ────────────
function(_cfbg_configure build_dir result_var stdout_var stderr_var)
    set(_args ${ARGN})
    file(REMOVE_RECURSE "${build_dir}")
    file(MAKE_DIRECTORY "${build_dir}")

    execute_process(
        COMMAND "${CMAKE_COMMAND}" ${_args}
        WORKING_DIRECTORY "${build_dir}"
        OUTPUT_VARIABLE _stdout
        ERROR_VARIABLE  _stderr
        RESULT_VARIABLE _rc)

    file(WRITE "${build_dir}/configure.log"
        "ARGS:\n${_args}\n\nSTDOUT:\n${_stdout}\n\nSTDERR:\n${_stderr}\n")

    set(${result_var} "${_rc}"     PARENT_SCOPE)
    set(${stdout_var} "${_stdout}" PARENT_SCOPE)
    set(${stderr_var} "${_stderr}" PARENT_SCOPE)
endfunction()

# ── Helper: assert a configure FATAL_ERROR contains all needles ───────────
function(_cfbg_assert_fatal case_label rc stdout stderr)
    set(_needles ${ARGN})
    if(rc EQUAL 0)
        message(FATAL_ERROR
            "cmake_fast_build_gate: ${case_label} configure succeeded but "
            "the fast-build gate must FATAL_ERROR. See sturm-e9gj.\n"
            "Configure stdout (first 2KB):\n${stdout}")
    endif()
    set(_combined "${stdout}\n${stderr}")
    foreach(_needle IN LISTS _needles)
        string(FIND "${_combined}" "${_needle}" _hit)
        if(_hit EQUAL -1)
            string(SUBSTRING "${_combined}" 0 4096 _head)
            message(FATAL_ERROR
                "cmake_fast_build_gate: ${case_label} FATAL_ERROR fired "
                "but its message does not mention `${_needle}`. The "
                "diagnostic MUST keep this load-bearing token so future "
                "agents reading the failure get the fix hint.\n"
                "Captured output (first 4KB):\n${_head}")
        endif()
    endforeach()
endfunction()

# Common toolchain args shared by every case so find_package(LLVM CONFIG)
# resolves to the same LLVM the parent build uses.
set(_common_toolchain
    "-DCMAKE_CXX_COMPILER=${PARENT_CXX_COMPILER}"
    "-DCMAKE_C_COMPILER=${PARENT_C_COMPILER}"
    "-DLLVM_DIR=${PARENT_LLVM_DIR}"
    "-DClang_DIR=${PARENT_CLANG_DIR}")

# ── Case 1: CCACHE_MISSING — STURM_USE_CCACHE=ON, ccache not findable ─────
#
# Simulate "ccache not on PATH" by pre-caching CCACHE_PROGRAM with the
# NOTFOUND sentinel. find_program() is a no-op when its output cache var
# is already populated, so this convinces the configure that ccache is
# absent without actually mucking with the host's PATH.
set(_case1_dir "${SCRATCH_ROOT}/ccache_missing")
_cfbg_configure("${_case1_dir}" _rc _stdout _stderr
    "-S" "${STURM_SOURCE_DIR}"
    "-B" "${_case1_dir}"
    "-G" "Ninja"
    ${_common_toolchain}
    "-DCCACHE_PROGRAM=CCACHE_PROGRAM-NOTFOUND")

_cfbg_assert_fatal("CCACHE_MISSING" "${_rc}" "${_stdout}" "${_stderr}"
    "sturm-e9gj"            # issue ID — pin so reflows keep the breadcrumb
    "STURM_USE_CCACHE"      # the option that triggered the gate
    "ccache"                # the tool name (lowercase, as written)
    "tools/setup_dev_env.sh") # the canonical fix command

message(STATUS "cmake_fast_build_gate: CCACHE_MISSING case OK — gate "
               "fired with all required diagnostic needles.")

# ── Case 2: MOLD_MISSING — STURM_USE_MOLD=ON on Linux, mold not findable ──
#
# Linux-only. The mold gate only fires on Linux (macOS uses ld64 natively
# and STURM_USE_MOLD is a no-op there). SKIP on non-Linux so the test
# is portable.
if(NOT CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "cmake_fast_build_gate: MOLD_MISSING case SKIPPED — "
                   "host is ${CMAKE_HOST_SYSTEM_NAME}, not Linux. "
                   "STURM_USE_MOLD is a no-op on this platform.")
else()
    set(_case2_dir "${SCRATCH_ROOT}/mold_missing")
    _cfbg_configure("${_case2_dir}" _rc _stdout _stderr
        "-S" "${STURM_SOURCE_DIR}"
        "-B" "${_case2_dir}"
        "-G" "Ninja"
        ${_common_toolchain}
        "-DMOLD_PROGRAM=MOLD_PROGRAM-NOTFOUND")

    _cfbg_assert_fatal("MOLD_MISSING" "${_rc}" "${_stdout}" "${_stderr}"
        "sturm-e9gj"
        "STURM_USE_MOLD"
        "mold"
        "tools/setup_dev_env.sh")

    message(STATUS "cmake_fast_build_gate: MOLD_MISSING case OK — gate "
                   "fired with all required diagnostic needles.")
endif()

# ── Case 3: NON_NINJA — generator is not Ninja, no opt-in flag ────────────
set(_case3_dir "${SCRATCH_ROOT}/non_ninja")
_cfbg_configure("${_case3_dir}" _rc _stdout _stderr
    "-S" "${STURM_SOURCE_DIR}"
    "-B" "${_case3_dir}"
    "-G" "Unix Makefiles"
    ${_common_toolchain})

_cfbg_assert_fatal("NON_NINJA" "${_rc}" "${_stdout}" "${_stderr}"
    "sturm-e9gj"
    "Ninja"
    "STURM_ALLOW_NON_NINJA"      # the documented escape hatch
    "tools/setup_dev_env.sh")    # the canonical fix command

message(STATUS "cmake_fast_build_gate: NON_NINJA case OK — gate fired "
               "with all required diagnostic needles.")

# ── Case 4: VALID — Ninja + good toolchain → succeed + JOB_POOLS active ───
#
# The happy path: this is the configuration the parent build uses, so a
# failure here means the gate is over-eager. Additionally we assert the
# STATUS log mentions the JOB_POOLS clamp so the parallelism cap is
# verifiably wired (not just declared in CMakeLists.txt).
set(_case4_dir "${SCRATCH_ROOT}/valid")
_cfbg_configure("${_case4_dir}" _rc _stdout _stderr
    "-S" "${STURM_SOURCE_DIR}"
    "-B" "${_case4_dir}"
    "-G" "Ninja"
    ${_common_toolchain})

if(NOT _rc EQUAL 0)
    string(SUBSTRING "${_stderr}" 0 4096 _head)
    message(FATAL_ERROR
        "cmake_fast_build_gate: VALID configure FAILED (exit ${_rc}). The "
        "fast-build gates must NOT fire on the canonical Ninja + ccache + "
        "mold + matched-toolchain configuration. If this fails the gates "
        "are over-eager.\n"
        "Configure log: ${_case4_dir}/configure.log\n"
        "Stderr (first 4KB):\n${_head}")
endif()

set(_combined "${_stdout}\n${_stderr}")
foreach(_needle
        "JOB_POOLS clamp active"   # the runtime confirmation that the cap is wired
        "sturm-e9gj")              # issue ID
    string(FIND "${_combined}" "${_needle}" _hit)
    if(_hit EQUAL -1)
        string(SUBSTRING "${_combined}" 0 4096 _head)
        message(FATAL_ERROR
            "cmake_fast_build_gate: VALID configure succeeded but its "
            "STATUS log does not mention `${_needle}`. The JOB_POOLS "
            "clamp must announce itself so an operator inspecting the "
            "configure output can confirm the parallelism cap is "
            "wired. See sturm-e9gj.\n"
            "Captured output (first 4KB):\n${_head}")
    endif()
endforeach()

message(STATUS "cmake_fast_build_gate: VALID case OK — configure "
               "succeeded and JOB_POOLS clamp is announced.")

message(STATUS "cmake_fast_build_gate: PASS — fast-build gates fire on "
               "missing ccache / missing mold / non-Ninja generator, "
               "stay silent on the canonical valid configuration "
               "(sturm-e9gj).")
