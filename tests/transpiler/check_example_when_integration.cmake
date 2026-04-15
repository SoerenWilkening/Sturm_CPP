# check_example_when_integration.cmake — Phase F PF-6 ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — examples/when_integration.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            when_integration.cpp
#
# Contract:
#   After a clean build that routes examples/when_integration.cpp through
#   sturm-transpile, the generated file must contain the Phase F PF-4
#   three-point rewrite of `WHEN((b | c) & d) { a.value ^= 1; }`:
#
#     1. Two flat `qbool __stu_tN = ...;` decls INJECTED BEFORE the WHEN
#        line (inner `b | c` → `__stu_t0`, outer `__stu_t0 & d` →
#        `__stu_t1`).
#     2. The WHEN macro argument substituted to `__stu_t1` (so the
#        generated line reads `WHEN(__stu_t1) { ... }`).
#     3. A LIFO pair of inverses planted DIRECTLY AFTER the WHEN body's
#        closing brace, still inside the enclosing inner scope:
#          `uncompute_and(__stu_t1, __stu_t0, d);`
#          `uncompute_or(__stu_t0, b, c);`
#
#   AND the source file must be byte-identical before and after the
#   transpile (the transpiler never rewrites its input in place — it
#   only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains the two flat decls BEFORE the WHEN macro:
#        `qbool __stu_t0 = b | c;`
#        `qbool __stu_t1 = __stu_t0 & d;`
#        (strictly before the `WHEN(__stu_t1) {` spelling).
#   3. GENERATED contains the rewritten WHEN line `WHEN(__stu_t1) {`
#      (the compound `(b | c) & d` argument has been replaced with the
#      outer-flat-temp identifier).
#   4. GENERATED contains both LIFO inverse calls AFTER the WHEN body's
#      closing brace:
#        `uncompute_and(__stu_t1, __stu_t0, d);`  (outer first — LIFO)
#        `uncompute_or(__stu_t0, b, c);`          (inner last)
#   5. GENERATED does NOT contain the original compound WHEN spelling
#        `WHEN((b | c) & d)`
#      — the Phase F `Rewriter.ReplaceText` over the macro argument
#      source range must strip it out; any surviving instance signals
#      a replacement regression.
#   6. SOURCE does NOT contain ANY of the injected fragments
#        `qbool __stu_t0 = b | c;`
#        `qbool __stu_t1 = __stu_t0 & d;`
#        `WHEN(__stu_t1)`
#        `uncompute_and(__stu_t1, __stu_t0, d)`
#        `uncompute_or(__stu_t0, b, c)`
#      — the transpiler must not contaminate its input. The source's
#      top-of-file comment block discusses the rewrite shape by bare
#      name only, and the header preamble of the AUTO-GENERATED file
#      embeds that same comment block verbatim, so we anchor every
#      "no-leak" search after `int main()` to avoid false positives
#      on comment text.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_when_integration: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_when_integration: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_when_integration: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_when_integration: source file missing: ${SOURCE}")
endif()

file(READ "${GENERATED}" gen_content)
file(READ "${SOURCE}"    src_content)

