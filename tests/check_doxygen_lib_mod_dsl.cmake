# check_doxygen_lib_mod_dsl.cmake — sturm-6ov3.3 ctest driver.
#
# Verifies the Phase 7 plan §9.1 #3 contract for the Doxygen documentation
# of each `lib_*_mod_dsl` modular-arithmetic primitive header (both forward
# and adjoint sibling):
#
#   For each of `lib_add_mod_dsl`, `__lib_add_mod_dsl_adj`,
#   `lib_mul_mod_dsl`, `__lib_mul_mod_dsl_adj`, `lib_pow_mod_dsl`, and
#   `__lib_pow_mod_dsl_adj`, the corresponding header in
#   `include/sturm/lib/` must carry a Doxygen comment block (`/** ... */`
#   or `///`) attached to that primitive that contains:
#     1. an `@pre` clause stating the precondition (operands in [0, n));
#     2. a description of the behavior under violation of that precondition
#        (the result is "undefined" / "undefined behavior");
#     3. a reference to the PRD section that defines the trust model
#        (`PRD §5`).
#
# This discharges the second half of PRD §5 bullet 1: "The Doxygen header
# for each `*_mod` free function and each `lib_*_mod_dsl` primitive."
#
# Method
# ------
# Pure file scan — no compilation, so the test runs in <100ms. The script
# reads each header into memory and uses CMake regex to extract, for each
# primitive function, the contiguous Doxygen comment block immediately
# preceding the `template <typename Bit>` line, then asserts the three
# needles above appear in that captured block.
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
        "check_doxygen_lib_mod_dsl: STURM_SOURCE_DIR not set. The ctest "
        "wiring in tests/CMakeLists.txt must pass -DSTURM_SOURCE_DIR=<path>.")
endif()

# ── Per-primitive Doxygen scan ─────────────────────────────────────────────
#
# The contract is per-primitive: each forward + adjoint sibling pair must
# have its own Doxygen block. We extract the Doxygen block as the closest
# `/** ... */` or `///`-run that immediately precedes the
# `template <typename Bit>` line of that function.
#
# We anchor the search on the function signature literal
# `void ${_fn}(` to avoid accidental matches inside helper namespaces
# (e.g. `detail_add_mod::push_flag`).

# Map of primitive function name → relative header path inside
# include/sturm/lib/.  We pair each forward primitive with its adjoint
# sibling so a single ctest run validates the whole set.
set(_primitives
    "lib_add_mod_dsl|add_mod_dsl.hpp"
    "__lib_add_mod_dsl_adj|add_mod_dsl_adj.hpp"
    "lib_mul_mod_dsl|mul_mod_dsl.hpp"
    "__lib_mul_mod_dsl_adj|mul_mod_dsl_adj.hpp"
    "lib_pow_mod_dsl|pow_mod_dsl.hpp"
    "__lib_pow_mod_dsl_adj|pow_mod_dsl_adj.hpp"
)

foreach(_pair IN LISTS _primitives)
    string(REPLACE "|" ";" _split "${_pair}")
    list(GET _split 0 _fn)
    list(GET _split 1 _hdr_rel)
    set(_header
        "${STURM_SOURCE_DIR}/include/sturm/detail/lib/${_hdr_rel}")
    if(NOT EXISTS "${_header}")
        message(FATAL_ERROR
            "check_doxygen_lib_mod_dsl: header missing: ${_header}")
    endif()

    file(READ "${_header}" _src)

    string(FIND "${_src}" "void ${_fn}(" _fn_pos)
    if(_fn_pos EQUAL -1)
        message(FATAL_ERROR
            "check_doxygen_lib_mod_dsl: could not locate the declaration "
            "`void ${_fn}(` in ${_header}. The header may have been "
            "restructured; update this test alongside the rename.")
    endif()

    # Slice the prefix up to (and excluding) the signature line. The
    # Doxygen block, if present, lives at the tail of this slice.
    string(SUBSTRING "${_src}" 0 ${_fn_pos} _prefix)

    # Strip the trailing `inline ` (if present) and `template <...>` line;
    # what remains ends at the closing `*/` (block-style) or last `///`
    # (line-style) of the doc block.
    string(REGEX REPLACE
        "[\n\r]*template[ \t]*<[^\n]*>[ \t]*[\n\r]+(inline[ \t]+)?$"
        ""
        _prefix_no_template
        "${_prefix}")

    if(_prefix_no_template STREQUAL "${_prefix}")
        message(FATAL_ERROR
            "check_doxygen_lib_mod_dsl: could not strip the "
            "`template <...>` prefix line for ${_fn} in ${_header}. "
            "Header structure changed?")
    endif()

    # Now extract the trailing Doxygen block. We accept either:
    #   - block-style: `/** ... */` ending at the tail of the prefix;
    #   - line-style: a contiguous run of lines starting with `///`.
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
            "check_doxygen_lib_mod_dsl: ${_fn} in ${_header} has NO "
            "Doxygen comment block attached. Plan §9.1 #3 / PRD §5 require "
            "each primitive header to carry a Doxygen block with @pre, "
            "behavior under violation, and a PRD §5 reference.")
    endif()

    # Sanity check: the extracted block should not span past the previous
    # function (otherwise we'd be matching the wrong block when the
    # current function lacks a Doxygen header). Heuristic: the block must
    # not contain a closing `}` of a function body on its own line.
    if(_doc_block MATCHES "[\n\r]}[\n\r]")
        message(FATAL_ERROR
            "check_doxygen_lib_mod_dsl: ${_fn} in ${_header} has no "
            "Doxygen block immediately preceding it — the regex captured "
            "across a function body, which means the documentation belongs "
            "to a previous function. Plan §9.1 #3 / PRD §5 require a "
            "per-primitive Doxygen header.")
    endif()

    # ── Validate the captured block contains all three required needles.
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
                "check_doxygen_lib_mod_dsl: ${_fn}'s Doxygen block in "
                "${_header} is missing the ${_label}: needle `${_needle}` "
                "not found. Captured block:\n${_doc_block}")
        endif()
        math(EXPR _idx "${_idx} + 1")
    endforeach()

    message(STATUS
        "check_doxygen_lib_mod_dsl: ${_fn} OK — Doxygen block contains "
        "@pre, undefined-behavior wording, and PRD §5 reference.")
endforeach()

message(STATUS
    "check_doxygen_lib_mod_dsl: PASS — every lib_*_mod_dsl primitive "
    "(forward + adjoint) carries a Doxygen header per plan §9.1 #3 / "
    "PRD §5.")
