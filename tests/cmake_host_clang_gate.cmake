# cmake_host_clang_gate.cmake — sturm-yial ctest driver.
#
# Verifies the configure-time gate added in the top-level
# `CMakeLists.txt` that detects host-compiler vs plugin-LLVM mismatch
# and emits an actionable FATAL_ERROR before find_package(LLVM CONFIG)
# silently produces a "plugin loads but ReplaceAction is invisible"
# build.
#
# Background: the sturm-transpile-plugin .dylib links Homebrew LLVM's
# libclang-cpp.dylib. When the host clang at `CMAKE_CXX_COMPILER` is a
# different Clang install (e.g. Apple Clang), the plugin's
# `FrontendPluginRegistry::Add<>` registers into the loaded library's
# static registry instead of the host's. The plugin loads cleanly, the
# `getActionType() == ReplaceAction` decision is invisible to the host,
# and the host runs the default `EmitObjAction` against the unrewritten
# source. Tests run, link, and execute — but emit zero gates because no
# qint-alias / qram-subscript rewrite ever ran. See sturm-yial for the
# full root-cause trace.
#
# Contract pinned by this test (per sturm-yial acceptance criteria):
#
#   1. Configure with `-DCMAKE_CXX_COMPILER=<host-clang>` AND
#      `-DLLVM_DIR=<plugin-llvm-cmakedir>` where the two installs
#      differ MUST FATAL_ERROR with a message that names BOTH the
#      compiler path AND the LLVM_DIR path AND the FrontendPluginRegistry
#      cause AND the recommended fix.
#   2. Configure with the SAME CMAKE_CXX_COMPILER and LLVM_DIR (i.e.
#      the host clang lives under the same prefix as LLVM_DIR's
#      `lib/cmake/llvm`) MUST succeed.
#   3. Configure with NEITHER `-DCMAKE_CXX_COMPILER` NOR `-DLLVM_DIR`
#      MUST NOT trigger the gate (the gate is on mismatch, not on
#      absence of LLVM_DIR).
#
# Method
# ------
# Three out-of-tree configures of the STURM source tree under
# `${SCRATCH_ROOT}/{mismatch,match,unset}` with different combinations
# of compiler / LLVM_DIR overrides. Configure-only — no compilation —
# so the test stays fast.
#
# Inputs (passed via `-D` from `tests/CMakeLists.txt`):
#   STURM_SOURCE_DIR     — absolute path to the STURM source tree.
#   SCRATCH_ROOT         — absolute path to a writable scratch root.
#   PARENT_LLVM_DIR      — value to pass as `-DLLVM_DIR=...` for the
#                          MATCH case (forwarded from the parent build's
#                          resolved LLVM CMake dir; required).
#   PARENT_CLANG_DIR     — value to pass as `-DClang_DIR=...` (mirror
#                          of PARENT_LLVM_DIR; required).
#   PARENT_CXX_COMPILER  — value to pass as `-DCMAKE_CXX_COMPILER=...`
#                          for the MATCH case (the parent build's host
#                          compiler, which by construction matches its
#                          LLVM_DIR; required).
#   PARENT_C_COMPILER    — value to pass as `-DCMAKE_C_COMPILER=...`
#                          (paired with PARENT_CXX_COMPILER; required).
#   MISMATCH_CXX         — absolute path to a clang++ that lives under
#                          a DIFFERENT install prefix than
#                          PARENT_LLVM_DIR (typically Apple Clang at
#                          `/usr/bin/c++`). The MISMATCH case fails
#                          early if this file does not exist on the
#                          test machine — the test SKIPs in that case
#                          rather than FAIL'ing, so non-Apple platforms
#                          (Linux, where there is only one clang) do
#                          not get a spurious red.
#
# Exit discipline
# ---------------
# message(FATAL_ERROR ...) on contract violation. message(STATUS ...)
# on SKIP. The configure logs land in
# `<scratch>/<case>/configure.log` for post-mortem.

cmake_minimum_required(VERSION 3.16)

# ── Argument validation ────────────────────────────────────────────────────
foreach(_required_arg STURM_SOURCE_DIR SCRATCH_ROOT
                      PARENT_LLVM_DIR PARENT_CLANG_DIR
                      PARENT_CXX_COMPILER PARENT_C_COMPILER)
    if(NOT DEFINED ${_required_arg} OR "${${_required_arg}}" STREQUAL "")
        message(FATAL_ERROR
            "cmake_host_clang_gate: ${_required_arg} not set. The ctest "
            "wiring in tests/CMakeLists.txt must pass it as -D...=...")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${STURM_SOURCE_DIR}")
    message(FATAL_ERROR
        "cmake_host_clang_gate: STURM_SOURCE_DIR does not exist or is "
        "not a directory: ${STURM_SOURCE_DIR}")
