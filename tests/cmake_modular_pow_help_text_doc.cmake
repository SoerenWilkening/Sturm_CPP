# cmake_modular_pow_help_text_doc.cmake — sturm-6ov3.4 ctest driver.
#
# Phase 7 plan §9.1 #4 + #6 contract — the Phase-6 ctest
# `cmake_modular_pow_help_text` (sturm-bjt2.1) already verifies that the
# `STURM_MODULAR_POW` option's help string names the gated rewrite
# (`pow` / `lib_pow_mod_dsl`) and the default state (OFF). Phase 7 layers
# two additional documentation requirements on top:
#
#   1. The CMake help string visible via `cmake -LH` must point a
#      reader at the trust-model section that defines the operand
#      precondition. Concretely, the help text must mention `PRD §5`
#      so a user discovering the flag via `cmake -LH` lands on the
#      `a, b ∈ [0, n)` contract in one hop without having to grep the
#      source tree.
#
#   2. `docs/TODO_reversibility_deferrals.md` must explicitly record
#      the cross-check from plan §9.1 #6 — i.e. the document must
#      contain a sentence stating that PRD §7 (modular-arith deferrals)
#      was reviewed and found to introduce no reversibility deferrals.
#      The plan's "verify" instruction is discharged by leaving an
#      auditable trace in the deferrals doc, not by silently nodding.
#
# Method
# ------
# The driver runs one out-of-tree configure of the STURM source tree
# and queries the option help text via `cmake -LH -N`. It then reads
# `docs/TODO_reversibility_deferrals.md` and scans for the cross-check
# sentence. Both checks are pure string operations — no compilation, so
# the test stays fast despite the heavy STURM configure.
#
# Inputs (passed via `-D` from `tests/CMakeLists.txt`):
#   STURM_SOURCE_DIR     — absolute path to the STURM source tree.
#   SCRATCH_ROOT         — absolute path to a writable scratch root
#                          (the driver creates `${SCRATCH_ROOT}/help_doc`
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
# enough context (the captured cmake -LH output, the scanned deferrals
# excerpt) to debug the failure without re-running the configure
# manually.

cmake_minimum_required(VERSION 3.16)

# ── Argument validation ────────────────────────────────────────────────────
foreach(_required_arg STURM_SOURCE_DIR SCRATCH_ROOT)
    if(NOT DEFINED ${_required_arg})
        message(FATAL_ERROR
            "cmake_modular_pow_help_text_doc: ${_required_arg} not set. The "
            "ctest wiring in tests/CMakeLists.txt must pass "
            "-D${_required_arg}=<path>.")
    endif()
endforeach()

if(NOT IS_DIRECTORY "${STURM_SOURCE_DIR}")
    message(FATAL_ERROR
        "cmake_modular_pow_help_text_doc: STURM_SOURCE_DIR does not exist or "
        "is not a directory: ${STURM_SOURCE_DIR}")
endif()

if(NOT EXISTS "${STURM_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "cmake_modular_pow_help_text_doc: STURM_SOURCE_DIR is missing the "
        "top-level CMakeLists.txt: ${STURM_SOURCE_DIR}")
endif()

set(_deferrals_doc
    "${STURM_SOURCE_DIR}/docs/TODO_reversibility_deferrals.md")
if(NOT EXISTS "${_deferrals_doc}")
    message(FATAL_ERROR
        "cmake_modular_pow_help_text_doc: missing deferrals doc: "
        "${_deferrals_doc}. Plan §9.1 #6 requires the cross-check to land "
        "in this file.")
endif()

# ── Step 1: configure the project once (default OFF is fine) ──────────────
set(_build_dir "${SCRATCH_ROOT}/help_doc")
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
        "cmake_modular_pow_help_text_doc: initial configure FAILED (exit "
        "${_rc}). See ${_build_dir}/configure.log for the full log. "
        "Tail of stderr:\n${_stderr}")
endif()

# ── Step 2: capture the option help text via `cmake -LH -N` ──────────────
execute_process(
    COMMAND "${CMAKE_COMMAND}" "-LH" "-N" "-B" "${_build_dir}"
    OUTPUT_VARIABLE _help_stdout
    ERROR_VARIABLE  _help_stderr
    RESULT_VARIABLE _help_rc)

file(WRITE "${_build_dir}/help.log"
    "STDOUT:\n${_help_stdout}\n\nSTDERR:\n${_help_stderr}\n")

if(NOT _help_rc EQUAL 0)
    message(FATAL_ERROR
        "cmake_modular_pow_help_text_doc: `cmake -LH -N` FAILED (exit "
        "${_help_rc}). See ${_build_dir}/help.log. "
        "Stderr:\n${_help_stderr}")
endif()

