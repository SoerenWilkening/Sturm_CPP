# check_example_nested_when.cmake — Phase G PG-8 ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — examples/nested_when.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            nested_when.cpp
#
# Contract:
#   After a clean build that routes examples/nested_when.cpp through
#   sturm-transpile, the generated file must contain the Phase G PG-2 /
#   PG-3 three-point rewrite for both the two-level and three-level
#   nested-WHEN pairs in main():
#
#     Case 1: `WHEN(a) { WHEN(b) { target.flip(); } }`
#       1. `qbool __stu_ctrl0 = a & b;` INJECTED between the outer
#          `WHEN(a) {` and the rewritten inner `WHEN(__stu_ctrl0) {`
#          line.
#       2. Inner WHEN argument substituted to `__stu_ctrl0` (so the
#          generated line reads `WHEN(__stu_ctrl0) { ... }`).
#       3. `uncompute_and(__stu_ctrl0, a, b);` planted DIRECTLY AFTER
#          the inner WHEN body's closing brace, still inside the outer
#          scope so the temp's destructor fires AFTER its inverse.
#
#     Case 2: `WHEN(x) { WHEN(y) { WHEN(z) { target.flip(); } } }`
#       Pairwise cascade fires twice against a single persistent
#       FreshNameAllocator carried over from Case 1, so the two
#       cascade levels allocate `__stu_ctrl1` (for the (x, y) pair)
#       and `__stu_ctrl2` (for the (y, z) pair):
#       1. `qbool __stu_ctrl1 = x & y;` injected before `WHEN(y)`,
#          `WHEN(y)` rewritten to `WHEN(__stu_ctrl1)`.
#       2. `qbool __stu_ctrl2 = y & z;` injected before `WHEN(z)`,
#          `WHEN(z)` rewritten to `WHEN(__stu_ctrl2)`.
#       3. `uncompute_and(__stu_ctrl2, y, z);` planted after the
#          innermost body's `}`; then
#          `uncompute_and(__stu_ctrl1, x, y);` planted after the
#          middle body's `}` — cascade order (inner-first).
#
#   AND the source file must be byte-identical before and after the
#   transpile (the transpiler never rewrites its input in place — it
#   only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains the Case 1 rewrite:
#        - `qbool __stu_ctrl0 = a & b;` precedes `WHEN(__stu_ctrl0)`
#          and follows `WHEN(a)`.
#        - `WHEN(__stu_ctrl0) {` is present (argument substitution fired).
#        - `uncompute_and(__stu_ctrl0, a, b);` appears AFTER the
#          rewritten inner `WHEN(__stu_ctrl0)` line.
#   3. GENERATED contains the Case 2 cascade rewrite:
#        - `qbool __stu_ctrl1 = x & y;` decl present, precedes
#          `WHEN(__stu_ctrl1)`.
#        - `qbool __stu_ctrl2 = y & z;` decl present, precedes
#          `WHEN(__stu_ctrl2)`.
#        - Both rewritten WHEN lines present.
#        - `uncompute_and(__stu_ctrl2, y, z);` appears before
#          `uncompute_and(__stu_ctrl1, x, y);` in the generated body
#          (cascade / LIFO order — inner uncomputes first).
#   4. GENERATED no longer contains the original inner WHEN spellings
#        `WHEN(b)` / `WHEN(y)` / `WHEN(z)` — the Phase G ReplaceText
#        over the inner `materialize_when` arg's spelling range must
#        strip the original identifier.  The outer-most
#        `WHEN(a)` / `WHEN(x)` lines ARE kept (Phase G only rewrites
#        the inner side of each matched pair).
#   5. SOURCE does NOT contain ANY of the injected fragments after
#      `int main()` — the transpiler must not contaminate its input.
#      We anchor the "no-leak" search after `int main()` to avoid
#      false-positives on the source's top-of-file comment block,
#      which discusses the rewrite shape in prose.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_nested_when: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_nested_when: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_nested_when: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_nested_when: source file missing: ${SOURCE}")
endif()

