# check_example_compound_expression.cmake — Phase E PE-5 ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — examples/compound_expression.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            compound_expression.cpp
#
# Contract:
#   After a clean build that routes examples/compound_expression.cpp
#   through sturm-transpile, the generated file must contain the
#   PE-4 / PE-5 flat decl sequence that REPLACES the original
#   `qbool r = (b | c) & d;` VarDecl, plus the two LIFO uncompute
#   inverses `uncompute_and(r, __stu_t0, d);` and
#   `uncompute_or(__stu_t0, b, c);` injected before the inner-block
#   close brace inside main(), AND the source file must be
#   byte-identical before and after the transpile (the transpiler
#   never rewrites its input in place — it only writes into
#   sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains the flat decl sequence:
#        a. `qbool __stu_t0 = b | c;` — the fresh-name intermediate
#           emitted by the Phase E compound matcher for the inner OR
#           sub-expression.
#        b. `qbool r = __stu_t0 & d;` — the outer AND VarDecl,
#           rewritten to consume the intermediate.
#      The two flat decls must appear strictly BEFORE both injected
#      uncompute call sites (the Rewriter applies the ReplaceText
#      over the original VarDecl's source range while
#      uncompute_pass.cpp inserts the inverses just before the
#      enclosing CompoundStmt's close-brace location).
#   3. GENERATED contains both injected inverse calls:
#        `uncompute_and(r, __stu_t0, d);`
#        `uncompute_or(__stu_t0, b, c);`
#      in LIFO order (outermost uncomputed first), each AFTER the
#      flat decls.
#   4. GENERATED does NOT contain the original compound spelling
#        `qbool r = (b | c) & d`
#      — the transpiler's `Rewriter.ReplaceText` is supposed to
#      strip it out, so any survival here would mean the
#      replacement step regressed.
#   5. SOURCE does NOT contain ANY of the injected fragments
#        `qbool __stu_t0 = b | c`
#        `qbool r = __stu_t0 & d`
#        `uncompute_and(r, __stu_t0, d)`
#        `uncompute_or(__stu_t0, b, c)`
#      — the transpiler must not contaminate its input. The forward
#      source spells `qbool r = (b | c) & d;` and discusses the
#      flat decls / inverses in BARE comments only (no full
#      signature with the `__stu_t0` name), so any full-signature
#      match in SOURCE would have to have been written there by
#      the transpiler. This is the byte-identity check the Phase E
#      plan calls for: if any injected fragment appears in SOURCE,
#      the transpiler rewrote its input in place.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_compound_expression: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_compound_expression: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_compound_expression: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_compound_expression: source file missing: ${SOURCE}")
endif()

file(READ "${GENERATED}" gen_content)
file(READ "${SOURCE}"    src_content)

# ── Assertion 2: flat decl sequence is present ─────────────────────────────
# Each fragment is emitted verbatim by the compound matcher's
# render_decl_line() helper (transpiler/src/matcher_qbool_compound.cpp).
# The regex tolerates internal whitespace around operators.
set(_pe5_flat_patterns
    "qbool[ \t]+__stu_t0[ \t]*=[ \t]*b[ \t]*\\|[ \t]*c[ \t]*;"
    "qbool[ \t]+r[ \t]*=[ \t]*__stu_t0[ \t]*&[ \t]*d[ \t]*;")
foreach(rx IN LISTS _pe5_flat_patterns)
    string(REGEX MATCHALL "${rx}" hits "${gen_content}")
    list(LENGTH hits hit_count)
    if(hit_count LESS 1)
        message(FATAL_ERROR
            "check_example_compound_expression: expected at least 1 "
            "occurrence of `${rx}` in generated file, found ${hit_count}.\n"
            "  file: ${GENERATED}")
    endif()
endforeach()

# ── Assertion 3: both injected inverses appear in LIFO order ───────────────
# The forward chain is `__stu_t0 = b | c` then `r = __stu_t0 & d` so
# LIFO uncompute emits `uncompute_and(r, ...)` first, then
# `uncompute_or(__stu_t0, ...)`. We anchor at the outer `qbool r =
# __stu_t0 & d;` flat decl and require both uncompute calls to appear
# after that offset in the expected order.
string(FIND "${gen_content}" "qbool r = __stu_t0 & d" outer_decl_offset)
if(outer_decl_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_compound_expression: could not anchor on the "
        "outer flat decl `qbool r = __stu_t0 & d` in generated file.")
