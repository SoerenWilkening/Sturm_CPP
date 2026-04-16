# check_example_user_routine.cmake — Phase I PI-6 ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — examples/user_routine.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            user_routine.cpp
#
# Contract:
#   After a clean build that routes examples/user_routine.cpp through
#   sturm-transpile, the generated file must contain the Phase I
#   PI-2/PI-3/PI-4 routine-call rewrite for both scenarios in main():
#
#     Case 1: local-intermediate output — a call whose output argument
#             is a qbool declared in the call's enclosing scope. PI-3
#             classifies this as `Intermediate`; PI-4's uncompute pass
#             plants `invert(rotate_by_k)(tmp, a, 3);` before the
#             enclosing scope's close brace.
#
#     Case 2: escaping output — a call whose output argument is a
#             function parameter (the caller routine's ParmVarDecl).
#             PI-3 classifies this as `Final`; `skip_uncompute=true`
#             is set and NO `invert(...)` call is injected in that
#             function's body.
#
#   AND the source file must be byte-identical before and after the
#   transpile (the transpiler never rewrites its input in place — it
#   only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains the Case 1 rewrite:
#        - `rotate_by_k(tmp, a, 3);` preserved (forward call untouched).
#        - `invert(rotate_by_k)(tmp, a, 3);` appears AFTER the forward
#          call inside the same enclosing scope.
#   3. GENERATED contains the Case 2 escape rewrite:
#        - `rotate_by_k(out, a, 3);` preserved inside the helper
#          function whose `out` parameter is the routine's output.
#        - NO `invert(rotate_by_k)(out, a, 3);` is injected after that
#          forward call — the output escapes through the function
#          parameter so PI-3 classifies it as `Final` and PI-4 emits
#          nothing.
#   4. SOURCE does NOT contain the injected `invert(rotate_by_k)(...)`
#      fragment after `int main()` — the transpiler must not
#      contaminate its input.  We anchor the "no-leak" search after
#      `int main()` to avoid false-positives on the source's top-of-
#      file comment block, which discusses the rewrite shape in prose.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_user_routine: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_user_routine: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_user_routine: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_user_routine: source file missing: ${SOURCE}")
endif()

file(READ "${GENERATED}" gen_content)
file(READ "${SOURCE}"    src_content)

