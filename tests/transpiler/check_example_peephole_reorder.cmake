# check_example_peephole_reorder.cmake — Phase M PM5-9 ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — examples/peephole_reorder.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            peephole_reorder.cpp
#
# Contract:
#   After a clean build that routes examples/peephole_reorder.cpp
#   through sturm-transpile, the generated file must reflect the
#   CURRENT pipeline emission for the canonical PM5 peephole-reorder
#   triple-shape source inside main()'s inner scope:
#
#     Source shape (source-file layout preserved inside main()):
#         {
#             qbool a(0.5); qbool b(0.5); qbool c(0.5);
#             qbool x(0.5); qbool y(0.5); qbool z(0.5);
#             qbool r = (a & b) | c;
#             y ^= z;
#             x ^= r;
#             (void)r;
#         }
#
#     Generated rewrite (current pipeline):
#         {
#             qbool a(0.5); qbool b(0.5); qbool c(0.5);
#             qbool x(0.5); qbool y(0.5); qbool z(0.5);
#             qbool __stu_t0 = a & b;   // PE-4 flatten — inner AND
#             qbool r = __stu_t0 | c;   // PE-4 flatten — outer OR
#             y ^= z;                    // preserved
#             x ^= r;                    // preserved
#             (void)r;
#             x ^= r;                    // PA-3 LIFO self-adjoint (for x)
#             y ^= z;                    // PA-3 LIFO self-adjoint (for y)
#             uncompute_or(r, __stu_t0, c);     // MVP OR uncompute
#             uncompute_and(__stu_t0, a, b);    // Phase E uncompute
#         }
#
#   PM5-5's peephole reorder matcher inspects the final op list but
#   conservatively refuses on the current MVP matcher set because the
#   PE-4 outer OR wedges between the inner AND and the user's
#   `x ^= r;` consumer in scope.ops (breaking the adjacent-triple Gate 1
#   precondition, and the outer OR reads __stu_t0 which breaks Gate 3's
#   footprint disjointness check).  This is documented explicitly in
#   the example's top-of-file prose and captured by the PM5-8 snapshot
#   fixtures under tests/transpiler/fixtures/reorder_*.expected.cpp.
#
#   The example therefore pins the CURRENT pipeline emission; any
#   future enablement (e.g. a standalone AND matcher whose decl-sink
#   emits synthetic `__stu_t*` temps without a wedge OR) would produce
#   a visible delta here, signaling that this check script needs
#   updating alongside the matcher change.
#
#   AND the source file must be byte-identical before and after the
#   transpile (the transpiler never rewrites its input in place — it
#   only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains the PE-4 flatten emission: both
#      `qbool __stu_t0 = a & b;` and `qbool r = __stu_t0 | c;` appear
#      inside main()'s body (anchored after `int main()` so the
#      top-of-file prose mentions do not false-positive).
#   3. GENERATED contains both user XOR_ASSIGN stmts preserved
#      verbatim: `y ^= z;` and `x ^= r;` inside main()'s body.
#   4. GENERATED contains the Phase E LIFO uncompute pair at scope
#      close: `uncompute_or(r, __stu_t0, c);` AFTER the forward
#      `qbool r = __stu_t0 | c;` decl, and
#      `uncompute_and(__stu_t0, a, b);` after that.
#   5. GENERATED does NOT contain a `ccnot_inplace(` call inside
#      main()'s body — PM5 should NOT fire on the current pipeline
#      (the presence of the fused call would mean the reorder +
#      downstream fuse collapsed the triple, which requires either a
#      future standalone AND matcher OR a matcher relaxation).  The
#      absence is the load-bearing "current-pipeline pin" observable
#      — see top-of-file prose.
#   6. SOURCE does NOT contain the PE-4 flattened `qbool __stu_t0`
#      fragment AFTER `int main()` — the transpiler must not
#      contaminate its input.  We anchor on `int main()` so the
#      top-of-file comment block's prose mentions (which spell
#      `__stu_t0` in its before/after schematic) cannot
#      false-positive.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_peephole_reorder: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_peephole_reorder: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_peephole_reorder: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_peephole_reorder: source file missing: ${SOURCE}")
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
        "check_example_peephole_reorder: generated file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${gen_content}" ${gen_main_offset} -1 gen_body)