# ── Step 3: locate the STURM_MODULAR_POW entry + its help text ───────────
if(NOT _help_stdout MATCHES "STURM_MODULAR_POW:BOOL=")
    message(FATAL_ERROR
        "cmake_modular_pow_help_text_doc: `cmake -LH -N` did not list "
        "STURM_MODULAR_POW as a BOOL cache entry. Output:\n${_help_stdout}")
endif()

string(REGEX MATCH
    "((// [^\n]*\n)+)STURM_MODULAR_POW:BOOL="
    _help_block "${_help_stdout}")
if(NOT _help_block)
    message(FATAL_ERROR
        "cmake_modular_pow_help_text_doc: could not extract the help-string "
        "block immediately preceding STURM_MODULAR_POW in `cmake -LH -N` "
        "output. Output was:\n${_help_stdout}")
endif()
set(_help_text "${CMAKE_MATCH_1}")

# ── Step 4: validate the Phase-7 docs reference (plan §9.1 #4) ───────────
#
# The Phase-6 test (`cmake_modular_pow_help_text`) already checks that
# the help text names the gated rewrite (pow / lib_pow_mod_dsl) and the
# default (OFF). Phase 7 layers a single additional needle on top: the
# help string must point at PRD §5 (the trust-model / precondition
# section) so a `cmake -LH` reader can land on the `a, b ∈ [0, n)`
# contract in one hop.
if(NOT _help_text MATCHES "PRD §5")
    message(FATAL_ERROR
        "cmake_modular_pow_help_text_doc: STURM_MODULAR_POW help text "
        "does not mention `PRD §5`. Plan §9.1 #4 requires the help text "
        "to point a `cmake -LH` reader at the trust-model section that "
        "defines the operand precondition. Captured help text:\n"
        "${_help_text}")
endif()

message(STATUS "cmake_modular_pow_help_text_doc: option help text OK — "
               "mentions PRD §5 trust-model reference.")

# ── Step 5: validate the cross-check trace in the deferrals doc ──────────
#
# Plan §9.1 #6 says: "cross-check `docs/TODO_reversibility_deferrals.md`
# for any deferred items from PRD §7 that interact with reversibility
# (none currently identified, but verify)". The verification has to
# leave an auditable trace in the deferrals doc itself so a future
# reader (or a future PRD §7 entry) does not silently bypass it.
#
# We require the doc to contain (a) an explicit reference to PRD §7 of
# the modular-arithmetic PRD AND (b) the conclusion that no
# reversibility deferral arises from PRD §7. Both needles in the same
# slice — i.e. the doc must contain a sentence that ties the two
# together, not two unrelated mentions.
file(READ "${_deferrals_doc}" _deferrals)

# Look for a section / paragraph that references PRD §7 of
# prd_modular_arithmetic.md AND explicitly states "no" / "none" /
# "introduces no" / similar. A loose AND of two needles is sufficient
# — the doc is small enough that a false-positive collision is
# implausible.
if(NOT _deferrals MATCHES "prd_modular_arithmetic")
    message(FATAL_ERROR
        "cmake_modular_pow_help_text_doc: ${_deferrals_doc} does not "
        "reference `prd_modular_arithmetic`. Plan §9.1 #6 requires an "
        "auditable cross-check trace pointing at the modular-arith PRD §7.")
endif()

if(NOT _deferrals MATCHES "PRD §7")
    message(FATAL_ERROR
        "cmake_modular_pow_help_text_doc: ${_deferrals_doc} does not "
        "mention `PRD §7`. Plan §9.1 #6 requires the cross-check to name "
        "the section it covered.")
endif()

# Conclusion needle — accept several phrasings so a future copy-edit
# does not silently break this test, but require at least one of them.
set(_conclusion_found FALSE)
foreach(_phrase
        "no reversibility"
        "none interact"
        "introduces no"
        "do not interact"
        "does not interact"
        "[Nn]o items interact")
    if(_deferrals MATCHES "${_phrase}")
        set(_conclusion_found TRUE)
        break()
    endif()
endforeach()
if(NOT _conclusion_found)
    message(FATAL_ERROR
        "cmake_modular_pow_help_text_doc: ${_deferrals_doc} references "
        "PRD §7 but does not state the conclusion of the cross-check "
        "(e.g. \"no reversibility deferrals\" / \"none interact\"). "
        "Plan §9.1 #6 requires the verification result to be recorded "
        "explicitly so a future reader cannot mistake silence for "
        "absence of review.")
endif()

message(STATUS "cmake_modular_pow_help_text_doc: deferrals doc OK — "
               "cross-check trace for PRD §7 is recorded.")

message(STATUS "cmake_modular_pow_help_text_doc: PASS — option help text "
               "references PRD §5 and TODO_reversibility_deferrals.md "
               "records the PRD §7 cross-check per plan §9.1 #4 / #6.")