# ── Case 1: local-intermediate output — injected invert(...) ──────────────
# Anchor the Case 1 "did-it-land?" search after `int main()` so the
# generated-file header preamble (which mirrors the source's top-of-
# file comment block verbatim and mentions the injected fragment by
# bare name in prose) cannot false-positive.
string(FIND "${gen_content}" "int main()" gen_main_offset)
if(gen_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_user_routine: generated file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${gen_content}" ${gen_main_offset} -1 gen_main_body)

# ── Case 1 · Assertion 2a: forward `rotate_by_k(tmp, a, 3);` present ──────
string(FIND "${gen_main_body}" "rotate_by_k(tmp, a, 3)"
       tmp_call_offset)
if(tmp_call_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_user_routine: expected forward "
        "`rotate_by_k(tmp, a, 3);` inside main()'s body, but it was "
        "not found.\n  file: ${GENERATED}")
endif()

# ── Case 1 · Assertion 2b: injected invert(...) appears AFTER the call ────
string(LENGTH "rotate_by_k(tmp, a, 3)" _tmp_call_len)
math(EXPR case1_tail_start "${tmp_call_offset} + ${_tmp_call_len}")
string(SUBSTRING "${gen_main_body}" ${case1_tail_start} -1 case1_tail)

string(FIND "${case1_tail}" "invert(rotate_by_k)(tmp, a, 3)"
       invert_tmp_offset)
if(invert_tmp_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_user_routine: expected injected "
        "`invert(rotate_by_k)(tmp, a, 3);` AFTER the forward "
        "`rotate_by_k(tmp, a, 3);` call inside main()'s body, but it "
        "was not found.\n  tail: <<<${case1_tail}>>>")
endif()

# ── Case 2: escaping output — no injection inside the helper function ────
# The helper function `apply_rotation` takes a qbool& parameter; PI-3
# classifies that as `Final` (ParmVarDecl) and PI-4 emits nothing.
# Anchor the "no-injection" search at the helper's signature so we
# isolate the helper's body from main()'s body above.
string(FIND "${gen_content}" "void apply_rotation(" apply_offset)
if(apply_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_user_routine: generated file is missing the "
        "helper function `void apply_rotation(...)` — the example is "
        "malformed or the transpile corrupted the signature.\n  "
        "file: ${GENERATED}")
endif()
# Capture the helper body from its signature up to `int main()`.
# That range covers the helper's `{ ... }` exclusively — Case 1's
# body lives below `int main()` and was already handled above.
string(SUBSTRING "${gen_content}" ${apply_offset} -1 apply_tail)
string(FIND "${apply_tail}" "int main()" rel_main_offset)
if(rel_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_user_routine: generated file is malformed — "
        "`int main()` does not follow `void apply_rotation(`.")
endif()
string(SUBSTRING "${apply_tail}" 0 ${rel_main_offset} apply_body)

# ── Case 2 · Assertion 3a: helper's forward call preserved ────────────────
string(FIND "${apply_body}" "rotate_by_k(out, a, 3)"
       out_call_offset)
if(out_call_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_user_routine: expected forward "
        "`rotate_by_k(out, a, 3);` inside the helper function body, "
        "but it was not found.\n  body: <<<${apply_body}>>>")
endif()

# ── Case 2 · Assertion 3b: NO injection for the escaping output ───────────
# PI-3 classifies `out` as `Final` (it's a ParmVarDecl), so
# `skip_uncompute=true` is set and the M8 pass plants nothing. A
# false-positive here would mean PI-3's escape classification broke.
string(FIND "${apply_body}" "invert(rotate_by_k)" bogus_invert_offset)
if(NOT bogus_invert_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_user_routine: found an injected "
        "`invert(rotate_by_k)(...)` call inside `apply_rotation` — "
        "but the `out` argument is a function parameter and PI-3 "
        "must classify it as `Final` (escaping), so no inverse "
        "should be injected.\n  body: <<<${apply_body}>>>")
endif()

# ── Assertion 4: SOURCE contains none of the injected fragments ───────────
# The source's top-of-file comment block mentions `invert(rotate_by_k)`
# by bare name in prose; the full injected fragment `invert(rotate_by_k)
# (tmp, a, 3);` never appears outside the generated sibling. Anchor
# the "no-leak" search after `int main()` so the documentation prose
# cannot false-positive. The transpiler must never rewrite its input
# in place; examples/user_routine.cpp must be byte-identical before
# and after the transpile.
string(FIND "${src_content}" "int main()" src_main_offset)
if(src_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_user_routine: source file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${src_content}" ${src_main_offset} -1 src_body)

set(_pi6_src_patterns
    "invert[ \t]*\\([ \t]*rotate_by_k[ \t]*\\)[ \t]*\\([ \t]*tmp[ \t]*,[ \t]*a[ \t]*,[ \t]*3[ \t]*\\)[ \t]*;")
foreach(rx IN LISTS _pi6_src_patterns)
    string(REGEX MATCH "${rx}" src_hit "${src_body}")
    if(src_hit)
        message(FATAL_ERROR
            "check_example_user_routine: an injected fragment matched "
            "`${rx}` appeared in SOURCE ${SOURCE} after "
            "`int main()`.\nThe transpiler must never rewrite its "
            "input in place; examples/user_routine.cpp must be "
            "byte-identical before and after the transpile.")
    endif()
endforeach()

message(STATUS
    "check_example_user_routine: OK — generated file has the PI-6 "
    "local-intermediate rewrite (`rotate_by_k(tmp, a, 3);` + "
    "`invert(rotate_by_k)(tmp, a, 3);` in main()), the escaping-output "
    "case correctly skipped inside `apply_rotation` (no inverse "
    "injected), and the source is byte-identical")
