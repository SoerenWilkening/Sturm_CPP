# check_example_zero_ancilla_fusion.cmake — Phase J PJ-1h ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — examples/zero_ancilla_fusion.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            zero_ancilla_fusion.cpp
#
# Contract:
#   After a clean build that routes examples/zero_ancilla_fusion.cpp
#   through sturm-transpile, the generated file must contain the
#   Phase J PJ-1d two-point fusion rewrite for the happy-path pair
#   inside main()'s inner scope:
#
#     Source pair (source-file layout preserved inside main()):
#         qbool __t = a & b;
#         x ^= __t;
#
#     Generated rewrite:
#         ccnot_inplace(x, a, b);    // forward QReplacement — PJ-1d
#                                    // collapses the source PAIR into
#                                    // this single call.
#         ccnot_inplace(x, a, b);    // self-adjoint uncompute planted
#                                    // before the inner scope's `}` by
#                                    // the PJ-1c render case in
#                                    // transpiler/src/uncompute_pass.cpp.
#
#   AND the source file must be byte-identical before and after the
#   transpile (the transpiler never rewrites its input in place — it
#   only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains the forward fused call:
#        - `ccnot_inplace(x, a, b);` appears inside main()'s body
#          (the first instance of the symbol after `int main()`).
#   3. GENERATED contains a SECOND `ccnot_inplace(x, a, b);` call
#      AFTER the first one — the PJ-1c self-adjoint uncompute
#      emission.  Both instances share the same arg spelling because
#      CCX is its own inverse, so forward + uncompute share one symbol.
#   4. GENERATED no longer contains the ORIGINAL pair inside main()'s
#      body: neither `qbool __t = a & b` nor `x ^= __t` survives.  The
#      QReplacement's SourceRange spans both stmts — a surviving
#      instance of either would signal that the Rewriter's ReplaceText
#      dropped the replacement (e.g. because raw insertions were
#      applied before replacements and the edit landed in a stale
#      buffer position).  We anchor the "no-survival" search after
#      `int main()` so the top-of-file comment block's prose
#      mentions of the original pair cannot false-positive.
#   5. SOURCE does NOT contain the injected
#      `ccnot_inplace(x, a, b);` fragment after `int main()` — the
#      transpiler must not contaminate its input.  We anchor the
#      "no-leak" search after `int main()` to avoid false-positives
#      on the source's top-of-file comment block, which discusses
#      the rewrite shape in prose.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_zero_ancilla_fusion: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_zero_ancilla_fusion: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_zero_ancilla_fusion: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_zero_ancilla_fusion: source file missing: ${SOURCE}")
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
        "check_example_zero_ancilla_fusion: generated file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${gen_content}" ${gen_main_offset} -1 gen_body)

# ── Assertion 2: forward fused `ccnot_inplace(x, a, b);` present ──────────
# The PJ-1d peephole matcher stages a `QReplacement` whose text is the
# fused call, spanning the source range of both original stmts.
string(FIND "${gen_body}" "ccnot_inplace(x, a, b)" fused_forward_offset)
if(fused_forward_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_zero_ancilla_fusion: expected forward "
        "`ccnot_inplace(x, a, b);` inside main()'s body, but it was "
        "not found — the PJ-1d peephole matcher did not fire.\n"
        "  file: ${GENERATED}")
endif()

# ── Assertion 3: second `ccnot_inplace(x, a, b);` AFTER the first ─────────
# The PJ-1c render case in transpiler/src/uncompute_pass.cpp emits a
# matching self-adjoint uncompute call before the enclosing scope's
# closing `}`.  Anchor the search AFTER the first occurrence.
string(LENGTH "ccnot_inplace(x, a, b)" _fused_call_len)
math(EXPR uncompute_tail_start "${fused_forward_offset} + ${_fused_call_len}")
string(SUBSTRING "${gen_body}" ${uncompute_tail_start} -1 uncompute_tail)

string(FIND "${uncompute_tail}" "ccnot_inplace(x, a, b)" fused_uncompute_offset)
if(fused_uncompute_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_zero_ancilla_fusion: expected a SECOND "
        "`ccnot_inplace(x, a, b);` (the self-adjoint uncompute) "
        "AFTER the forward fused call inside main()'s body, but "
        "only ONE instance was found — the PJ-1c render case in "
        "uncompute_pass.cpp did not plant the self-adjoint "
        "uncompute.\n  tail: <<<${uncompute_tail}>>>")
endif()

# ── Assertion 4: the original pair is GONE from main()'s body ─────────────
# The PJ-1d QReplacement covers BOTH stmts (VarDecl begin through
# `^=` op-call's terminating `;`), so `Rewriter.ReplaceText` strips
# every token in that range.  Neither the `__t` decl nor the `^=`
# consumer should survive inside main()'s body — any survival signals
# a replacement regression (e.g. raw insertions applied before
# replacements, Clang's Rewriter dropping the edit silently).
#
# Anchor on `int main()` so the top-of-file comment block's prose
# mentions of the source pair cannot false-positive.
string(REGEX MATCH "qbool[ \t]+__t[ \t]*=[ \t]*a[ \t]*&[ \t]*b[ \t]*;"
       t_decl_survived "${gen_body}")
if(t_decl_survived)
    message(FATAL_ERROR
        "check_example_zero_ancilla_fusion: the original "
        "`qbool __t = a & b;` VarDecl survived inside main()'s body "
        "of the generated file — the Phase J PJ-1d QReplacement did "
        "not strip the first half of the fused pair.\n"
        "  match: ${t_decl_survived}")
endif()

string(REGEX MATCH "x[ \t]*\\^=[ \t]*__t[ \t]*;"
       xor_survived "${gen_body}")
if(xor_survived)
    message(FATAL_ERROR
        "check_example_zero_ancilla_fusion: the original "
        "`x ^= __t;` stmt survived inside main()'s body of the "
        "generated file — the Phase J PJ-1d QReplacement did not "
        "strip the second half of the fused pair.\n"
        "  match: ${xor_survived}")
endif()

# ── Assertion 5: SOURCE contains no injected fragment after main() ────────
# The source's top-of-file comment block mentions the injected
# `ccnot_inplace(x, a, b);` fragment by bare name in prose.  The
# full injected fragment never appears outside the generated
# sibling.  Anchor the "no-leak" search after `int main()` so the
# documentation prose cannot false-positive.  The transpiler must
# never rewrite its input in place; examples/zero_ancilla_fusion.cpp
# must be byte-identical before and after the transpile.
string(FIND "${src_content}" "int main()" src_main_offset)
if(src_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_zero_ancilla_fusion: source file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${src_content}" ${src_main_offset} -1 src_body)

string(REGEX MATCH "ccnot_inplace[ \t]*\\([ \t]*x[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)[ \t]*;"
       src_hit "${src_body}")
if(src_hit)
    message(FATAL_ERROR
        "check_example_zero_ancilla_fusion: an injected fragment "
        "matched `ccnot_inplace(x, a, b);` appeared in SOURCE "
        "${SOURCE} after `int main()`.\n"
        "The transpiler must never rewrite its input in place; "
        "examples/zero_ancilla_fusion.cpp must be byte-identical "
        "before and after the transpile.")
endif()

message(STATUS
    "check_example_zero_ancilla_fusion: OK — generated file has the "
    "PJ-1d fusion rewrite (one forward `ccnot_inplace(x, a, b);` + "
    "one self-adjoint uncompute `ccnot_inplace(x, a, b);` inside "
    "main()), the original `qbool __t = a & b; x ^= __t;` pair is "
    "gone from main()'s body, and the source is byte-identical")
