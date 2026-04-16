# check_example_control_flow.cmake — Phase H PH-5 ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — examples/control_flow.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            control_flow.cpp
#
# Contract:
#   After a clean build that routes examples/control_flow.cpp through
#   sturm-transpile, the generated file must contain the Phase H
#   per-iteration / per-branch uncompute lowering for both the
#   for-loop and the if/else scenario in main():
#
#     Case 1: `for (int i = 0; i < 3; ++i) { qbool tmp = a | b; }`
#       The MVP OR matcher + PH-1 enclosing_scope refactor key a QScope
#       on the for-body's CompoundStmt, and the M8 uncompute-synthesis
#       pass plants `uncompute_or(tmp, a, b);` before the loop body's
#       closing `}` — once per iteration at run time.
#
#     Case 2: `if (cond) { qbool x = a | b; } else { qbool y = a | b; }`
#       Distinct intermediates per branch.  The then-arm and else-arm
#       each get their own QScope keyed on their respective
#       CompoundStmt, so the M8 pass plants
#       `uncompute_or(x, a, b);` before the then-arm's `}` and
#       `uncompute_or(y, a, b);` before the else-arm's `}`.
#
#   AND the source file must be byte-identical before and after the
#   transpile (the transpiler never rewrites its input in place — it
#   only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains the Case 1 rewrite:
#        - `qbool tmp = a | b;` preserved inside the for-body.
#        - `uncompute_or(tmp, a, b);` appears AFTER the forward
#          `qbool tmp = a | b;` decl inside the for-loop body.
#   3. GENERATED contains the Case 2 rewrite:
#        - `qbool x = a | b;` preserved inside the then-arm.
#        - `qbool y = a | b;` preserved inside the else-arm.
#        - `uncompute_or(x, a, b);` appears AFTER the then-arm's
#          forward decl.
#        - `uncompute_or(y, a, b);` appears AFTER the else-arm's
#          forward decl.
#        - The then-arm's `uncompute_or(x, ...)` precedes the
#          else-arm's `uncompute_or(y, ...)` in the generated body
#          (source / LIFO order).
#   4. SOURCE does NOT contain ANY of the injected fragments after
#      `int main()` — the transpiler must not contaminate its input.
#      We anchor the "no-leak" search after `int main()` to avoid
#      false-positives on the source's top-of-file comment block,
#      which discusses the rewrite shape in prose.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_control_flow: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_control_flow: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_control_flow: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_control_flow: source file missing: ${SOURCE}")
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
        "check_example_control_flow: generated file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${gen_content}" ${gen_main_offset} -1 gen_body)

# ── Case 1 · Assertion 2a: forward for-body decl present ──────────────────
string(FIND "${gen_body}" "qbool tmp = a | b;" tmp_decl_offset)
if(tmp_decl_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_control_flow: expected forward "
        "`qbool tmp = a | b;` inside the for-body, but it was not "
        "found.\n  file: ${GENERATED}")
endif()

# ── Case 1 · Assertion 2b: per-iteration uncompute injected AFTER decl ────
string(LENGTH "qbool tmp = a | b;" _tmp_decl_len)
math(EXPR case1_tail_start "${tmp_decl_offset} + ${_tmp_decl_len}")
string(SUBSTRING "${gen_body}" ${case1_tail_start} -1 case1_tail)

string(FIND "${case1_tail}" "uncompute_or(tmp, a, b)"
       uncompute_tmp_offset)
if(uncompute_tmp_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_control_flow: expected injected "
        "`uncompute_or(tmp, a, b);` AFTER the forward "
        "`qbool tmp = a | b;` decl inside the for-body, but it was "
        "not found.\n  tail: <<<${case1_tail}>>>")
endif()

# ── Case 2 · Assertion 3a: then-arm forward decl present ──────────────────
string(FIND "${gen_body}" "qbool x = a | b;" x_decl_offset)
if(x_decl_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_control_flow: expected forward "
        "`qbool x = a | b;` inside the then-arm, but it was not "
        "found.\n  file: ${GENERATED}")
endif()

# ── Case 2 · Assertion 3b: else-arm forward decl present ──────────────────
string(FIND "${gen_body}" "qbool y = a | b;" y_decl_offset)
if(y_decl_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_control_flow: expected forward "
        "`qbool y = a | b;` inside the else-arm, but it was not "
        "found.\n  file: ${GENERATED}")
endif()

