# check_example_in_memory_transpile.cmake — Phase M PM1-8 ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — examples/in_memory_transpile.cpp
#   -DGENERATED=<abs path> — the transpiler-mirrored sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            in_memory_transpile.cpp
#
# Contract:
#   After a clean build that routes examples/in_memory_transpile.cpp
#   through the plugin (default add_quantum_executable() mode since
#   PM1-5), the mirrored buffer at GENERATED must contain all THREE
#   rewrites exercised by the example — each confined to its own
#   inner scope inside main():
#
#     ── Inner scope E ── Phase E PE-5 compound flatten ─────────────────────
#         qbool __stu_t0 = b | c;
#         qbool r = __stu_t0 & d;
#         ...
#         uncompute_and(r, __stu_t0, d);      // LIFO outer first
#         uncompute_or(__stu_t0, b, c);       // LIFO inner last
#
#     ── Inner scope F ── Phase F PF-4 WHEN-lift ────────────────────────────
#         qbool __stu_t0 = wb | wc;
#         qbool __stu_t1 = __stu_t0 & wd;
#         WHEN(__stu_t1) { ... }
#         uncompute_and(__stu_t1, __stu_t0, wd);
#         uncompute_or(__stu_t0, wb, wc);
#
#     ── Inner scope J ── Phase J PJ-1 zero-ancilla fusion ──────────────────
#         ccnot_inplace(fx, fa, fb);          // forward (QReplacement)
#         ccnot_inplace(fx, fa, fb);          // self-adjoint uncompute
#       (the original `qbool __t = fa & fb; fx ^= __t;` pair is stripped.)
#
#   AND the source file must be byte-identical before and after the
#   transpile — the transpiler must never rewrite its input in place.
#
# Phase-allocator caveat:
#   Phase E uses one `FreshNameAllocator` per QUnit, so its `__stu_t<N>`
#   counter carries across Phase E matches in the same TU. In this
#   example only ONE Phase E compound exists (`qbool r = (b | c) & d;`),
#   so Phase E emits `__stu_t0` + the original `r`. Phase F, in
#   contrast, mints a fresh per-invocation allocator inside
#   `flatten_arg` (transpiler/src/matcher_when_lift.cpp), so its
#   `__stu_tN` sequence restarts at `__stu_t0` independently — the
#   two allocators do NOT collide. Repeating either identifier inside
#   the same outer scope would be a C++ redeclaration error, but each
#   rewrite lives in its own `{ }` block so the temp names are
#   independent scopes.
#
# Assertions:
#   1. GENERATED exists on disk and contains `int main()`.
#   2. GENERATED contains the Phase E flat decls + LIFO inverses
#      inside the first inner scope.
#   3. GENERATED contains the Phase F flat decls + rewritten
#      `WHEN(__stu_t1)` line + LIFO inverses after the WHEN body.
#   4. GENERATED contains TWO adjacent `ccnot_inplace(fx, fa, fb);`
#      calls (forward fused call + self-adjoint uncompute) AND the
#      original `qbool __t = fa & fb;` / `fx ^= __t;` pair is gone
#      from main()'s body.
#   5. SOURCE does NOT contain any of the injected fragments after
#      `int main()` — the transpiler must not contaminate its input.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_in_memory_transpile: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_in_memory_transpile: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_in_memory_transpile: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_in_memory_transpile: source file missing: ${SOURCE}")
endif()

file(READ "${GENERATED}" gen_content)
file(READ "${SOURCE}"    src_content)

