# run_a1_diff.cmake — sturm-f8ib / Frontend simpl. P9 / PRD A1
# textual-diff sub-gate.
#
# A1 has two parts: (a) byte-identical stdout regression, owned by
# `test_qram_demo_byte_identical` (F-8 / sturm-yggr); (b) the source-
# level textual identity between `examples/qram_demo.cpp` and the PRD
# §4 "After" listing. This driver owns part (b).
#
# A1 reconciliation (sturm-f8ib NOTES): the original PRD §4 "After"
# listing was a two-include schematic that omitted `sturm/qram.h` (the
# opt-in header that makes `qint b = a[i];` link) and the per-bit
# `phi()` rotations needed to encode `i = 2` as a non-trivial gate
# stream. Updating the PRD listing (option (a) in the issue) is the
# cleanest reconciliation: it preserves the byte-identity test contract
# and avoids a behavioral change to the demo just to match a buggy
# source-of-truth listing.
#
# Mechanics: the PRD's §4 "After" code block is fenced by ` ```cpp `
# and ` ``` ` markers. We extract the first cpp block following the
# "**After**" header, normalise both sides to LF + trailing-newline,
# and `string(COMPARE EQUAL)`. Any drift between the demo and the PRD
# source-of-truth listing fails this gate loud.
#
# Inputs (passed via -D from `add_test`):
#   PRD_PATH       — absolute path to docs/prd_frontend_simplification.md
#   DEMO_PATH      — absolute path to examples/qram_demo.cpp

cmake_minimum_required(VERSION 3.16)

foreach(_required PRD_PATH DEMO_PATH)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "run_a1_diff: ${_required} not set.")
    endif()
    if(NOT EXISTS "${${_required}}")
        message(FATAL_ERROR "run_a1_diff: ${_required}=${${_required}} does not exist.")
    endif()
endforeach()

file(READ "${PRD_PATH}" _prd_text)
file(READ "${DEMO_PATH}" _demo_text)

# Locate the **After** listing's fenced cpp block. The PRD §4 layout is:
#   **After** (… reconciliation prose …):
#
#   ```cpp
#   <listing>
#   ```
# We anchor on the literal "**After**" header marker, then advance to
# the first ```cpp fence after it.
string(FIND "${_prd_text}" "**After**" _after_pos)
if(_after_pos EQUAL -1)
    message(FATAL_ERROR
        "run_a1_diff: PRD §4 '**After**' header not found in ${PRD_PATH}.")
endif()
string(SUBSTRING "${_prd_text}" ${_after_pos} -1 _after_tail)
string(FIND "${_after_tail}" "```cpp" _open_rel)
if(_open_rel EQUAL -1)
    message(FATAL_ERROR
        "run_a1_diff: PRD §4 'After' has no following ```cpp fence.")
endif()
math(EXPR _open_pos "${_open_rel} + 6") # skip past "```cpp"
string(SUBSTRING "${_after_tail}" ${_open_pos} -1 _block_tail)
# Skip the LF that follows the opening fence so the extracted listing
# starts at the first source line.
string(SUBSTRING "${_block_tail}" 1 -1 _block_tail)
string(FIND "${_block_tail}" "```" _close_rel)
if(_close_rel EQUAL -1)
    message(FATAL_ERROR
        "run_a1_diff: PRD §4 'After' code block has no closing fence.")
endif()
string(SUBSTRING "${_block_tail}" 0 ${_close_rel} _prd_listing)

# Normalise both sides: strip trailing whitespace per line, ensure
# exactly one trailing LF. Filesystem newline form differs per host;
# CMake's file(READ) returns LF-only on Unix.
string(REGEX REPLACE "[ \t]+\n" "\n" _prd_listing "${_prd_listing}")
string(REGEX REPLACE "[ \t]+\n" "\n" _demo_text   "${_demo_text}")
string(REGEX REPLACE "\n+$" "\n" _prd_listing "${_prd_listing}")
string(REGEX REPLACE "\n+$" "\n" _demo_text   "${_demo_text}")

if(_prd_listing STREQUAL _demo_text)
    message(STATUS "run_a1_diff: PASS — examples/qram_demo.cpp matches "
                   "PRD §4 'After' listing byte-for-byte.")
    return()
endif()

# Mismatch: emit a triage hint with the first divergent line.
string(LENGTH "${_prd_listing}" _prd_len)
string(LENGTH "${_demo_text}"   _demo_len)
message(FATAL_ERROR
    "run_a1_diff: A1 textual-diff sub-gate FAILED.\n"
    "  PRD listing  : ${_prd_len} bytes\n"
    "  Demo source  : ${_demo_len} bytes\n"
    "  Reconcile by editing PRD §4 'After' to match the demo source "
    "(option (a) in the sturm-f8ib NOTES) — DO NOT mutate the demo to "
    "match a buggy listing.\n"
    "  --- PRD §4 'After' (extracted) ---\n${_prd_listing}\n"
    "  --- examples/qram_demo.cpp ---\n${_demo_text}")
