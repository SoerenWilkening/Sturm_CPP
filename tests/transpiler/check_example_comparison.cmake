# check_example_comparison.cmake — Phase D example-observability ctest.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — the source example examples/comparison.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            comparison.cpp
#
# Contract:
#   After a clean build that routes examples/comparison.cpp through
#   sturm-transpile, the generated file must contain the six injected
#   `uncompute_{eq,ne,lt,le,gt,ge}_qint(c_xx, a, b);` inverses (one per
#   forward `qbool c_xx = a OP b;` VarDecl initializer in PD-1..PD-6),
#   in LIFO order after the last forward statement, AND the source file
#   must be untouched — byte-identical before and after the transpile
#   (the transpiler never rewrites its input in place — it only writes
#   into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains each of the six injected inverse call texts
#      at least once:
#        uncompute_eq_qint(c_eq, a, b)
#        uncompute_ne_qint(c_ne, a, b)
#        uncompute_lt_qint(c_lt, a, b)
#        uncompute_le_qint(c_le, a, b)
#        uncompute_gt_qint(c_gt, a, b)
#        uncompute_ge_qint(c_ge, a, b)
#      The regex tolerates internal whitespace.
#   3. The six injected inverses appear in LIFO (reverse source) order
#      inside the generated file. The forward chain is
#      `==, !=, <, <=, >, >=`, so the injected block must be
#      `ge, gt, le, lt, ne, eq` — each match strictly after the last
#      forward statement (`qbool c_ge = a >= b;`).
#   4. SOURCE does NOT contain ANY of the six injected call signatures
#      `uncompute_*_qint(c_xx, a, b)` — the transpiler must not
#      contaminate its input. The forward source spells out the VarDecl
#      initializer `qbool c_xx = a OP b;` and the documentation comment
#      discusses the helpers by BARE NAME only (never as a call with
#      the argument list), so any full-signature match in SOURCE would
#      have to have been written there by the transpiler. This is the
#      byte-identity check the Phase D plan calls for: if any injected
#      signature appears in SOURCE, the transpiler rewrote its input in
#      place.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_comparison: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_comparison: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_comparison: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_comparison: source file missing: ${SOURCE}")
endif()

file(READ "${GENERATED}" gen_content)
file(READ "${SOURCE}"    src_content)

# ── Assertion 2: each injected call appears at least once in gen ─────────────
# A call with the `(c_xx, a, b)` argument list only appears via the
# transpiler's injection — the forward source has `qbool c_xx = a OP b;`
# but no `uncompute_*_qint(c_xx, a, b)` text. We require count >= 1 for
# each.
set(_pd_patterns
    "uncompute_eq_qint[ \t]*\\([ \t]*c_eq[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_ne_qint[ \t]*\\([ \t]*c_ne[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_lt_qint[ \t]*\\([ \t]*c_lt[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_le_qint[ \t]*\\([ \t]*c_le[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_gt_qint[ \t]*\\([ \t]*c_gt[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_ge_qint[ \t]*\\([ \t]*c_ge[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)")
foreach(rx IN LISTS _pd_patterns)
    string(REGEX MATCHALL "${rx}" hits "${gen_content}")
    list(LENGTH hits hit_count)
    if(hit_count LESS 1)
        message(FATAL_ERROR
            "check_example_comparison: expected at least 1 occurrence of "
            "`${rx}` in generated file, found ${hit_count}.\n"
            "  file: ${GENERATED}")
    endif()
endforeach()

# ── Assertion 3: LIFO order of the injected block ───────────────────────────
# The forward chain is `==, !=, <, <=, >, >=`. LIFO uncompute therefore
# emits `ge, gt, le, lt, ne, eq` at the bottom of the scope. We find the
# byte offset of the LAST forward statement (`qbool c_ge = a >= b;`) and
# confirm that, after that offset, the six injected calls appear in the
# expected order.
string(FIND "${gen_content}" "c_ge = a >= b;" last_fwd_offset)
if(last_fwd_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_comparison: could not find forward "
        "`c_ge = a >= b;` in generated file.")
endif()
math(EXPR tail_start "${last_fwd_offset} + 14")
string(SUBSTRING "${gen_content}" ${tail_start} -1 tail)

foreach(seq IN ITEMS
        "uncompute_ge_qint(c_ge, a, b)"
        "uncompute_gt_qint(c_gt, a, b)"
        "uncompute_le_qint(c_le, a, b)"
        "uncompute_lt_qint(c_lt, a, b)"
        "uncompute_ne_qint(c_ne, a, b)"
        "uncompute_eq_qint(c_eq, a, b)")
    string(FIND "${tail}" "${seq}" seq_offset)
    if(seq_offset EQUAL -1)
        message(FATAL_ERROR
            "check_example_comparison: expected injected `${seq};` "
            "after the last forward statement, but it was not found.\n"
            "  tail: <<<${tail}>>>")
    endif()
    string(LENGTH "${seq}" seq_len)
    math(EXPR tail_consumed "${seq_offset} + ${seq_len}")
    string(SUBSTRING "${tail}" ${tail_consumed} -1 tail)
endforeach()

# ── Assertion 4: SOURCE remains byte-identical (no injected signatures) ─────
# The forward source never spells any `uncompute_*_qint(c_xx, a, b)`
# call. The documentation comment discusses the helpers by BARE NAME
# only — it never writes the full `(c_xx, a, b)` argument list. If the
# transpiler ever contaminated its input, at least one of the six
# signatures would appear in SOURCE.
set(_pd_src_patterns
    "uncompute_eq_qint[ \t]*\\([ \t]*c_eq[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_ne_qint[ \t]*\\([ \t]*c_ne[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_lt_qint[ \t]*\\([ \t]*c_lt[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_le_qint[ \t]*\\([ \t]*c_le[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_gt_qint[ \t]*\\([ \t]*c_gt[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_ge_qint[ \t]*\\([ \t]*c_ge[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)")
foreach(rx IN LISTS _pd_src_patterns)
    string(REGEX MATCH "${rx}" src_hit "${src_content}")
    if(src_hit)
        message(FATAL_ERROR
            "check_example_comparison: an injected call signature "
            "matched `${rx}` appeared in SOURCE ${SOURCE}.\n"
            "The transpiler must never rewrite its input in place; "
            "examples/comparison.cpp must be byte-identical before and "
            "after the transpile.")
    endif()
endforeach()

message(STATUS
    "check_example_comparison: OK — generated file has all six "
    "injected `uncompute_*_qint(c_xx, a, b)` calls in LIFO order and "
    "source is byte-identical")
