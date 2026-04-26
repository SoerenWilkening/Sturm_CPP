# cmake_modular_pow_help_text.cmake — sturm-bjt2.1 ctest driver.
#
# Verifies the Phase 6 plan §8.1 contract for the user-facing
# documentation of the `STURM_MODULAR_POW` CMake option:
#
#   1. The top-level CMakeLists declares `STURM_MODULAR_POW` as a BOOL
#      option whose help string mentions both the gated rewrite
#      (`pow(a, x) % n` => `lib_pow_mod_dsl`) and the default state
#      (OFF). The help text must be visible via `cmake -LH` (the
#      canonical way users discover build-time knobs).
#   2. The top-level `README.md` `Build from source` section documents
#      the flag, naming `STURM_MODULAR_POW`, mentioning the default
#      (OFF), and pointing at the underlying primitive
#      (`lib_pow_mod_dsl`).
#
# Method
# ------
# The driver runs one out-of-tree configure of the STURM source tree
# and then queries the option help text via `cmake -LH -N` (list cache
# entries with help, no configure step). Both the regex match against
# the captured stdout and the README scan run as plain CMake string
# operations — no compilation, so the test stays fast despite the
# heavy STURM configure.
#
# Inputs (passed via `-D` from `tests/CMakeLists.txt`):
#   STURM_SOURCE_DIR     — absolute path to the STURM source tree.
#   SCRATCH_ROOT         — absolute path to a writable scratch root
#                          (the driver creates `${SCRATCH_ROOT}/help`
#                          under it).
#   PARENT_LLVM_DIR      — value to pass through as `-DLLVM_DIR=...`
#                          for the child configure (may be empty).
#   PARENT_CLANG_DIR     — value to pass through as `-DClang_DIR=...`
#                          (may be empty).
#   PARENT_CXX_COMPILER  — value to pass through as
#                          `-DCMAKE_CXX_COMPILER=...` (may be empty).
#   PARENT_C_COMPILER    — value to pass through as
#                          `-DCMAKE_C_COMPILER=...` (may be empty).
#
# Exit discipline
# ---------------
# message(FATAL_ERROR ...) on any contract violation; messages carry
# enough context (the captured cmake -LH output, the scanned README
# excerpt) to debug the failure without re-running the configure
# manually.

cmake_minimum_required(VERSION 3.16)

# ── Argument validation ────────────────────────────────────────────────────
foreach(_required_arg STURM_SOURCE_DIR SCRATCH_ROOT)
    if(NOT DEFINED ${_required_arg})
        message(FATAL_ERROR
            "cmake_modular_pow_help_text: ${_required_arg} not set. The "
            "ctest wiring in tests/CMakeLists.txt must pass "
            "-D${_required_arg}=<path>.")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${STURM_SOURCE_DIR}")
    message(FATAL_ERROR
        "cmake_modular_pow_help_text: STURM_SOURCE_DIR does not exist or "
        "is not a directory: ${STURM_SOURCE_DIR}")
endif()

if(NOT EXISTS "${STURM_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "cmake_modular_pow_help_text: STURM_SOURCE_DIR is missing the "
        "top-level CMakeLists.txt: ${STURM_SOURCE_DIR}")
endif()

if(NOT EXISTS "${STURM_SOURCE_DIR}/README.md")
    message(FATAL_ERROR
        "cmake_modular_pow_help_text: STURM_SOURCE_DIR is missing the "
        "top-level README.md: ${STURM_SOURCE_DIR}")
endif()

# ── Step 1: configure the project once (default OFF is fine) ──────────────
set(_build_dir "${SCRATCH_ROOT}/help")
file(REMOVE_RECURSE "${_build_dir}")
file(MAKE_DIRECTORY "${_build_dir}")

set(_args
    "-S" "${STURM_SOURCE_DIR}"
    "-B" "${_build_dir}"
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
    "STDOUT:\n${_stdout}\n\nSTDERR:\n${_stderr}\n")

if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
        "cmake_modular_pow_help_text: initial configure FAILED (exit "
        "${_rc}). See ${_build_dir}/configure.log for the full log. "
        "Tail of stderr:\n${_stderr}")
endif()

# ── Step 2: capture the option help text via `cmake -LH -N` ──────────────
#
# `-N` skips the configure step (we already configured above), `-LH`
# lists cache entries WITH their help strings — the canonical way
# users discover build-time knobs. The help text appears as a `// ...`
# comment line directly above each `KEY:TYPE=value` cache entry.
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-LH" "-N" "-B" "${_build_dir}"
    OUTPUT_VARIABLE _help_stdout
    ERROR_VARIABLE  _help_stderr
    RESULT_VARIABLE _help_rc)

file(WRITE "${_build_dir}/help.log"
    "STDOUT:\n${_help_stdout}\n\nSTDERR:\n${_help_stderr}\n")

if(NOT _help_rc EQUAL 0)
    message(FATAL_ERROR
        "cmake_modular_pow_help_text: `cmake -LH -N` FAILED (exit "
        "${_help_rc}). See ${_build_dir}/help.log. Stderr:\n${_help_stderr}")
