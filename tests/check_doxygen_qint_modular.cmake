# check_doxygen_qint_modular.cmake — sturm-6ov3.2 ctest driver.
#
# Verifies the Phase 7 plan §9.1 #2 contract for the Doxygen documentation
# of the public modular-arithmetic free functions in
# `include/sturm/ops/qint_modular.hpp`:
#
#   For each of `add_mod`, `mul_mod`, `pow_mod` the header must carry a
#   Doxygen comment block (`/** ... */` or `///`) attached to that function
#   that contains:
#     1. an `@pre` clause stating the precondition (operands in [0, n));
#     2. a description of the behavior under violation of that precondition
#        (the result is "undefined" / "undefined behavior");
#     3. a reference to the PRD section that defines the trust model
#        (`PRD §5`).
#
# This discharges PRD §5 bullet 1: "The Doxygen header for each `*_mod`
# free function and each `lib_*_mod_dsl` primitive."
#
# Method
# ------
# Pure file scan — no compilation, so the test runs in <100ms. The script
# reads the header into memory and uses CMake regex to extract, for each
# free function, the contiguous Doxygen comment block immediately
# preceding the `template <std::size_t W>` line, then asserts the three
# needles above appear in that captured block.
#
# Implementation note: CMake's `string(REPLACE "\n" ";")` pattern is
# unsafe here because the Doxygen blocks may legitimately contain `;`
# characters (they appear in CMake output, brace-init expressions, etc.),
# which would corrupt the resulting list. We use regex with `[\s\S]` /
# multi-line patterns instead.
#
# Inputs (passed via `-D` from `tests/CMakeLists.txt`):
#   STURM_SOURCE_DIR — absolute path to the STURM source tree.
#
# Exit discipline
# ---------------
# message(FATAL_ERROR ...) on any contract violation; messages quote the
# captured block (or note its absence) so the failure can be debugged
# without re-reading the header.

cmake_minimum_required(VERSION 3.16)

# ── Argument validation ────────────────────────────────────────────────────
if(NOT DEFINED STURM_SOURCE_DIR)
    message(FATAL_ERROR
        "check_doxygen_qint_modular: STURM_SOURCE_DIR not set. The ctest "
        "wiring in tests/CMakeLists.txt must pass -DSTURM_SOURCE_DIR=<path>.")
endif()

set(_header
    "${STURM_SOURCE_DIR}/include/sturm/ops/qint_modular.hpp")
if(NOT EXISTS "${_header}")
    message(FATAL_ERROR
        "check_doxygen_qint_modular: header missing: ${_header}")
endif()

file(READ "${_header}" _src)

# ── Per-function Doxygen scan ──────────────────────────────────────────────
#
# The contract is per-function: each of `add_mod`, `mul_mod`, `pow_mod`
# must have its own Doxygen block. We extract the Doxygen block as the
# closest `/** ... */` or `///`-run that immediately precedes the
# `template <std::size_t W>` line of that function.
#
# We anchor the search on the function signature literal
# `qint_t<W> ${_fn}(` to avoid accidental matches inside helper namespaces
# (e.g. the `pow_mod_classical` helper inside `detail_qint_modular`).