# ── Assertion 2: PE-4 flatten emission present ────────────────────────────
# The Phase E / PE-4 compound-flatten matcher replaces the user's
# `qbool r = (a & b) | c;` decl with a flat two-decl sequence: one
# synthetic `qbool __stu_t0 = a & b;` for the inner AND, and the
# outer OR now reads `qbool r = __stu_t0 | c;`.  Both lines must
# appear inside main()'s body.  The `__stu_t0` name is the default
# FreshNameAllocator emission when no prior compound-flatten has
# consumed the counter; any subsequent enablement that bumps the
# counter would produce `__stu_t1`, `__stu_t2`, etc. and this
# assertion would fail — at which point the failure diagnostic
# below instructs the reviewer to adjust the expected name.
string(FIND "${gen_body}" "qbool __stu_t0 = a & b;" pe4_inner_offset)
if(pe4_inner_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_peephole_reorder: expected the PE-4 flattened "
        "inner-AND decl `qbool __stu_t0 = a & b;` inside main()'s "
        "body, but it was not found — the PE-4 compound-flatten "
        "matcher did not fire on `qbool r = (a & b) | c;` OR the "
        "FreshNameAllocator counter advanced past `__stu_t0` before "
        "this invocation.\n  file: ${GENERATED}")
endif()

string(FIND "${gen_body}" "qbool r = __stu_t0 | c;" pe4_outer_offset)
if(pe4_outer_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_peephole_reorder: expected the PE-4 flattened "
        "outer-OR decl `qbool r = __stu_t0 | c;` inside main()'s "
        "body, but it was not found — the PE-4 compound-flatten "
        "matcher did not rewrite the outer OR to consume the "
        "synthetic `__stu_t0` temp.\n  file: ${GENERATED}")
endif()

# ── Assertion 3: user XOR_ASSIGNs preserved verbatim ──────────────────────
# PA-3 passes `y ^= z;` and `x ^= r;` through as forward XOR_ASSIGNs
# without any text rewrite; both stmts must survive verbatim inside
# main()'s body.  A failure here would signal that one of the PA-3
# forward-path guards regressed (e.g. a new no-leak guard
# over-fired and stripped the forward text).
string(REGEX MATCH "y[ \t]*\\^=[ \t]*z[ \t]*;"
       yxz_survived "${gen_body}")
if(NOT yxz_survived)
    message(FATAL_ERROR
        "check_example_peephole_reorder: the user-written "
        "`y ^= z;` stmt did NOT survive in main()'s body of the "
        "generated file — a PA-3 forward-path regression stripped "
        "the forward text unexpectedly.")
endif()

string(REGEX MATCH "x[ \t]*\\^=[ \t]*r[ \t]*;"
       xxr_survived "${gen_body}")
if(NOT xxr_survived)
    message(FATAL_ERROR
        "check_example_peephole_reorder: the user-written "
        "`x ^= r;` stmt did NOT survive in main()'s body of the "
        "generated file — a PA-3 forward-path regression stripped "
        "the forward text unexpectedly.")
endif()

# ── Assertion 4: Phase E LIFO uncompute pair lands at scope close ─────────
# The Phase E / PE-4 compound-flatten matcher stages uncompute ops
# in LIFO order: the outer OR's `uncompute_or(r, __stu_t0, c);` is
# planted FIRST at scope close, then the inner AND's
# `uncompute_and(__stu_t0, a, b);` follows (reverse-source-order
# semantic is "inverse executes the inner op last, which is the
# outer op's inverse first").  Anchor the search after the forward
# PE-4 outer-OR decl so we don't cross-match an earlier spelling.
string(LENGTH "qbool r = __stu_t0 | c;" _pe4_outer_len)
math(EXPR post_pe4_outer "${pe4_outer_offset} + ${_pe4_outer_len}")
string(SUBSTRING "${gen_body}" ${post_pe4_outer} -1 post_pe4_outer_tail)

string(FIND "${post_pe4_outer_tail}" "uncompute_or(r, __stu_t0, c)"
       uc_or_offset)
if(uc_or_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_peephole_reorder: expected injected "
        "`uncompute_or(r, __stu_t0, c);` AFTER the forward "
        "`qbool r = __stu_t0 | c;` decl inside main()'s body, but "
        "it was not found — the Phase E / MVP OR uncompute "
        "emission did not fire at scope close.\n"
        "  tail: <<<${post_pe4_outer_tail}>>>")
endif()

string(FIND "${post_pe4_outer_tail}" "uncompute_and(__stu_t0, a, b)"
       uc_and_offset)
if(uc_and_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_peephole_reorder: expected injected "
        "`uncompute_and(__stu_t0, a, b);` AFTER the "
        "`uncompute_or(...)` at scope close inside main()'s body, "
        "but it was not found — the Phase E AND uncompute "
        "emission did not fire.\n"
        "  tail: <<<${post_pe4_outer_tail}>>>")