# ── Case 2 · Assertion 3c: then-arm uncompute injected AFTER x decl ───────
string(LENGTH "qbool x = a | b;" _x_decl_len)
math(EXPR then_tail_start "${x_decl_offset} + ${_x_decl_len}")
string(SUBSTRING "${gen_body}" ${then_tail_start} -1 then_tail)

string(FIND "${then_tail}" "uncompute_or(x, a, b)"
       uncompute_x_offset)
if(uncompute_x_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_control_flow: expected injected "
        "`uncompute_or(x, a, b);` AFTER the forward "
        "`qbool x = a | b;` decl inside the then-arm, but it was "
        "not found.\n  tail: <<<${then_tail}>>>")
endif()

# ── Case 2 · Assertion 3d: else-arm uncompute injected AFTER y decl ───────
string(LENGTH "qbool y = a | b;" _y_decl_len)
math(EXPR else_tail_start "${y_decl_offset} + ${_y_decl_len}")
string(SUBSTRING "${gen_body}" ${else_tail_start} -1 else_tail)

string(FIND "${else_tail}" "uncompute_or(y, a, b)"
       uncompute_y_offset)
if(uncompute_y_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_control_flow: expected injected "
        "`uncompute_or(y, a, b);` AFTER the forward "
        "`qbool y = a | b;` decl inside the else-arm, but it was "
        "not found.\n  tail: <<<${else_tail}>>>")
endif()

# ── Case 2 · Assertion 3e: then-arm uncompute precedes else-arm uncompute ─
# Recompute ABSOLUTE offsets of the two uncomputes inside gen_body so
# we can order them source-wise.  The x-decl comes before the
# y-decl in source, so the then-arm uncompute (planted before the
# then-arm's `}`) must appear before the else-arm uncompute
# (planted before the else-arm's `}`).
string(FIND "${gen_body}" "uncompute_or(x, a, b)" abs_uncompute_x_offset)
string(FIND "${gen_body}" "uncompute_or(y, a, b)" abs_uncompute_y_offset)
if(abs_uncompute_x_offset EQUAL -1 OR abs_uncompute_y_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_control_flow: absolute-offset lookup failed "
        "for one of the per-branch uncomputes — this should have been "
        "caught by assertion 3c/3d above.")
endif()
if(NOT abs_uncompute_x_offset LESS abs_uncompute_y_offset)
    message(FATAL_ERROR
        "check_example_control_flow: per-branch uncomputes out of "
        "order. Expected `uncompute_or(x, a, b);` (offset "
        "${abs_uncompute_x_offset}) to precede "
        "`uncompute_or(y, a, b);` (offset ${abs_uncompute_y_offset}) "
        "— the then-arm is the first branch in source order, so its "
        "uncompute lands first in the generated body.")
endif()

# ── Assertion 4: SOURCE contains none of the injected fragments ───────────
# The source's top-of-file comment block mentions `uncompute_or` by
# bare name in prose, but the full injected fragment spellings below
# never appear outside the generated sibling.  We anchor the
# "no-leak" search after `int main()` so the documentation prose
# cannot false-positive.  The transpiler must never rewrite its
# input in place; examples/control_flow.cpp must be byte-identical
# before and after the transpile.
string(FIND "${src_content}" "int main()" src_main_offset)
if(src_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_control_flow: source file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${src_content}" ${src_main_offset} -1 src_body)

set(_ph5_src_patterns
    "uncompute_or[ \t]*\\([ \t]*tmp[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)[ \t]*;"
    "uncompute_or[ \t]*\\([ \t]*x[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)[ \t]*;"
    "uncompute_or[ \t]*\\([ \t]*y[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)[ \t]*;")
foreach(rx IN LISTS _ph5_src_patterns)
    string(REGEX MATCH "${rx}" src_hit "${src_body}")
    if(src_hit)
        message(FATAL_ERROR
            "check_example_control_flow: an injected fragment matched "
            "`${rx}` appeared in SOURCE ${SOURCE} after "
            "`int main()`.\nThe transpiler must never rewrite its "
            "input in place; examples/control_flow.cpp must be "
            "byte-identical before and after the transpile.")
    endif()
endforeach()

message(STATUS
    "check_example_control_flow: OK — generated file has the PH-5 "
    "for-loop per-iteration rewrite (`qbool tmp = a | b;` + "
    "`uncompute_or(tmp, a, b);` inside the loop body), the if/else "
    "per-branch rewrite (`qbool x = a | b;` + "
    "`uncompute_or(x, a, b);` inside the then-arm, "
    "`qbool y = a | b;` + `uncompute_or(y, a, b);` inside the "
    "else-arm, in source order), and the source is byte-identical")
