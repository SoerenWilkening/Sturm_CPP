# check_example_qint_arith.cmake — Phase C example-observability ctest.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — the source example examples/qint_arith.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            qint_arith.cpp
#
# Contract (post-LO-2, sturm-czfi):
#   After a clean build that routes examples/qint_arith.cpp through
#   sturm-transpile, the generated file must contain:
#     - The two injected reversible inverses (`uncompute_{add,sub}_qint`)
#       for the PC-1 / PC-2 reversible compound-assigns.
#     - The three LO-2 desugar triplets (`mul_oop` + swap; `divide_oop`
#       + swap × 2 for `/=` and `%=`) for the PC-3 / PC-4 / PC-5 lossy
#       compound-assigns; the matching scope-exit cleanup
#       (`*_oop_adj` calls + reverse swaps) must appear in LIFO order
#       at the inner block's close brace.
#   AND the source file must be untouched (the transpiler never rewrites
#   its input in place — it only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains each of the two injected reversible inverse
#      call texts at least once:
#        uncompute_add_qint(a, b)
#        uncompute_sub_qint(a, b)
#      and each of the three LO-2 forward-triplet identifiers:
#        mul_oop(a, b, ...)         from PC-3 (`a *= b;`)
#        divide_oop(a, b, ...)      from PC-4 + PC-5 (twice)
#      and the matching adjoint cleanup identifiers:
#        mul_oop_adj(a, b, ...)
#        divide_oop_adj(a, b, ...)
#      The regex tolerates internal whitespace.
#   3. LIFO order: the two reversible inverses + the three LO-2 cleanup
#      lines all sit AFTER the last forward statement (`a %= b;`), in
#      the documented LIFO order. The Phase C reversible inverses
#      (`uncompute_sub_qint`, `uncompute_add_qint`) come from the LO-2-
#      free PC-1 / PC-2 statements; the LO-2 cleanups come from the
#      lossy PC-3..PC-5 statements. Both groups are ordered against
#      themselves (LIFO).
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
# Two reversible-uncompute call signatures (PC-1 / PC-2) and three LO-2
# OOP forward-triplet identifiers (PC-3 / PC-4 / PC-5) plus the matching
# adjoint cleanups. Source comments mention the helpers by name but never
# as a call with the `(a, b, ...)` argument list, so a count >= 1 each is
# enough to assert "the transpiler injected them".
set(_pc_patterns
    "uncompute_add_qint[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "uncompute_sub_qint[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*\\)"
    "mul_oop[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*,"
    "divide_oop[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*,"
    "mul_oop_adj[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*,"
    "divide_oop_adj[ \t]*\\([ \t]*a[ \t]*,[ \t]*b[ \t]*,")
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

# ── Assertion 3: LIFO order of the injected cleanup block ───────────────────
# The forward chain is `+=, -=, *=, /=, %=`. LIFO uncompute therefore emits,
# at the close brace of the inner scope, in order:
#   1. `uncompute_sub_qint(a, b)` (PC-2 inverse, from Phase C)
#   2. `uncompute_add_qint(a, b)` (PC-1 inverse, from Phase C)
#   3. `divide_oop_adj` (PC-5 cleanup, from LO-2 — mod)
#   4. `divide_oop_adj` (PC-4 cleanup, from LO-2 — div)
#   5. `mul_oop_adj`    (PC-3 cleanup, from LO-2 — mul)
#
# The Phase C inverses come BEFORE the LO-2 cleanups in source order because
# the transpiler's `external_cleanups_` list (populated by LO-2c) is appended
# AFTER the per-op LIFO insertions. That produces a single well-defined
# byte order we can pin here.
#
# We find the byte offset of the LAST forward statement (the LO-2 swap line
# for `%=`, which is `swap(a, __sturm_tmp_mod_2_r);` after desugaring) and
# verify the five expected sequences appear after it in the order above.
string(FIND "${gen_content}" "a %= b;" last_fwd_marker)
if(last_fwd_marker EQUAL -1)
    # LO-2 may have rewritten the literal `a %= b;` away; the swap target
    # is the next-best anchor. The last forward swap for `%=` is unique by
    # its `__sturm_tmp_mod_*_r` ancilla.
    string(REGEX MATCH "swap\\([ \t]*a[ \t]*,[ \t]*__sturm_tmp_mod_[0-9]+_r[ \t]*\\)"
        last_fwd_match "${gen_content}")
    if(NOT last_fwd_match)
        message(FATAL_ERROR
            "check_example_qint_arith: could not find forward `a %= b;` "
            "or its LO-2 swap surrogate in generated file.")
    endif()
    string(FIND "${gen_content}" "${last_fwd_match}" last_fwd_marker)
endif()
math(EXPR tail_start "${last_fwd_marker} + 7")
string(SUBSTRING "${gen_content}" ${tail_start} -1 tail)

foreach(seq IN ITEMS
        "uncompute_sub_qint(a, b)"
        "uncompute_add_qint(a, b)"
        "divide_oop_adj"
        "divide_oop_adj"
        "mul_oop_adj")
    string(FIND "${tail}" "${seq}" seq_offset)
    if(seq_offset EQUAL -1)
        message(FATAL_ERROR
            "check_example_qint_arith: expected cleanup token `${seq}` "
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
