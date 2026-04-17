# check_example_uncompute_hoisting.cmake — Phase J PJ-3g ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — examples/uncompute_hoisting.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            uncompute_hoisting.cpp
#
# Contract:
#   After a clean build that routes examples/uncompute_hoisting.cpp
#   through sturm-transpile, the generated file must contain the Phase
#   J PJ-3d uncompute-hoisting rewrite for the happy-path loop inside
#   main()'s inner scope:
#
#     Source shape (source-file layout preserved inside main()):
#         {
#             qbool a;
#             qbool b;
#             qbool t;                    // outer predecl — pragmatic
#             for (int i = 0; i < 3; ++i) {
#                 qbool t = a | b;
#                 (void)t;
#             }
#         }
#
#     Generated rewrite:
#         {
#             qbool a;
#             qbool b;
#             qbool t;
#             for (int i = 0; i < 3; ++i) {
#                 qbool t = a | b;        // forward stays in loop body
#                 (void)t;                 //  (PJ-3d only moves the
#             }                            //   uncompute anchor; forward
#                                          //   text-move is PJ-3g+)
#             // ...
#             uncompute_or(t, a, b);       // hoisted OUT of loop body,
#         }                                 //  lands at the ENCLOSING
#                                          //  scope's close brace
#
#   AND the source file must be byte-identical before and after the
#   transpile (the transpiler never rewrites its input in place — it
#   only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED preserves the forward `qbool t = a | b;` inside the
#      for-loop body of main().  PJ-3d only relocates the uncompute
#      anchor — the forward compute stays per-iteration until the
#      PJ-3g+ forward text-move phase lands.
#   3. GENERATED contains the hoisted `uncompute_or(t, a, b);` call
#      AFTER the forward decl.  The M8 synthesis pass consumes
#      `op.hoist_to_override` at uncompute-emission time, planting
#      the call at the enclosing scope's close brace rather than the
#      loop body's close brace.
#   4. GENERATED lands the `uncompute_or(t, a, b);` call AFTER the
#      for-loop's closing `}` — NOT immediately before it.  This is
#      the load-bearing PJ-3d semantic: `hoist_to_override` takes
#      precedence over `scope.close_brace` in `uncompute_pass.cpp`'s
#      anchor-selection ladder.  We anchor on the `(void)t;` reader
#      and the `}` that closes the for-body to verify the ordering.
#   5. SOURCE does NOT contain the injected `uncompute_or(t, a, b);`
#      fragment after `int main()` — the transpiler must not
#      contaminate its input.  The top-of-file comment block
#      discusses the rewrite shape in prose by bare name, so we
#      anchor the "no-leak" search after `int main()` to avoid
#      false-positives.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_uncompute_hoisting: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_uncompute_hoisting: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_uncompute_hoisting: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_uncompute_hoisting: source file missing: ${SOURCE}")
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
        "check_example_uncompute_hoisting: generated file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${gen_content}" ${gen_main_offset} -1 gen_body)

# ── Assertion 2: forward `qbool t = a | b;` preserved in for-body ─────────
# PJ-3d only relocates the uncompute anchor; the forward compute stays
# inside the loop body, firing per-iteration.  A failure here would
# mean the matcher accidentally replaced the forward text — not
# expected until the PJ-3g+ forward text-move phase lands.
string(FIND "${gen_body}" "qbool t = a | b;" forward_decl_offset)
if(forward_decl_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_uncompute_hoisting: expected forward "
        "`qbool t = a | b;` inside main()'s for-loop body, but it "
        "was not found — the MVP OR matcher must leave the forward "
        "decl in place (PJ-3d only moves the uncompute anchor).\n"
        "  file: ${GENERATED}")
endif()

# ── Assertion 3: hoisted `uncompute_or(t, a, b);` planted AFTER decl ──────
# The M8 synthesis pass consumes `op.hoist_to_override` at uncompute-
# emission time, planting the call at the enclosing scope's close
# brace.  Anchor the search AFTER the forward decl so we cannot
# collide with any earlier occurrence.
string(LENGTH "qbool t = a | b;" _fwd_len)
math(EXPR post_fwd_start "${forward_decl_offset} + ${_fwd_len}")
string(SUBSTRING "${gen_body}" ${post_fwd_start} -1 post_fwd_tail)

string(FIND "${post_fwd_tail}" "uncompute_or(t, a, b)"
       uncompute_offset)
