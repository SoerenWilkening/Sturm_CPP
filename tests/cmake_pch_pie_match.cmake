# cmake_pch_pie_match.cmake — sturm-2vbr ctest driver.
#
# Verifies that the `sturm-transpile` driver (executable) and the
# `sturm-transpile-plugin` (SHARED library) compile with matching
# Position-Independent-Code state, so the PCH the plugin reuses via
# `target_precompile_headers(... REUSE_FROM sturm-transpile)` does not
# trigger a `is pie differs in PCH file vs. current file` error on
# Linux clang++-17.
#
# Background
# ----------
# `transpiler/CMakeLists.txt` REUSE_FROMs the driver's PCH on the plugin
# target. CMake REUSE_FROM enforces matching compile options *between
# the two targets' explicit settings* but does NOT normalize the
# compiler's default PIE state per target type. On Linux clang++-17,
# executables default to `-fpie` while SHARED libraries are forced to
# `-fPIC`. The PCH header file produced by the driver is therefore
# tagged `is pie = true`; loading it from the plugin (`is pie = false`)
# trips clang's `is pie differs` diagnostic and fails the build.
#
# The fix (sturm-2vbr proposed-shape #1): pin
# `POSITION_INDEPENDENT_CODE ON` on the driver so it compiles with
# `-fPIC` (matching the plugin's `is pie = false`). On macOS the change
# is a no-op (PIC is the only relocation model on Mach-O); on Linux it
# unifies the PCH PIE state across the two targets.
#
# Contract pinned by this test
# ----------------------------
#   1. The driver target's per-target compile flags include `-fPIC`.
#      Surfaced via `${build}/transpiler/CMakeFiles/sturm-transpile.dir/
#      flags.make` (CXX_FLAGS line). Without `-fPIC` on the driver, the
#      Linux PCH PIE mismatch reproduces.
#   2. The plugin target's per-target compile flags include `-fPIC`.
#      Always true for SHARED on Linux; this is asserted as a
#      sanity check so the assertion-pair stays in sync.
#   3. The driver and plugin agree on the `-fPIC` state — i.e. either
#      both have `-fPIC` or both do NOT. This is the actual contract
#      the PCH cares about; on macOS Mach-O the flag is a no-op (PIC is
#      forced), so the assertion is "they match", not "both have -fPIC".
#
# Method
# ------
# Run one out-of-tree configure of the STURM source tree (forwarding
# the parent build's resolved LLVM/Clang/compiler hints), then read the
# generated Makefile flags for the two transpiler targets and compare
# their `-fPIC` state. Configure-only (no compilation), so the test
# runs in a few seconds.
#
# Inputs (passed via `-D` from `tests/CMakeLists.txt`):
#   STURM_SOURCE_DIR     — absolute path to the STURM source tree.
#   SCRATCH_ROOT         — absolute path to a writable scratch root.
#   PARENT_LLVM_DIR      — value to pass through as `-DLLVM_DIR=...`
#                          (forward parent's resolved LLVM CMake dir).
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
            "cmake_pch_pie_match: ${_required_arg} not set. The ctest "
            "wiring in tests/CMakeLists.txt must pass "
            "-D${_required_arg}=<path>.")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${STURM_SOURCE_DIR}")
    message(FATAL_ERROR
        "cmake_pch_pie_match: STURM_SOURCE_DIR does not exist or is "
        "not a directory: ${STURM_SOURCE_DIR}")
endif()

if(NOT EXISTS "${STURM_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "cmake_pch_pie_match: STURM_SOURCE_DIR is missing the "
        "top-level CMakeLists.txt: ${STURM_SOURCE_DIR}")
endif()

# ── Run one configure forwarding the parent toolchain ─────────────────────
set(_build_dir "${SCRATCH_ROOT}/configure")
file(REMOVE_RECURSE "${_build_dir}")
file(MAKE_DIRECTORY "${_build_dir}")

set(_args
    "-S" "${STURM_SOURCE_DIR}"
    "-B" "${_build_dir}"
    # Force Unix Makefiles so the per-target `flags.make` location is
    # well-defined. Other generators (Ninja) put the same data in
    # `build.ninja`, but the test only needs to inspect ONE generator
    # target so we pin the simpler one.
    "-G" "Unix Makefiles"
    # sturm-e9gj: the top-level CMakeLists.txt refuses non-Ninja
    # generators by default (no silent slow-build fallback). This
    # test deliberately needs Unix Makefiles to read flags.make, so
    # it opts in via the documented escape hatch.
    "-DSTURM_ALLOW_NON_NINJA=ON"
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
        "cmake_pch_pie_match: parent-mirror configure FAILED (exit ${_rc}). "
        "Configure log: ${_build_dir}/configure.log\n"
        "Stderr (first 4KB):\n${_head}")
endif()

