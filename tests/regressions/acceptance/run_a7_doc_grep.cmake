# run_a7_doc_grep.cmake — sturm-f8ib / Frontend simpl. P9 / PRD A7.
#
# A7 contract: `docs/qram_user_intro.md` and `docs/getting_started.md`
# reflect the new (post-Frontend-simplification) shape — the pre-PRD
# example shape must not be present in either file. Forbidden tokens:
#   - `kNumQubits` — the explicit qubit-count constant the pre-PRD
#     example required (PRD §1, §4 "Before").
#   - The "eight-include block" — any of the eight pre-PRD individual
#     `<sturm/.../*.hpp>` includes that the umbrella `sturm.h` replaces
#     (PRD §4 "Before"). We scan for each #include needle individually
#     so a single forbidden include is enough to fail the gate.
#
# Inputs (passed via -D from add_test):
#   DOC_PATHS — semicolon-separated list of absolute doc paths.

cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED DOC_PATHS)
    message(FATAL_ERROR "run_a7_doc_grep: DOC_PATHS not set.")
endif()

# Forbidden tokens. The first family is the explicit qubit-count
# constant from the pre-PRD example. The remainder is the 8-include
# block from PRD §4 "Before"; spotting any one of these in user-facing
# docs means the doc still teaches the deprecated shape.
set(_forbidden_tokens
    "kNumQubits"
    "sturm/backend/draw_ascii.hpp"
    "sturm/backend/exec_append.hpp"
    "sturm/backend/ir.hpp"
    "sturm/core/context.hpp"
    "sturm/core/core.h"
    "sturm/qram/qram_read.hpp"
    "sturm/qtypes/qint.hpp"
    "sturm/qtypes/qint_alias.hpp")

set(_violations "")
foreach(_doc ${DOC_PATHS})
    if(NOT EXISTS "${_doc}")
        message(FATAL_ERROR "run_a7_doc_grep: doc not found: ${_doc}")
    endif()
    file(READ "${_doc}" _doc_text)
    foreach(_needle ${_forbidden_tokens})
        string(FIND "${_doc_text}" "${_needle}" _hit)
        if(NOT _hit EQUAL -1)
            list(APPEND _violations "${_doc}: contains forbidden token `${_needle}`")
        endif()
    endforeach()
endforeach()

if(_violations)
    string(REPLACE ";" "\n  " _violations_pretty "${_violations}")
    message(FATAL_ERROR
        "run_a7_doc_grep: A7 FAILED — pre-PRD example shape leaks into "
        "user-facing docs:\n  ${_violations_pretty}\n"
        "Rewrite the doc to use the umbrella `sturm.h` + opt-in feature "
        "headers (PRD §4 'After') and drop the `kNumQubits` constant.")
endif()

message(STATUS "run_a7_doc_grep: PASS — no forbidden pre-PRD tokens "
               "in qram_user_intro.md / getting_started.md.")