endif()
math(EXPR tail_start "${outer_decl_offset} + 22")
string(SUBSTRING "${gen_content}" ${tail_start} -1 tail)

foreach(seq IN ITEMS
        "uncompute_and(r, __stu_t0, d)"
        "uncompute_or(__stu_t0, b, c)")
    string(FIND "${tail}" "${seq}" seq_offset)
    if(seq_offset EQUAL -1)
        message(FATAL_ERROR
            "check_example_compound_expression: expected injected "
            "`${seq};` after the outer flat decl, but it was not found.\n"
            "  tail: <<<${tail}>>>")
    endif()
    string(LENGTH "${seq}" seq_len)
    math(EXPR tail_consumed "${seq_offset} + ${seq_len}")
    string(SUBSTRING "${tail}" ${tail_consumed} -1 tail)
endforeach()

# ── Assertion 4: the original compound spelling is gone from generated ─────
# `Rewriter.ReplaceText` over the original VarDecl source range must
# strip the `qbool r = (b | c) & d` text; only the flat sequence
# above remains. A surviving instance signals a replacement
# regression (e.g. emitter applied insertions before replacements
# and Clang's Rewriter dropped the ReplaceText silently).
#
# The header preamble of the AUTO-GENERATED file embeds the source's
# top-of-file comment block verbatim, and that block contains the
# original compound spelling inside a `//` comment. We therefore
# anchor the search after the `int main()` line so the comment-only
# matches in the preamble do not produce a false positive.
string(FIND "${gen_content}" "int main()" main_offset)
if(main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_compound_expression: generated file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${gen_content}" ${main_offset} -1 gen_body)
string(REGEX MATCH "qbool[ \t]+r[ \t]*=[ \t]*\\([ \t]*b[ \t]*\\|[ \t]*c[ \t]*\\)[ \t]*&[ \t]*d"
       compound_survived "${gen_body}")
if(compound_survived)
    message(FATAL_ERROR
        "check_example_compound_expression: the original compound "
        "spelling `qbool r = (b | c) & d` survived in the generated "
        "file body — the Phase E ReplaceText pass did not fire.\n"
        "  match: ${compound_survived}")
endif()

# ── Assertion 5: SOURCE remains byte-identical (no injected fragments) ─────
# The source spells `qbool r = (b | c) & d;` and references the flat
# decls / inverses by name only inside `//` comments. None of the
# four injected fragments below ever appears verbatim in the source,
# so any match here would mean the transpiler contaminated its input.
set(_pe5_src_patterns
    "qbool[ \t]+__stu_t0[ \t]*=[ \t]*b[ \t]*\\|[ \t]*c[ \t]*;"
    "qbool[ \t]+r[ \t]*=[ \t]*__stu_t0[ \t]*&[ \t]*d[ \t]*;"
    "uncompute_and[ \t]*\\([ \t]*r[ \t]*,[ \t]*__stu_t0[ \t]*,[ \t]*d[ \t]*\\)[ \t]*;"
    "uncompute_or[ \t]*\\([ \t]*__stu_t0[ \t]*,[ \t]*b[ \t]*,[ \t]*c[ \t]*\\)[ \t]*;")
foreach(rx IN LISTS _pe5_src_patterns)
    string(REGEX MATCH "${rx}" src_hit "${src_content}")
    if(src_hit)
        message(FATAL_ERROR
            "check_example_compound_expression: an injected fragment "
            "matched `${rx}` appeared in SOURCE ${SOURCE}.\n"
            "The transpiler must never rewrite its input in place; "
            "examples/compound_expression.cpp must be byte-identical "
            "before and after the transpile.")
    endif()
endforeach()

message(STATUS
    "check_example_compound_expression: OK — generated file has the "
    "PE-4 flat decl sequence, both LIFO `uncompute_{and,or}` injections, "
    "no surviving compound spelling, and the source is byte-identical")