endif()

# ── Step 3: locate the STURM_MODULAR_POW entry + its help text ───────────
#
# Expected layout (see CMake docs for `-LH`):
#
#   // <multi-word help string for this entry>
#   STURM_MODULAR_POW:BOOL=OFF
#
# We capture (a) the help line(s) and (b) the cache type. Both must
# match the plan §8.1 contract.
if(NOT _help_stdout MATCHES "STURM_MODULAR_POW:BOOL=")
    message(FATAL_ERROR
        "cmake_modular_pow_help_text: `cmake -LH -N` did not list "
        "STURM_MODULAR_POW as a BOOL cache entry. Output:\n${_help_stdout}")
endif()

# Pull every `// ...` line that immediately precedes the
# STURM_MODULAR_POW entry. CMake `-LH` prints contiguous `// ...`
# lines for the entry's help string; we accept one or more such lines
# concatenated.
string(REGEX MATCH
    "((// [^\n]*\n)+)STURM_MODULAR_POW:BOOL="
    _help_block "${_help_stdout}")
if(NOT _help_block)
    message(FATAL_ERROR
        "cmake_modular_pow_help_text: could not extract the help-string "
        "block immediately preceding STURM_MODULAR_POW in `cmake -LH -N` "
        "output. Output was:\n${_help_stdout}")
endif()
set(_help_text "${CMAKE_MATCH_1}")

# ── Step 4: validate the help text content (plan §8.1 / PRD §3.4) ────────
#
# The plan is explicit: a one-line description of the effect. We do
# not police the literal line count (CMake wraps long help strings
# across `// ...` continuation lines when displayed), but we DO
# require the description to mention:
#   - the gated rewrite target (`pow` and `lib_pow_mod_dsl`), so a
#     reader knows what flipping the flag actually changes;
#   - the default state (OFF), so a reader knows the on-disk default
#     without reading the CMakeLists source.
foreach(_needle
        "pow"           # gated rewrite source pattern
        "lib_pow_mod_dsl"  # gated rewrite target primitive
        "OFF")          # default state
    if(NOT _help_text MATCHES "${_needle}")
        message(FATAL_ERROR
            "cmake_modular_pow_help_text: STURM_MODULAR_POW help text "
            "does not mention `${_needle}`. Plan §8.1 / PRD §3.4 "
            "require the help text to describe the gated rewrite and "
            "default state. Captured help text:\n${_help_text}")
    endif()
endforeach()

message(STATUS "cmake_modular_pow_help_text: option help text OK — "
               "mentions pow/lib_pow_mod_dsl/OFF.")

# ── Step 5: validate the README.md Build section ─────────────────────────
#
# Plan §8.1 requires a Build-section entry in the top-level README.md
# that documents the flag. We require the flag name itself, the
# default state (OFF), and a pointer to the gated primitive
# (lib_pow_mod_dsl) — the same three needles we required of the CMake
# help text, scoped to the README.
file(READ "${STURM_SOURCE_DIR}/README.md" _readme)

# Locate the Build section. The current README uses
# `**Build from source (contributors):**` as its build heading; accept
# any common variant (`## Build`, `### Build`, or the bold form) so a
# future restructure does not silently break the test.
string(REGEX MATCH
    "(\\*\\*Build[^*]*\\*\\*|## +Build[^\n]*|### +Build[^\n]*)"
    _build_anchor "${_readme}")
if(NOT _build_anchor)
    message(FATAL_ERROR
        "cmake_modular_pow_help_text: README.md is missing a Build "
        "section. Plan §8.1 requires the flag to be documented under "
        "the Build section.")
endif()

# Slice the README from the Build anchor onward; the flag
# documentation must live within that slice (i.e., in or after the
# Build section, not earlier in an unrelated paragraph).
string(FIND "${_readme}" "${_build_anchor}" _build_offset)
string(SUBSTRING "${_readme}" ${_build_offset} -1 _build_slice)

foreach(_needle
        "STURM_MODULAR_POW"
        "OFF"
        "lib_pow_mod_dsl")
    if(NOT _build_slice MATCHES "${_needle}")
        message(FATAL_ERROR
            "cmake_modular_pow_help_text: README.md Build section does "
            "not mention `${_needle}`. Plan §8.1 requires the Build "
            "section to document the STURM_MODULAR_POW flag (name, "
            "default OFF, and the gated primitive lib_pow_mod_dsl). "
            "Build slice (truncated to first 2KB):\n"
            )
        # Safety net: also print first 2KB of the slice for context.
        string(SUBSTRING "${_build_slice}" 0 2000 _slice_head)
        message(FATAL_ERROR "${_slice_head}")
    endif()
endforeach()

message(STATUS "cmake_modular_pow_help_text: README.md Build section OK "
               "— mentions STURM_MODULAR_POW/OFF/lib_pow_mod_dsl.")

message(STATUS "cmake_modular_pow_help_text: PASS — option help text "
               "and README.md Build section both document "
               "STURM_MODULAR_POW per plan §8.1 / PRD §3.4.")