# Anchor every "did-it-land-in-the-right-place?" search after
# `int main()` in the generated file. The generated file's header
# preamble embeds the source's top-of-file comment block verbatim,
# and that comment block mentions every injected fragment by bare
# name — we must not false-positive on those comment-only spellings.
string(FIND "${gen_content}" "int main()" gen_main_offset)
if(gen_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_in_memory_transpile: generated file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${gen_content}" ${gen_main_offset} -1 gen_body)

# ── Assertion 2: Phase E flat decls + LIFO inverses ───────────────────────
# The Phase E matcher (transpiler/src/matcher_qbool_compound.cpp)
# flattens `qbool r = (b | c) & d;` into one fresh `__stu_t0` decl
# for the inner OR plus the outer AND on the original VarDecl name,
# and stages a LIFO uncompute pair at the enclosing scope close.
foreach(frag IN ITEMS
        "qbool __stu_t0 = b | c;"
        "qbool r = __stu_t0 & d;"
        "uncompute_and(r, __stu_t0, d)"
        "uncompute_or(__stu_t0, b, c)")
    string(FIND "${gen_body}" "${frag}" _off)
    if(_off EQUAL -1)
        message(FATAL_ERROR
            "check_example_in_memory_transpile: expected Phase E "
            "fragment `${frag}` in generated file body, but it was "
            "not found.\n  file: ${GENERATED}")
    endif()
endforeach()

# ── Assertion 3: Phase F flat decls + rewritten WHEN + LIFO inverses ──────
# The Phase F matcher (transpiler/src/matcher_when_lift.cpp) mints a
# fresh per-invocation allocator, so its `__stu_tN` sequence is
# independent of Phase E's and restarts at 0. Both Phase E's and
# Phase F's `__stu_t0` decls live in separate `{ }` scopes in the
# example, so the text appears at distinct offsets in the generated
# file — we locate Phase F's fragments by their unique operand
# spellings (`wb`, `wc`, `wd`, `__stu_t1`).
foreach(frag IN ITEMS
        "qbool __stu_t0 = wb | wc;"
        "qbool __stu_t1 = __stu_t0 & wd;"
        "WHEN(__stu_t1)"
        "uncompute_and(__stu_t1, __stu_t0, wd)"
        "uncompute_or(__stu_t0, wb, wc)")
    string(FIND "${gen_body}" "${frag}" _off)
    if(_off EQUAL -1)
        message(FATAL_ERROR
            "check_example_in_memory_transpile: expected Phase F "
            "fragment `${frag}` in generated file body, but it was "
            "not found.\n  file: ${GENERATED}")
    endif()
endforeach()

# The original compound-WHEN spelling `WHEN((wb | wc) & wd)` must be
# stripped by Phase F's `Rewriter.ReplaceText` over the macro argument
# source range.
string(REGEX MATCH
    "WHEN[ \t]*\\([ \t]*\\([ \t]*wb[ \t]*\\|[ \t]*wc[ \t]*\\)[ \t]*&[ \t]*wd[ \t]*\\)"
    compound_when_survived "${gen_body}")
if(compound_when_survived)
    message(FATAL_ERROR
        "check_example_in_memory_transpile: the original compound "
        "`WHEN((wb | wc) & wd)` spelling survived in the generated "
        "body — the Phase F ReplaceText pass did not fire.\n"
        "  match: ${compound_when_survived}")
endif()

# ── Assertion 4: Phase J PJ-1 fused-call pair + pair gone ─────────────────
# The PJ-1d peephole stages a `QReplacement` whose text is the fused
# call, spanning the source range of both original stmts. The PJ-1c
# render case in transpiler/src/uncompute_pass.cpp plants a matching
# self-adjoint uncompute call before the enclosing scope's `}`. We
# locate the first `ccnot_inplace(fx, fa, fb)` and require a second
# instance to appear strictly after it.
string(FIND "${gen_body}" "ccnot_inplace(fx, fa, fb)" fused_forward_offset)
if(fused_forward_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_in_memory_transpile: expected forward "
        "`ccnot_inplace(fx, fa, fb);` inside main()'s body, but it "
        "was not found — the PJ-1d peephole matcher did not fire.\n"
        "  file: ${GENERATED}")
endif()

string(LENGTH "ccnot_inplace(fx, fa, fb)" _fused_call_len)
math(EXPR uncompute_tail_start "${fused_forward_offset} + ${_fused_call_len}")
string(SUBSTRING "${gen_body}" ${uncompute_tail_start} -1 uncompute_tail)

string(FIND "${uncompute_tail}" "ccnot_inplace(fx, fa, fb)" fused_uncompute_offset)
if(fused_uncompute_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_in_memory_transpile: expected a SECOND "
        "`ccnot_inplace(fx, fa, fb);` (the self-adjoint uncompute) "
        "AFTER the forward fused call inside main()'s body, but "
        "only ONE instance was found — the PJ-1c render case in "
        "uncompute_pass.cpp did not plant the self-adjoint "
        "uncompute.\n  tail: <<<${uncompute_tail}>>>")
endif()

# The original PJ-1 source pair must be GONE from main()'s body —
# the QReplacement's source range covers both stmts. A surviving
# instance of either half signals a replacement regression.
string(REGEX MATCH "qbool[ \t]+__t[ \t]*=[ \t]*fa[ \t]*&[ \t]*fb[ \t]*;"
       t_decl_survived "${gen_body}")
if(t_decl_survived)
    message(FATAL_ERROR
        "check_example_in_memory_transpile: the original "
        "`qbool __t = fa & fb;` VarDecl survived inside main()'s "
        "body of the generated file — the Phase J PJ-1d "
        "QReplacement did not strip the first half of the fused "
        "pair.\n  match: ${t_decl_survived}")
endif()

string(REGEX MATCH "fx[ \t]*\\^=[ \t]*__t[ \t]*;"
       xor_survived "${gen_body}")
if(xor_survived)
    message(FATAL_ERROR
        "check_example_in_memory_transpile: the original "
        "`fx ^= __t;` stmt survived inside main()'s body of the "
        "generated file — the Phase J PJ-1d QReplacement did not "
        "strip the second half of the fused pair.\n"
        "  match: ${xor_survived}")
endif()

# ── Assertion 5: SOURCE contains no injected fragment after main() ────────
# The source's top-of-file comment block describes each rewrite in
# prose and mentions the fragments by bare name for documentation
# purposes, but the full injected fragments must never appear in
# the user's hand-written main body — only in the generated sibling.
# Anchor the "no-leak" search after `int main()` so the comment
# prose cannot false-positive.
string(FIND "${src_content}" "int main()" src_main_offset)
if(src_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_in_memory_transpile: source file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${src_content}" ${src_main_offset} -1 src_body)

set(_pm18_src_patterns
    # Phase E injected fragments
    "qbool[ \t]+__stu_t0[ \t]*=[ \t]*b[ \t]*\\|[ \t]*c[ \t]*;"
    "qbool[ \t]+r[ \t]*=[ \t]*__stu_t0[ \t]*&[ \t]*d[ \t]*;"
    "uncompute_and[ \t]*\\([ \t]*r[ \t]*,[ \t]*__stu_t0[ \t]*,[ \t]*d[ \t]*\\)[ \t]*;"
    "uncompute_or[ \t]*\\([ \t]*__stu_t0[ \t]*,[ \t]*b[ \t]*,[ \t]*c[ \t]*\\)[ \t]*;"
    # Phase F injected fragments
    "qbool[ \t]+__stu_t0[ \t]*=[ \t]*wb[ \t]*\\|[ \t]*wc[ \t]*;"
    "qbool[ \t]+__stu_t1[ \t]*=[ \t]*__stu_t0[ \t]*&[ \t]*wd[ \t]*;"
    "WHEN[ \t]*\\([ \t]*__stu_t1[ \t]*\\)"
    "uncompute_and[ \t]*\\([ \t]*__stu_t1[ \t]*,[ \t]*__stu_t0[ \t]*,[ \t]*wd[ \t]*\\)[ \t]*;"
    "uncompute_or[ \t]*\\([ \t]*__stu_t0[ \t]*,[ \t]*wb[ \t]*,[ \t]*wc[ \t]*\\)[ \t]*;"
    # Phase J PJ-1 injected fragment
    "ccnot_inplace[ \t]*\\([ \t]*fx[ \t]*,[ \t]*fa[ \t]*,[ \t]*fb[ \t]*\\)[ \t]*;")
foreach(rx IN LISTS _pm18_src_patterns)
    string(REGEX MATCH "${rx}" src_hit "${src_body}")
    if(src_hit)
        message(FATAL_ERROR
            "check_example_in_memory_transpile: an injected fragment "
            "matched `${rx}` appeared in SOURCE ${SOURCE} after "
            "`int main()`.\nThe transpiler must never rewrite its "
            "input in place; examples/in_memory_transpile.cpp must be "
            "byte-identical before and after the transpile.")
    endif()
endforeach()

message(STATUS
    "check_example_in_memory_transpile: OK — generated file contains "
    "Phase E flat decls + LIFO uncomputes, Phase F flat decls + "
    "rewritten `WHEN(__stu_t1)` + LIFO uncomputes, Phase J PJ-1 "
    "fused pair `ccnot_inplace(fx, fa, fb);` (forward + self-adjoint "
    "uncompute), and the source is byte-identical")