endif()

if(NOT EXISTS "${STURM_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "cmake_host_clang_gate: STURM_SOURCE_DIR is missing the "
        "top-level CMakeLists.txt: ${STURM_SOURCE_DIR}")
endif()

# ── Helper: run one configure and capture rc / stdout / stderr ────────────
function(_chcg_configure build_dir result_var stdout_var stderr_var)
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

# ── Helper: locate the parent build's STURM_GEN dir for cleanup ───────────
# (No special handling needed — each scratch build is self-contained.)

# ── Case 1: MISMATCH — Apple Clang host vs Homebrew LLVM_DIR ──────────────
#
# The reproduction from the bd-autopilot 2026-05-06 RED:
#   -DCMAKE_CXX_COMPILER=/usr/bin/c++  (Apple Clang)
#   -DLLVM_DIR=/usr/local/opt/llvm@17/lib/cmake/llvm
# must FATAL_ERROR with a message that names the mismatch and the fix.
#
# The test SKIPs (rather than FAILs) when the MISMATCH_CXX path does not
# exist on the host — non-Apple platforms (Linux, where only one clang
# is installed) cannot reproduce the trap and the gate is a no-op.

if(NOT MISMATCH_CXX OR "${MISMATCH_CXX}" STREQUAL "")
    message(STATUS "cmake_host_clang_gate: MISMATCH_CXX not set — skipping "
                   "MISMATCH case (no second clang to test against).")
elseif(NOT EXISTS "${MISMATCH_CXX}")
    message(STATUS "cmake_host_clang_gate: MISMATCH_CXX (${MISMATCH_CXX}) "
                   "does not exist on this host — skipping MISMATCH case.")
else()
    set(_mismatch_dir "${SCRATCH_ROOT}/mismatch")
    _chcg_configure("${_mismatch_dir}" _rc _stdout _stderr
        "-S" "${STURM_SOURCE_DIR}"
        "-B" "${_mismatch_dir}"
        "-DCMAKE_CXX_COMPILER=${MISMATCH_CXX}"
        "-DLLVM_DIR=${PARENT_LLVM_DIR}"
        "-DClang_DIR=${PARENT_CLANG_DIR}")

    if(_rc EQUAL 0)
        message(FATAL_ERROR
            "cmake_host_clang_gate: MISMATCH configure succeeded but the "
            "host-clang gate must FATAL_ERROR when CMAKE_CXX_COMPILER "
            "(${MISMATCH_CXX}) lives under a different install prefix than "
            "LLVM_DIR (${PARENT_LLVM_DIR}). Without the gate, the plugin's "
            "FrontendPluginRegistry::Add<> registers into the dlopen'd "
            "libclang-cpp.dylib's registry instead of the host's, "
            "ReplaceAction is invisible, and tests run silently with zero "
            "transpiler rewrites. See sturm-yial.\n"
            "Configure log: ${_mismatch_dir}/configure.log")
    endif()

    # The combined output may land on stdout or stderr depending on how
    # CMake routes message(FATAL_ERROR ...); concatenate both for the
    # diagnostic match.
    set(_combined "${_stdout}\n${_stderr}")

    # Required needles in the diagnostic. The plan §"actionable
    # diagnostic" requires the message to name (a) the host compiler,
    # (b) the plugin LLVM dir, (c) the cause (registry / plugin), and
    # (d) the recommended fix (set CMAKE_CXX_COMPILER to LLVM_DIR's
    # clang++). We accept any wording that mentions each token.
    #
    # Use `string(FIND ...)` for the path needles (literal substring
    # match) — `MATCHES` would attempt a regex compile and the path
    # `/usr/bin/c++` contains the regex special character `+`, which
    # blows up on macOS. The fixed-token needles
    # (FrontendPluginRegistry, CMAKE_CXX_COMPILER) are regex-safe.
    foreach(_needle
            "${MISMATCH_CXX}"          # the offending host compiler path
            "${PARENT_LLVM_DIR}"       # the offending LLVM_DIR path
            "FrontendPluginRegistry"   # the cause
            "CMAKE_CXX_COMPILER")      # the fix knob
        string(FIND "${_combined}" "${_needle}" _hit)
        if(_hit EQUAL -1)
            string(SUBSTRING "${_combined}" 0 4096 _head)
            message(FATAL_ERROR
                "cmake_host_clang_gate: MISMATCH FATAL_ERROR was emitted "
                "but its message does not mention `${_needle}`. The "
                "diagnostic must name the host compiler, the LLVM_DIR, "
                "the FrontendPluginRegistry cause, and the "
                "CMAKE_CXX_COMPILER fix knob (sturm-yial).\n"
                "Captured output (first 4KB):\n${_head}")
        endif()
    endforeach()

    message(STATUS "cmake_host_clang_gate: MISMATCH case OK — gate fired "
                   "with all required diagnostic needles.")