if(uncompute_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_uncompute_hoisting: expected injected "
        "`uncompute_or(t, a, b);` AFTER the forward "
        "`qbool t = a | b;` decl inside main()'s body, but it was "
        "not found — the PJ-3d hoist matcher did not fire OR the "
        "M8 synthesis pass did not consume `hoist_to_override`.\n"
        "  tail: <<<${post_fwd_tail}>>>")
endif()

# ── Assertion 4: `uncompute_or(t, a, b);` lands AFTER the for-loop `}` ────
# The load-bearing PJ-3d semantic: `op.hoist_to_override` takes
# precedence over `scope.close_brace` in the anchor-selection ladder,
# so the uncompute lands at the ENCLOSING scope's close brace rather
# than the loop body's close brace.  We locate the `(void)t;` reader
# (last stmt in the for-body), then the FIRST `}` after it (the for-
# body's close brace), and verify the uncompute sits AFTER that `}`
# rather than before it.
string(FIND "${gen_body}" "(void)t;" void_t_offset)
if(void_t_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_uncompute_hoisting: expected `(void)t;` reader "
        "inside main()'s for-loop body, but it was not found — the "
        "source's reader stmt is missing from the generated file.")
endif()

# Find the first `}` after `(void)t;` — that's the for-body's close brace.
string(LENGTH "(void)t;" _void_t_len)
math(EXPR after_void_t "${void_t_offset} + ${_void_t_len}")
string(SUBSTRING "${gen_body}" ${after_void_t} -1 after_void_t_tail)
string(FIND "${after_void_t_tail}" "}" for_body_close_rel_offset)
if(for_body_close_rel_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_uncompute_hoisting: expected a closing `}` "
        "after `(void)t;` — the for-body has no close brace in the "
        "generated file.")
endif()
math(EXPR for_body_close_abs
     "${after_void_t} + ${for_body_close_rel_offset}")

# Compute absolute offsets for `(void)t;`, the for-body close `}`, and
# the `uncompute_or(t, a, b)` call within gen_body.  The uncompute
# MUST sit AFTER the for-body close `}` — that is the PJ-3d invariant
# this assertion pins.
string(FIND "${gen_body}" "uncompute_or(t, a, b)" abs_uncompute_offset)
if(abs_uncompute_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_uncompute_hoisting: absolute-offset lookup "
        "failed for `uncompute_or(t, a, b)` — this should have been "
        "caught by assertion 3 above.")
endif()

if(NOT abs_uncompute_offset GREATER for_body_close_abs)
    message(FATAL_ERROR
        "check_example_uncompute_hoisting: the hoisted "
        "`uncompute_or(t, a, b);` call did NOT land after the for-"
        "loop's close brace.\n"
        "  for-body `}` offset: ${for_body_close_abs}\n"
        "  uncompute offset:    ${abs_uncompute_offset}\n"
        "The PJ-3d semantic requires `op.hoist_to_override` to "
        "relocate the uncompute to the ENCLOSING scope's close brace, "
        "AFTER the loop body's `}`.  A failure here means the hoist "
        "matcher did not fire OR the M8 synthesis pass ignored the "
        "override and fell through to `scope.close_brace`.")
endif()

# ── Assertion 5: SOURCE contains no injected fragment after main() ────────
# The source's top-of-file comment block mentions `uncompute_or` by
# bare name in prose.  The full injected fragment spellings below
# never appear outside the generated sibling.  Anchor the "no-leak"
# search after `int main()` to avoid false-positives on the
# documentation prose.  The transpiler must never rewrite its input
# in place; examples/uncompute_hoisting.cpp must be byte-identical
# before and after the transpile.
string(FIND "${src_content}" "int main()" src_main_offset)
if(src_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_uncompute_hoisting: source file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${src_content}" ${src_main_offset} -1 src_body)

string(REGEX MATCH "uncompute_or[ \t]*\\([ \t]*t[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)[ \t]*;"
       src_hit "${src_body}")
if(src_hit)
    message(FATAL_ERROR
        "check_example_uncompute_hoisting: an injected fragment "
        "matched `uncompute_or(t, a, b);` appeared in SOURCE "
        "${SOURCE} after `int main()`.\n"
        "The transpiler must never rewrite its input in place; "
        "examples/uncompute_hoisting.cpp must be byte-identical "
        "before and after the transpile.")
endif()

message(STATUS
    "check_example_uncompute_hoisting: OK — generated file has the "
    "PJ-3d uncompute-hoisting rewrite (forward `qbool t = a | b;` "
    "preserved inside the for-body, hoisted `uncompute_or(t, a, b);` "
    "planted AFTER the for-loop's close brace at the enclosing "
    "scope's close brace), and the source is byte-identical")
