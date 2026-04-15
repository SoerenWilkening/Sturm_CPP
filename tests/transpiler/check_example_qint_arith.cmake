# check_example_qint_arith.cmake — Phase C example-observability ctest.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — the source example examples/qint_arith.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            qint_arith.cpp
#
# Contract:
#   After a clean build that routes examples/qint_arith.cpp through
#   sturm-transpile, the generated file must contain the five injected
#   `uncompute_*_qint(a, b);` inverses (one per forward compound-assign
#   in PC-1..PC-5), in LIFO order after the last forward statement,
#   AND the source file must be untouched (the transpiler never rewrites
#   its input in place — it only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains each of the five injected inverse call texts
#      at least once:
#        uncompute_add_qint(a, b)
#        uncompute_sub_qint(a, b)
#        uncompute_mul_qint(a, b)
#        uncompute_div_qint(a, b)
#        uncompute_mod_qint(a, b)
#      The regex tolerates internal whitespace.
#   3. The five injected inverses appear in LIFO (reverse source) order
#      inside the generated file. The forward chain is
#      `+=, -=, *=, /=, %=`, so the injected block must be
#      `mod, div, mul, sub, add` — each match strictly after the last
#      forward statement (`a %= b;`).
#   4. SOURCE does NOT contain the injected call text
#      `uncompute_add_qint(a, b)` — the transpiler must not contaminate
#      its input. The check mirrors the generated-file regex for the
#      add-qint signature (same whitespace tolerance). The comment in
#      the source discusses the helpers by name, but not as a call with
#      the `(a, b)` argument list, so this probe is specific to an
#      injected statement, not to the documentation.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_qint_arith: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_qint_arith: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_qint_arith: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_qint_arith: source file missing: ${SOURCE}")
endif()

file(READ "${GENERATED}" gen_content)
file(READ "${SOURCE}"    src_content)

# ── Assertion 2: each injected call appears at least once in gen ─────────────
# A call with the `(a, b)` argument list only appears via the transpiler's
# injection — the forward source has `a += b; a -= b; ...` but no
# `uncompute_*_qint(a, b)` text. We require count >= 1 for each.
set(_pc_patterns
    "uncompute_add_qint[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_sub_qint[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_mul_qint[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_div_qint[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_mod_qint[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*\\)")
foreach(rx IN LISTS _pc_patterns)
    string(REGEX MATCHALL "${rx}" hits "${gen_content}")
    list(LENGTH hits hit_count)
    if(hit_count LESS 1)
        message(FATAL_ERROR
            "check_example_qint_arith: expected at least 1 occurrence of "
            "`${rx}` in generated file, found ${hit_count}.\n"
            "  file: ${GENERATED}")
    endif()
endforeach()

# ── Assertion 3: LIFO order of the injected block ───────────────────────────
# The forward chain is `+=, -=, *=, /=, %=`. LIFO uncompute therefore emits
# `mod, div, mul, sub, add` at the bottom of the scope. We find the byte
# offset of the LAST forward statement (`a %= b;`) and confirm that, after
# that offset, the five injected calls appear in the expected order.
string(FIND "${gen_content}" "a %= b;" last_fwd_offset)
if(last_fwd_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_qint_arith: could not find forward `a %= b;` "
        "in generated file.")
endif()
math(EXPR tail_start "${last_fwd_offset} + 7")
string(SUBSTRING "${gen_content}" ${tail_start} -1 tail)

foreach(seq IN ITEMS
        "uncompute_mod_qint(a, b)"
        "uncompute_div_qint(a, b)"
        "uncompute_mul_qint(a, b)"
        "uncompute_sub_qint(a, b)"
        "uncompute_add_qint(a, b)")
    string(FIND "${tail}" "${seq}" seq_offset)
    if(seq_offset EQUAL -1)
        message(FATAL_ERROR
            "check_example_qint_arith: expected injected `${seq};` "
            "after the last forward statement, but it was not found.\n"
            "  tail: <<<${tail}>>>")
    endif()
    string(LENGTH "${seq}" seq_len)
    math(EXPR tail_consumed "${seq_offset} + ${seq_len}")
    string(SUBSTRING "${tail}" ${tail_consumed} -1 tail)
endforeach()

# ── Assertion 4: SOURCE remains clean ───────────────────────────────────────
# The forward source never spells `uncompute_add_qint(a, b)` as a call; the
# documentation comment discusses the helpers as names only. If the
# transpiler ever contaminated its input, the injected signature would
# appear in SOURCE too.
string(REGEX MATCH
    "uncompute_add_qint[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    src_hit "${src_content}")
if(src_hit)
    message(FATAL_ERROR
        "check_example_qint_arith: the injected signature "
        "`uncompute_add_qint(a, b)` appeared in SOURCE ${SOURCE}.\n"
        "The transpiler must never rewrite its input in place.")
endif()

message(STATUS
    "check_example_qint_arith: OK — generated file has all five "
    "injected `uncompute_*_qint(a, b)` calls in LIFO order and source "
    "is clean")