file(READ "${GENERATED}" gen_content)
file(READ "${SOURCE}"    src_content)

# Anchor every "did-it-land-in-the-right-place?" search after
# `int main()` in the generated file.  The generated file's header
# preamble embeds the source's top-of-file comment block verbatim,
# and that comment block mentions every injected fragment by bare
# name — we must not false-positive on those comment-only spellings.
string(FIND "${gen_content}" "int main()" gen_main_offset)
if(gen_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: generated file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${gen_content}" ${gen_main_offset} -1 gen_body)

# ── Case 1 · Assertion 2a: `qbool __stu_ctrl0 = a & b;` decl present ──────
string(FIND "${gen_body}" "qbool __stu_ctrl0 = a & b;" ctrl0_decl_offset)
if(ctrl0_decl_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: expected injected decl "
        "`qbool __stu_ctrl0 = a & b;` in generated body, but it was "
        "not found.\n  file: ${GENERATED}")
endif()

# ── Case 1 · Assertion 2b: `WHEN(__stu_ctrl0) {` line present ─────────────
string(FIND "${gen_body}" "WHEN(__stu_ctrl0)" when_ctrl0_offset)
if(when_ctrl0_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: expected rewritten "
        "`WHEN(__stu_ctrl0)` in generated body, but it was not "
        "found — the Phase G ReplaceText pass did not substitute the "
        "inner WHEN's argument.\n  file: ${GENERATED}")
endif()

# ── Case 1 · Assertion 2a (cont.): decl precedes rewritten WHEN line ──────
if(NOT ctrl0_decl_offset LESS when_ctrl0_offset)
    message(FATAL_ERROR
        "check_example_nested_when: `qbool __stu_ctrl0 = a & b;` at "
        "offset ${ctrl0_decl_offset} did not precede "
        "`WHEN(__stu_ctrl0)` at offset ${when_ctrl0_offset}.")
endif()

# ── Case 1 · Assertion 2a (cont.): decl follows `WHEN(a) {` ───────────────
string(FIND "${gen_body}" "WHEN(a)" when_a_offset)
if(when_a_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: expected outer `WHEN(a)` spelling "
        "preserved in generated body, but it was not found — Phase G "
        "must NOT rewrite the outer side of a named-named pair.")
endif()
if(NOT when_a_offset LESS ctrl0_decl_offset)
    message(FATAL_ERROR
        "check_example_nested_when: outer `WHEN(a)` at offset "
        "${when_a_offset} did not precede injected decl "
        "`qbool __stu_ctrl0 = a & b;` at offset ${ctrl0_decl_offset}.")
endif()

# ── Case 1 · Assertion 2c: `uncompute_and(__stu_ctrl0, a, b);` AFTER inner ─
string(LENGTH "WHEN(__stu_ctrl0)" _when_ctrl0_len)
math(EXPR case1_tail_start "${when_ctrl0_offset} + ${_when_ctrl0_len}")
string(SUBSTRING "${gen_body}" ${case1_tail_start} -1 case1_tail)

string(FIND "${case1_tail}" "uncompute_and(__stu_ctrl0, a, b)"
       uncompute_ctrl0_offset)
if(uncompute_ctrl0_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: expected injected "
        "`uncompute_and(__stu_ctrl0, a, b);` AFTER the rewritten "
        "`WHEN(__stu_ctrl0)` line, but it was not found.\n"
        "  tail: <<<${case1_tail}>>>")
endif()

# ── Case 2 · Assertion 3a: cascade decls present ──────────────────────────
string(FIND "${gen_body}" "qbool __stu_ctrl1 = x & y;" ctrl1_decl_offset)
if(ctrl1_decl_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: expected injected decl "
        "`qbool __stu_ctrl1 = x & y;` (outer cascade pair) in "
        "generated body, but it was not found.\n  file: ${GENERATED}")
endif()

string(FIND "${gen_body}" "qbool __stu_ctrl2 = y & z;" ctrl2_decl_offset)
if(ctrl2_decl_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: expected injected decl "
        "`qbool __stu_ctrl2 = y & z;` (inner cascade pair) in "
        "generated body, but it was not found.\n  file: ${GENERATED}")
endif()

# ── Case 2 · Assertion 3b: rewritten WHEN lines present and ordered ───────
string(FIND "${gen_body}" "WHEN(__stu_ctrl1)" when_ctrl1_offset)
if(when_ctrl1_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: expected rewritten "
        "`WHEN(__stu_ctrl1)` in generated body, but it was not "
        "found — the Phase G cascade first-pair ReplaceText did not "
        "fire.\n  file: ${GENERATED}")
endif()
string(FIND "${gen_body}" "WHEN(__stu_ctrl2)" when_ctrl2_offset)
if(when_ctrl2_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: expected rewritten "
        "`WHEN(__stu_ctrl2)` in generated body, but it was not "
        "found — the Phase G cascade second-pair ReplaceText did not "
        "fire.\n  file: ${GENERATED}")
endif()

# Outer cascade decl must precede its rewritten WHEN line.
if(NOT ctrl1_decl_offset LESS when_ctrl1_offset)
    message(FATAL_ERROR
        "check_example_nested_when: `qbool __stu_ctrl1 = x & y;` at "
        "offset ${ctrl1_decl_offset} did not precede "
        "`WHEN(__stu_ctrl1)` at offset ${when_ctrl1_offset}.")
endif()
# Inner cascade decl must precede its rewritten WHEN line.
if(NOT ctrl2_decl_offset LESS when_ctrl2_offset)
    message(FATAL_ERROR
        "check_example_nested_when: `qbool __stu_ctrl2 = y & z;` at "
        "offset ${ctrl2_decl_offset} did not precede "
        "`WHEN(__stu_ctrl2)` at offset ${when_ctrl2_offset}.")
endif()

# Outer `WHEN(x)` stays untouched (Phase G only rewrites the inner
# side of each pair; the very outermost WHEN is never the inner side).
string(FIND "${gen_body}" "WHEN(x)" when_x_offset)
if(when_x_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: expected outer `WHEN(x)` spelling "
        "preserved in generated body, but it was not found — Phase G "
        "must NOT rewrite the outer-most side of a cascade.")
endif()

# ── Case 2 · Assertion 3c: cascade uncomputes present, LIFO order ─────────
# Anchor search after `WHEN(__stu_ctrl2)` (the innermost rewritten
# line): the two uncomputes must appear AFTER that point in cascade
# order — inner ctrl2 first (immediately after innermost `}`), then
# outer ctrl1 (after middle `}`).
string(LENGTH "WHEN(__stu_ctrl2)" _when_ctrl2_len)
math(EXPR case2_tail_start "${when_ctrl2_offset} + ${_when_ctrl2_len}")
string(SUBSTRING "${gen_body}" ${case2_tail_start} -1 case2_tail)

string(FIND "${case2_tail}" "uncompute_and(__stu_ctrl2, y, z)"
       uncompute_ctrl2_offset)
if(uncompute_ctrl2_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: expected injected "
        "`uncompute_and(__stu_ctrl2, y, z);` AFTER the innermost "
        "`WHEN(__stu_ctrl2)` line, but it was not found.\n"
        "  tail: <<<${case2_tail}>>>")
endif()
string(FIND "${case2_tail}" "uncompute_and(__stu_ctrl1, x, y)"
       uncompute_ctrl1_offset)
if(uncompute_ctrl1_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: expected injected "
        "`uncompute_and(__stu_ctrl1, x, y);` AFTER the innermost "
        "`WHEN(__stu_ctrl2)` line, but it was not found.\n"
        "  tail: <<<${case2_tail}>>>")
endif()
# Inner (ctrl2) uncompute must precede outer (ctrl1) uncompute — LIFO
# cascade order.
if(NOT uncompute_ctrl2_offset LESS uncompute_ctrl1_offset)
    message(FATAL_ERROR
        "check_example_nested_when: cascade uncomputes out of order. "
        "Expected `uncompute_and(__stu_ctrl2, y, z);` (offset "
        "${uncompute_ctrl2_offset}) to precede "
        "`uncompute_and(__stu_ctrl1, x, y);` (offset "
        "${uncompute_ctrl1_offset}) — LIFO/cascade requires inner "
        "first.")
endif()

# ── Assertion 4: original inner WHEN spellings are gone from body ─────────
# The Phase G `Rewriter.ReplaceText` over each inner
# `materialize_when` arg's spelling range must strip the original
# identifier.  The outer-most `WHEN(a)` / `WHEN(x)` lines were
# already checked above as preserved (Phase G only rewrites the
# inner side of each matched pair).  Any surviving inner spelling
# signals a replacement regression.
foreach(stale_inner IN ITEMS
        "WHEN(b)"
        "WHEN(y)"
        "WHEN(z)")
    string(FIND "${gen_body}" "${stale_inner}" stale_offset)
    if(NOT stale_offset EQUAL -1)
        message(FATAL_ERROR
            "check_example_nested_when: original inner WHEN spelling "
            "`${stale_inner}` survived in the generated file body at "
            "offset ${stale_offset} — the Phase G ReplaceText pass did "
            "not fire for that pair.")
    endif()
endforeach()

# ── Assertion 5: SOURCE contains none of the injected fragments ───────────
# The source's top-of-file comment block mentions `__stu_ctrl0` /
# `uncompute_and` by bare name in prose, but the full injected
# fragment spellings below never appear outside the generated
# sibling.  We anchor the "no-leak" search after `int main()` so the
# documentation prose cannot false-positive.  The transpiler must
# never rewrite its input in place; examples/nested_when.cpp must be
# byte-identical before and after the transpile.
string(FIND "${src_content}" "int main()" src_main_offset)
if(src_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_nested_when: source file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${src_content}" ${src_main_offset} -1 src_body)

set(_pg8_src_patterns
    "qbool[ \t]+__stu_ctrl0[ \t]*=[ \t]*a[ \t]*&[ \t]*b[ \t]*;"
    "qbool[ \t]+__stu_ctrl1[ \t]*=[ \t]*x[ \t]*&[ \t]*y[ \t]*;"
    "qbool[ \t]+__stu_ctrl2[ \t]*=[ \t]*y[ \t]*&[ \t]*z[ \t]*;"
    "WHEN[ \t]*\\([ \t]*__stu_ctrl0[ \t]*\\)"
    "WHEN[ \t]*\\([ \t]*__stu_ctrl1[ \t]*\\)"
    "WHEN[ \t]*\\([ \t]*__stu_ctrl2[ \t]*\\)"
    "uncompute_and[ \t]*\\([ \t]*__stu_ctrl0[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)[ \t]*;"
    "uncompute_and[ \t]*\\([ \t]*__stu_ctrl1[ \t]*,[ \t]*x[ \t]*,[ \t]*y[ \t]*\\)[ \t]*;"
    "uncompute_and[ \t]*\\([ \t]*__stu_ctrl2[ \t]*,[ \t]*y[ \t]*,[ \t]*z[ \t]*\\)[ \t]*;")
foreach(rx IN LISTS _pg8_src_patterns)
    string(REGEX MATCH "${rx}" src_hit "${src_body}")
    if(src_hit)
        message(FATAL_ERROR
            "check_example_nested_when: an injected fragment matched "
            "`${rx}` appeared in SOURCE ${SOURCE} after "
            "`int main()`.\nThe transpiler must never rewrite its "
            "input in place; examples/nested_when.cpp must be "
            "byte-identical before and after the transpile.")
    endif()
endforeach()

message(STATUS
    "check_example_nested_when: OK — generated file has the PG-2 "
    "two-level rewrite (`__stu_ctrl0 = a & b` + `WHEN(__stu_ctrl0)` + "
    "`uncompute_and(__stu_ctrl0, a, b)`), the PG-3 three-level "
    "cascade (`__stu_ctrl1`, `__stu_ctrl2` with LIFO uncomputes), "
    "no surviving inner-WHEN spellings, and the source is "
    "byte-identical")