# ── Helper: read the CXX_FLAGS line from a target's flags.make ────────────
#
# `flags.make` is written by Unix Makefiles for every target. The line
# of interest is `CXX_FLAGS = ...` — it accumulates the per-target
# `target_compile_options` plus the language-standard / PIC flags CMake
# injects automatically. We do NOT inspect `compile_commands.json`
# because the same TU (e.g. `transpile_consumer.cpp`) appears under
# both targets' compile commands and there is no per-target
# discriminator field.
function(_pcpm_read_cxx_flags target_dir out_flags)
    set(_flags_make "${target_dir}/flags.make")
    if(NOT EXISTS "${_flags_make}")
        message(FATAL_ERROR
            "cmake_pch_pie_match: expected flags.make at ${_flags_make}; "
            "the parent-mirror configure must have produced it.")
    endif()
    file(READ "${_flags_make}" _content)
    # Match the FIRST `CXX_FLAGS = ...` line (the per-target default;
    # subsequent lines are PCH-specific overrides we don't want to
    # confuse with the canonical flag set).
    if(NOT _content MATCHES "CXX_FLAGS *= *([^\n]*)")
        message(FATAL_ERROR
            "cmake_pch_pie_match: ${_flags_make} does not contain a "
            "CXX_FLAGS line; cannot read per-target compile flags.")
    endif()
    set(${out_flags} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

set(_driver_dir
    "${_build_dir}/transpiler/CMakeFiles/sturm-transpile.dir")
set(_plugin_dir
    "${_build_dir}/transpiler/CMakeFiles/sturm-transpile-plugin.dir")

_pcpm_read_cxx_flags("${_driver_dir}" _driver_flags)
_pcpm_read_cxx_flags("${_plugin_dir}" _plugin_flags)

message(STATUS "cmake_pch_pie_match: driver CXX_FLAGS = ${_driver_flags}")
message(STATUS "cmake_pch_pie_match: plugin CXX_FLAGS = ${_plugin_flags}")

# ── Contract 1: driver carries -fPIC ──────────────────────────────────────
# Scan as a whitespace-separated token list — `_driver_flags MATCHES
# "-fPIC"` would also fire on `-fPIC-something-else`, which is
# sub-optimal.
string(REGEX REPLACE "[ \t]+" ";" _driver_tokens "${_driver_flags}")
list(FIND _driver_tokens "-fPIC" _driver_pic_idx)
if(_driver_pic_idx EQUAL -1)
    message(FATAL_ERROR
        "cmake_pch_pie_match: sturm-transpile driver does NOT carry -fPIC "
        "in its per-target CXX_FLAGS. Without -fPIC the driver compiles "
        "with the host clang's default PIE state (`-fpie` on Linux), the "
        "PCH it produces is tagged `is pie = true`, and the "
        "sturm-transpile-plugin's REUSE_FROM load fails with `is pie "
        "differs in PCH file vs. current file` (sturm-2vbr).\n"
        "  driver CXX_FLAGS = ${_driver_flags}\n"
        "  flags.make path  = ${_driver_dir}/flags.make\n"
        "Fix: set POSITION_INDEPENDENT_CODE ON on the sturm-transpile "
        "target in transpiler/CMakeLists.txt.")
endif()

# ── Contract 2: plugin carries -fPIC ──────────────────────────────────────
# Sanity check — a SHARED library on every platform we support emits
# `-fPIC` automatically. If this assertion ever flips, the contract has
# changed and the matched-PIE assertion (#3) below needs an update.
string(REGEX REPLACE "[ \t]+" ";" _plugin_tokens "${_plugin_flags}")
list(FIND _plugin_tokens "-fPIC" _plugin_pic_idx)
if(_plugin_pic_idx EQUAL -1)
    message(FATAL_ERROR
        "cmake_pch_pie_match: sturm-transpile-plugin does NOT carry -fPIC "
        "in its per-target CXX_FLAGS. SHARED libraries should always emit "
        "-fPIC; if this assertion fires, the sturm-2vbr fix is no longer "
        "addressing the right problem.\n"
        "  plugin CXX_FLAGS = ${_plugin_flags}\n"
        "  flags.make path  = ${_plugin_dir}/flags.make")
endif()

# ── Contract 3: driver and plugin agree on -fPIC ──────────────────────────
# Implied by #1 and #2 above (both must be present). Pinned explicitly
# so a future change that flips both off (e.g. STURM_USE_PCH=OFF + a
# different relocation model) still gets caught.
if(NOT _driver_pic_idx EQUAL -1 AND NOT _plugin_pic_idx EQUAL -1)
    message(STATUS "cmake_pch_pie_match: driver and plugin both carry "
                   "-fPIC — PCH PIE state will match across REUSE_FROM.")
elseif(_driver_pic_idx EQUAL -1 AND _plugin_pic_idx EQUAL -1)
    message(STATUS "cmake_pch_pie_match: driver and plugin both lack "
                   "-fPIC — PCH PIE state matches by absence (rare).")
else()
    message(FATAL_ERROR
        "cmake_pch_pie_match: driver vs plugin -fPIC state DIFFERS. "
        "REUSE_FROM PCH load will fail with `is pie differs` on Linux "
        "(sturm-2vbr).\n"
        "  driver -fPIC: ${_driver_pic_idx} (-1 = absent)\n"
        "  plugin -fPIC: ${_plugin_pic_idx} (-1 = absent)\n"
        "  driver CXX_FLAGS = ${_driver_flags}\n"
        "  plugin CXX_FLAGS = ${_plugin_flags}")
endif()

message(STATUS "cmake_pch_pie_match: PASS — sturm-transpile and "
               "sturm-transpile-plugin agree on -fPIC; the PCH PIE "
               "state matches across the REUSE_FROM boundary "
               "(sturm-2vbr).")