endif()

# ── Case 2: MATCH — host compiler under same prefix as LLVM_DIR ───────────
#
# When CMAKE_CXX_COMPILER lives under PARENT_LLVM_DIR's install prefix,
# the configure must succeed cleanly. This is the same configuration the
# parent build ran with, so a failure here means the gate is over-eager.
set(_match_dir "${SCRATCH_ROOT}/match")
_chcg_configure("${_match_dir}" _rc _stdout _stderr
    "-S" "${STURM_SOURCE_DIR}"
    "-B" "${_match_dir}"
    "-DCMAKE_CXX_COMPILER=${PARENT_CXX_COMPILER}"
    "-DCMAKE_C_COMPILER=${PARENT_C_COMPILER}"
    "-DLLVM_DIR=${PARENT_LLVM_DIR}"
    "-DClang_DIR=${PARENT_CLANG_DIR}")

if(NOT _rc EQUAL 0)
    string(SUBSTRING "${_stderr}" 0 4096 _head)
    message(FATAL_ERROR
        "cmake_host_clang_gate: MATCH configure FAILED (exit ${_rc}). The "
        "gate must NOT fire when CMAKE_CXX_COMPILER (${PARENT_CXX_COMPILER}) "
        "and LLVM_DIR (${PARENT_LLVM_DIR}) share an install prefix. This is "
        "the same toolchain the parent build is using, so a failure here "
        "means the gate is over-eager.\n"
        "Configure log: ${_match_dir}/configure.log\n"
        "Stderr (first 4KB):\n${_head}")
endif()

message(STATUS "cmake_host_clang_gate: MATCH case OK — configure "
               "succeeded with paired CMAKE_CXX_COMPILER + LLVM_DIR.")

# ── Case 3: UNSET — no compiler / no LLVM_DIR override ────────────────────
#
# When the user gives the configure NEITHER CMAKE_CXX_COMPILER NOR
# LLVM_DIR, the gate must NOT trigger. The configure may still fail
# downstream (find_package(LLVM CONFIG) without LLVM_DIR is brittle on
# distros that do not ship LLVM via the default prefix), but the
# failure must NOT come from our gate.
#
# Strategy: try the configure WITHOUT any toolchain hints. If it
# happens to succeed (e.g. LLVM is on the system default search path),
# great. If it fails, we scan the diagnostic output and verify our
# gate's signature ("FrontendPluginRegistry") is absent.
set(_unset_dir "${SCRATCH_ROOT}/unset")
_chcg_configure("${_unset_dir}" _rc _stdout _stderr
    "-S" "${STURM_SOURCE_DIR}"
    "-B" "${_unset_dir}")

set(_combined "${_stdout}\n${_stderr}")
if(_combined MATCHES "FrontendPluginRegistry")
    string(SUBSTRING "${_combined}" 0 4096 _head)
    message(FATAL_ERROR
        "cmake_host_clang_gate: UNSET configure (no CMAKE_CXX_COMPILER, "
        "no LLVM_DIR) hit the host-clang gate even though no LLVM_DIR was "
        "supplied. The gate must trigger on the MISMATCH between two "
        "explicit installs, not on the absence of LLVM_DIR (sturm-yial "
        "acceptance criterion 3).\n"
        "Captured output (first 4KB):\n${_head}")
endif()

message(STATUS "cmake_host_clang_gate: UNSET case OK — gate did not "
               "trigger when LLVM_DIR was absent.")

message(STATUS "cmake_host_clang_gate: PASS — host-clang vs plugin-LLVM "
               "gate fires on mismatch, stays silent on match and on "
               "unset (sturm-yial).")