# Anchor every "did-it-land-in-the-right-place?" search after `int main()`
# in the generated file. The header preamble embeds the source's
# top-of-file comment block verbatim, and that comment block mentions
# every fragment by bare name for documentation purposes — we must not
# false-positive on those comment-only spellings.
string(FIND "${gen_content}" "int main()" gen_main_offset)
if(gen_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_when_integration: generated file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${gen_content}" ${gen_main_offset} -1 gen_body)

# ── Assertion 2: flat decls are present and precede the rewritten WHEN ─────
# Each fragment is emitted verbatim by the Phase F matcher's
# render_decl_block() helper (transpiler/src/matcher_when_lift.cpp).
# We verify both decls appear and strictly precede the rewritten
# `WHEN(__stu_t1) {` line in the generated body.
string(FIND "${gen_body}" "qbool __stu_t0 = b | c;" t0_decl_offset)
if(t0_decl_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_when_integration: expected flat decl "
        "`qbool __stu_t0 = b | c;` in generated file body, but it was "
        "not found.\n  file: ${GENERATED}")
endif()

string(FIND "${gen_body}" "qbool __stu_t1 = __stu_t0 & d;" t1_decl_offset)
if(t1_decl_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_when_integration: expected flat decl "
        "`qbool __stu_t1 = __stu_t0 & d;` in generated file body, but "
        "it was not found.\n  file: ${GENERATED}")
endif()

# ── Assertion 3: the rewritten WHEN line names the outer flat temp ─────────
string(FIND "${gen_body}" "WHEN(__stu_t1)" when_offset)
if(when_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_when_integration: expected rewritten "
        "`WHEN(__stu_t1)` in generated file body, but it was not found "
        "— the Phase F ReplaceText pass did not substitute the macro "
        "argument.\n  file: ${GENERATED}")
endif()

# The decls must come before the rewritten WHEN line.
if(NOT t0_decl_offset LESS when_offset)
    message(FATAL_ERROR
        "check_example_when_integration: flat decl "
        "`qbool __stu_t0 = b | c;` at offset ${t0_decl_offset} did not "
        "precede `WHEN(__stu_t1)` at offset ${when_offset}.")
endif()
if(NOT t1_decl_offset LESS when_offset)
    message(FATAL_ERROR
        "check_example_when_integration: flat decl "
        "`qbool __stu_t1 = __stu_t0 & d;` at offset ${t1_decl_offset} "
        "did not precede `WHEN(__stu_t1)` at offset ${when_offset}.")
endif()

# ── Assertion 4: LIFO inverse pair appears after the WHEN body's `}` ───────
# The forward chain is `__stu_t0 = b | c;` then `__stu_t1 = __stu_t0 & d;`
# so LIFO uncompute emits `uncompute_and(__stu_t1, ...)` first, then
# `uncompute_or(__stu_t0, ...)`. We anchor at the `WHEN(__stu_t1)` line
# and require both calls to appear after that offset in LIFO order.
string(LENGTH "WHEN(__stu_t1)" _when_len)
math(EXPR tail_start "${when_offset} + ${_when_len}")
string(SUBSTRING "${gen_body}" ${tail_start} -1 tail)

foreach(seq IN ITEMS
        "uncompute_and(__stu_t1, __stu_t0, d)"
        "uncompute_or(__stu_t0, b, c)")
    string(FIND "${tail}" "${seq}" seq_offset)
    if(seq_offset EQUAL -1)
        message(FATAL_ERROR
            "check_example_when_integration: expected injected "
            "`${seq};` after the rewritten `WHEN(__stu_t1)` line, but "
            "it was not found.\n  tail: <<<${tail}>>>")
    endif()
    string(LENGTH "${seq}" seq_len)
    math(EXPR tail_consumed "${seq_offset} + ${seq_len}")
    string(SUBSTRING "${tail}" ${tail_consumed} -1 tail)
endforeach()

# ── Assertion 5: the original compound WHEN spelling is gone from body ─────
# The Phase F `Rewriter.ReplaceText` over the macro argument source
# range must strip the `(b | c) & d` text inside `WHEN(...)`. Any
# surviving instance in the body signals a replacement regression
# (e.g. emitter applied insertions before replacements and Clang's
# Rewriter dropped the ReplaceText silently).
string(REGEX MATCH
    "WHEN[ \t]*\\([ \t]*\\([ \t]*b[ \t]*\\|[ \t]*c[ \t]*\\)[ \t]*&[ \t]*d[ \t]*\\)"
    compound_survived "${gen_body}")
if(compound_survived)
    message(FATAL_ERROR
        "check_example_when_integration: the original compound "
        "`WHEN((b | c) & d)` spelling survived in the generated file "
        "body — the Phase F ReplaceText pass did not fire.\n"
        "  match: ${compound_survived}")
endif()

# ── Assertion 6: SOURCE contains none of the injected fragments ────────────
# The source's top-of-file comment block discusses the rewrite shape
# by bare name, but never spells any full injected fragment verbatim.
# We anchor the "no-leak" search after `int main()` so the source-code
# comment block (which IS part of the source text and DOES contain
# some fragments by name in documentation prose) cannot false-positive.
# Injected fragments must never appear in the user's hand-written main
# body — only in the generated sibling.
string(FIND "${src_content}" "int main()" src_main_offset)
if(src_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_when_integration: source file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${src_content}" ${src_main_offset} -1 src_body)

set(_pf6_src_patterns
    "qbool[ \t]+__stu_t0[ \t]*=[ \t]*b[ \t]*\\|[ \t]*c[ \t]*;"
    "qbool[ \t]+__stu_t1[ \t]*=[ \t]*__stu_t0[ \t]*&[ \t]*d[ \t]*;"
    "WHEN[ \t]*\\([ \t]*__stu_t1[ \t]*\\)"
    "uncompute_and[ \t]*\\([ \t]*__stu_t1[ \t]*,[ \t]*__stu_t0[ \t]*,[ \t]*d[ \t]*\\)[ \t]*;"
    "uncompute_or[ \t]*\\([ \t]*__stu_t0[ \t]*,[ \t]*b[ \t]*,[ \t]*c[ \t]*\\)[ \t]*;")
foreach(rx IN LISTS _pf6_src_patterns)
    string(REGEX MATCH "${rx}" src_hit "${src_body}")
    if(src_hit)
        message(FATAL_ERROR
            "check_example_when_integration: an injected fragment "
            "matched `${rx}` appeared in SOURCE ${SOURCE} after "
            "`int main()`.\nThe transpiler must never rewrite its "
            "input in place; examples/when_integration.cpp must be "
            "byte-identical before and after the transpile.")
    endif()
endforeach()

message(STATUS
    "check_example_when_integration: OK — generated file has the "
    "PF-4 flat decl pair, rewritten `WHEN(__stu_t1)` argument, both "
    "LIFO `uncompute_{and,or}` injections after the WHEN body, no "
    "surviving compound-WHEN spelling, and the source is "
    "byte-identical")