foreach(_fn add_mod mul_mod pow_mod)
    string(FIND "${_src}" "qint_t<W> ${_fn}(" _fn_pos)
    if(_fn_pos EQUAL -1)
        message(FATAL_ERROR
            "check_doxygen_qint_modular: could not locate the declaration "
            "`qint_t<W> ${_fn}(` in ${_header}. The header may have been "
            "restructured; update this test alongside the rename.")
    endif()

    # Slice the prefix up to (and excluding) the signature line. The
    # Doxygen block, if present, lives at the tail of this slice.
    string(SUBSTRING "${_src}" 0 ${_fn_pos} _prefix)

    # Strip the trailing `template <std::size_t W>` line; what remains
    # ends at the closing `*/` (block-style) or last `///` (line-style)
    # of the doc block. CMake regex `MATCHES` greedily anchors at the
    # whole-string match, so we use REGEX REPLACE to drop the suffix.
    string(REGEX REPLACE
        "[\n\r]*template[ \t]*<[^\n]*>[ \t]*[\n\r]+$"
        ""
        _prefix_no_template
        "${_prefix}")

    if(_prefix_no_template STREQUAL "${_prefix}")
        message(FATAL_ERROR
            "check_doxygen_qint_modular: could not strip the "
            "`template <...>` prefix line for ${_fn} in ${_header}. "
            "Header structure changed?")
    endif()

    # Now extract the trailing Doxygen block. We accept either:
    #   - block-style: `/** ... */` ending at the tail of the prefix;
    #   - line-style: a contiguous run of lines starting with `///`.
    #
    # CMake's regex engine is greedy and POSIX-ERE — `(.|\n)*` between
    # `/**` and `*/` would span across an *earlier* `*/` if such a
    # block exists for a previous function. Defense: locate the LAST
    # occurrence of `/**` in the prefix via `string(FIND ... REVERSE)`,
    # then verify the slice from there to end-of-prefix matches a single
    # well-formed `/** ... */` block (no intervening `*/` before the
    # final closing tag).
    set(_doc_block "")

    string(FIND "${_prefix_no_template}" "/**" _open_pos REVERSE)
    if(NOT _open_pos EQUAL -1)
        string(SUBSTRING "${_prefix_no_template}" ${_open_pos} -1
            _candidate)
        # The slice must end with `*/` (allowing trailing whitespace /
        # newlines that REGEX REPLACE already stripped above).
        if(_candidate MATCHES "\\*/[ \t\n\r]*$")
            # Verify there is no *interior* `*/` before the trailing one,
            # which would mean we are spanning past a different block.
            # Strip the final `*/...$` and look for any other `*/` left.
            string(REGEX REPLACE "\\*/[ \t\n\r]*$" "" _interior
                "${_candidate}")
            if(NOT _interior MATCHES "\\*/")
                set(_doc_block "${_candidate}")
            endif()
        endif()
    endif()

    if(NOT _doc_block)
        # Try line-style: capture trailing run of `///` lines.
        string(REGEX MATCH "(///[^\n]*[\n\r]+)+$"
            _doc_block "${_prefix_no_template}")
    endif()

    if(NOT _doc_block)
        message(FATAL_ERROR
            "check_doxygen_qint_modular: ${_fn} in ${_header} has NO "
            "Doxygen comment block attached. Plan §9.1 #2 / PRD §5 require "
            "each free function to carry a Doxygen header with @pre, "
            "behavior under violation, and a PRD §5 reference.")
    endif()

    # Sanity check: the extracted block should not span past the previous
    # function (otherwise we'd be matching the wrong block when the
    # current function lacks a Doxygen header). Heuristic: the block must
    # not contain a closing `}` of a function body on its own line.
    if(_doc_block MATCHES "[\n\r]}[\n\r]")
        message(FATAL_ERROR
            "check_doxygen_qint_modular: ${_fn} in ${_header} has no "
            "Doxygen block immediately preceding it — the regex captured "
            "across a function body, which means the documentation belongs "
            "to a previous function. Plan §9.1 #2 / PRD §5 require a "
            "per-function Doxygen header.")
    endif()

    # ── Validate the captured block contains all three required needles.
    #   1. `@pre` clause — Doxygen tag for preconditions.
    #   2. behavior under violation — explicit "undefined" wording (matches
    #      PRD §5 wording: "is undefined behavior").
    #   3. PRD reference — explicit "PRD §5" citation so a reader can
    #      land on the trust-model section in one hop.
    set(_needles_keys "@pre"  "undefined"  "PRD §5")
    set(_needles_lbls
        "@pre clause (operand precondition)"
        "behavior-under-violation wording"
        "reference to PRD §5 (trust model)")
    set(_idx 0)
    foreach(_needle IN LISTS _needles_keys)
        list(GET _needles_lbls ${_idx} _label)
        if(NOT _doc_block MATCHES "${_needle}")
            message(FATAL_ERROR
                "check_doxygen_qint_modular: ${_fn}'s Doxygen block is "
                "missing the ${_label}: needle `${_needle}` not found. "
                "Captured block:\n${_doc_block}")
        endif()
        math(EXPR _idx "${_idx} + 1")
    endforeach()

    message(STATUS
        "check_doxygen_qint_modular: ${_fn} OK — Doxygen block contains "
        "@pre, undefined-behavior wording, and PRD §5 reference.")
endforeach()

message(STATUS "check_doxygen_qint_modular: PASS — add_mod / mul_mod / "
               "pow_mod each carry a Doxygen header per plan §9.1 #2 / "
               "PRD §5.")