endif()

# The LIFO order contract: uncompute_or lands BEFORE uncompute_and
# in source order (outer first → inner second).  Each offset is
# relative to post_pe4_outer_tail so comparing them directly checks
# source order.
if(NOT uc_and_offset GREATER uc_or_offset)
    message(FATAL_ERROR
        "check_example_peephole_reorder: the LIFO uncompute order "
        "is wrong.  `uncompute_or(r, __stu_t0, c);` must land "
        "BEFORE `uncompute_and(__stu_t0, a, b);` at scope close.\n"
        "  uncompute_or offset:  ${uc_or_offset}\n"
        "  uncompute_and offset: ${uc_and_offset}")
endif()

# ── Assertion 5: PM5 reorder does NOT fire (current pipeline pin) ─────────
# Through the current MVP matcher set the PM5-5 peephole reorder
# conservatively refuses — see top-of-file prose for the detailed
# rationale (PE-4 outer OR wedges between the inner AND and the
# user's `^=` consumer in scope.ops; Gate 3's footprint check would
# also refuse since B = outer OR reads `__stu_t0`).  The downstream
# PJ-1d ccnot-fuse peephole therefore does NOT fire either.  Absence
# of a `ccnot_inplace(` call inside main()'s body is the load-bearing
# observable: a future matcher relaxation that enables PM5 + PJ-1d
# on this shape would produce a `ccnot_inplace(x, a, b);` emission,
# at which point this assertion would fail and the reviewer should
# update the check script to reflect the new post-reorder layout.
string(FIND "${gen_body}" "ccnot_inplace(" ccnot_offset)
if(NOT ccnot_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_peephole_reorder: unexpected "
        "`ccnot_inplace(...)` call inside main()'s body of the "
        "generated file — the PM5 + PJ-1d fuse fired on source "
        "that historically did NOT trigger them.\n"
        "A matcher relaxation likely landed since this test was "
        "written; update the check script to reflect the new "
        "post-reorder / post-fuse layout.\n"
        "  offset: ${ccnot_offset}\n"
        "  file:   ${GENERATED}")
endif()

# ── Assertion 6: SOURCE contains no injected fragment after main() ────────
# The source's top-of-file comment block mentions `__stu_t0` in the
# before/after schematic prose.  The full injected spelling
# `qbool __stu_t0 = a & b;` never appears outside the generated
# sibling (the user did not write it by hand).  Anchor the no-leak
# search after `int main()` so the documentation prose cannot
# false-positive.  The transpiler must never rewrite its input in
# place; examples/peephole_reorder.cpp must be byte-identical
# before and after the transpile.
string(FIND "${src_content}" "int main()" src_main_offset)
if(src_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_peephole_reorder: source file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${src_content}" ${src_main_offset} -1 src_body)

string(REGEX MATCH "qbool[ \t]+__stu_t0[ \t]*=[ \t]*a[ \t]*&[ \t]*b[ \t]*;"
       src_stu_hit "${src_body}")
if(src_stu_hit)
    message(FATAL_ERROR
        "check_example_peephole_reorder: an injected fragment "
        "matched `qbool __stu_t0 = a & b;` appeared in SOURCE "
        "${SOURCE} after `int main()`.\n"
        "The transpiler must never rewrite its input in place; "
        "examples/peephole_reorder.cpp must be byte-identical "
        "before and after the transpile.")
endif()

string(REGEX MATCH "uncompute_or[ \t]*\\([ \t]*r[ \t]*,[ \t]*__stu_t0[ \t]*,[ \t]*c[ \t]*\\)[ \t]*;"
       src_uc_or_hit "${src_body}")
if(src_uc_or_hit)
    message(FATAL_ERROR
        "check_example_peephole_reorder: an injected fragment "
        "matched `uncompute_or(r, __stu_t0, c);` appeared in SOURCE "
        "${SOURCE} after `int main()`.\n"
        "The transpiler must never rewrite its input in place; "
        "examples/peephole_reorder.cpp must be byte-identical "
        "before and after the transpile.")
endif()

message(STATUS
    "check_example_peephole_reorder: OK — generated file reflects "
    "the current PM5 pipeline emission (PE-4 flatten on "
    "`(a & b) | c`, user `^=` stmts preserved, LIFO "
    "`uncompute_or` + `uncompute_and` pair at scope close, no "
    "`ccnot_inplace` since PM5 conservatively refuses the wedged "
    "triple), and the source is byte-identical")
