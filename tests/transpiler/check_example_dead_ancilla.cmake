# check_example_dead_ancilla.cmake — Phase J PJ-4d ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — examples/dead_ancilla.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            dead_ancilla.cpp
#
# Contract:
#   After a clean build that routes examples/dead_ancilla.cpp through
#   sturm-transpile, the generated file must contain the PJ-4a
#   dead-ancilla elimination for the happy-path `dead` scope AND the
#   unchanged Phase A PA-3 / MVP OR rewrite for the reader-reject
#   `live` scope in main():
#
#     Case 1 (happy path): the `dead` scope's
#                          `qbool dead = a | b;` VarDecl is REPLACED
#                          by empty text.  No `uncompute_or(dead, ...)`
#                          is planted at scope close — PJ-4a's
#                          eliminated_stmt_ranges entry suppresses the
#                          MVP OR matcher via
#                          `apply_eliminated_stmt_guards`.
#
#     Case 2 (reject):     the `live` scope's
#                          `qbool live = a | b;` forward decl is
#                          PRESERVED.  The Phase A PA-3 forward
#                          `x ^= live;` is preserved, a PA-3 LIFO
#                          self-adjoint `x ^= live;` inverse is planted
#                          before scope close, and an MVP OR
#                          `uncompute_or(live, a, b);` follows (LIFO
#                          order).
#
#   AND the source file must be byte-identical before and after the
#   transpile (the transpiler never rewrites its input in place — it
#   only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED eliminates the Case 1 decl inside main()'s body:
#        - `qbool dead = a | b;` does NOT appear anywhere after
#          `int main()` in the generated file.  The PJ-4a
#          QReplacement over the decl's full stmt range (including
#          the trailing `;`) must strip the line verbatim.
#   3. GENERATED does NOT plant an `uncompute_or(dead, a, b);` in
#      Case 1's scope — the `eliminated_stmt_ranges` entry makes the
#      MVP OR matcher early-return via `apply_eliminated_stmt_guards`.
#      Searched after `int main()` so the top-of-file comment block's
#      prose mentions cannot false-positive.
#   4. GENERATED preserves Case 2's forward pair inside main()'s body:
#        - `qbool live = a | b;` appears after `int main()`.
#        - `x ^= live;` appears after the `live` decl.
#   5. GENERATED plants the Case 2 Phase A PA-3 LIFO self-adjoint
#      inverse AND the MVP OR uncompute AFTER the forward pair:
#        - A second `x ^= live;` appears AFTER the first.
#        - `uncompute_or(live, a, b);` appears AFTER the forward
#          `qbool live = a | b;` decl.
#   6. SOURCE does NOT contain the `uncompute_or(live, a, b);`
#      injected fragment after `int main()` — the transpiler must
#      not contaminate its input.  The top-of-file comment block
#      discusses the rewrite shape in prose by bare name, so we
#      anchor the "no-leak" search after `int main()` to avoid
#      false-positives.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_dead_ancilla: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_dead_ancilla: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_dead_ancilla: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_dead_ancilla: source file missing: ${SOURCE}")
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
        "check_example_dead_ancilla: generated file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${gen_content}" ${gen_main_offset} -1 gen_body)

# ── Assertion 2: Case 1 — `qbool dead = a | b;` is GONE ───────────────────
# The PJ-4a QReplacement's SourceRange spans the VarDecl's begin through
# its terminating `;`, so `Rewriter.ReplaceText` must strip the entire
# line.  A surviving match of the decl anywhere inside main()'s body
# signals a replacement regression (raw insertions ordered before
# replacements, Clang Rewriter dropped the edit silently, PJ-4a
# registration out of order relative to Phase A/E, etc.).
string(REGEX MATCH "qbool[ \t]+dead[ \t]*=[ \t]*a[ \t]*\\|[ \t]*b[ \t]*;"
       dead_decl_survived "${gen_body}")
if(dead_decl_survived)
    message(FATAL_ERROR
        "check_example_dead_ancilla: the Case 1 "
        "`qbool dead = a | b;` VarDecl survived inside main()'s body "
        "of the generated file — the Phase J PJ-4a QReplacement did "
        "not strip the dead decl.\n"
        "  match: ${dead_decl_survived}")
endif()

# ── Assertion 3: Case 1 — NO `uncompute_or(dead, a, b);` at scope close ───
# The PJ-4a matcher pushes the decl's range into
# `eliminated_stmt_ranges`; the `apply_eliminated_stmt_guards` cleanup
# pass then removes any QOperation (MVP OR's scheduled uncompute in
# particular) whose `stmt_range` lies inside an eliminated entry.
# A false-positive here would mean PJ-4a's range publication broke
# or the cleanup pass regressed.
string(REGEX MATCH "uncompute_or[ \t]*\\([ \t]*dead[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)[ \t]*;"
       dead_uncompute_hit "${gen_body}")
if(dead_uncompute_hit)
    message(FATAL_ERROR
        "check_example_dead_ancilla: found an injected "
        "`uncompute_or(dead, a, b);` inside main()'s body of the "
        "generated file — but the Case 1 decl was supposed to be "
        "eliminated by PJ-4a, so no uncompute should land.\n"
        "  match: ${dead_uncompute_hit}")
endif()

# ── Assertion 4a: Case 2 — forward `qbool live = a | b;` preserved ────────
string(FIND "${gen_body}" "qbool live = a | b;" live_decl_offset)
if(live_decl_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_dead_ancilla: expected forward "
        "`qbool live = a | b;` inside main()'s body, but it was not "
        "found — the PJ-4a matcher must leave the reader-reject "
        "`live` decl untouched.\n  file: ${GENERATED}")
endif()

# ── Assertion 4b: Case 2 — forward `x ^= live;` preserved AFTER decl ──────
string(LENGTH "qbool live = a | b;" _live_decl_len)
math(EXPR live_tail_start "${live_decl_offset} + ${_live_decl_len}")
string(SUBSTRING "${gen_body}" ${live_tail_start} -1 live_tail)

string(FIND "${live_tail}" "x ^= live;" first_xor_offset)
if(first_xor_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_dead_ancilla: expected forward "
        "`x ^= live;` AFTER the `qbool live = a | b;` decl inside "
        "main()'s body, but it was not found.\n"
        "  tail: <<<${live_tail}>>>")
endif()

# ── Assertion 5a: Case 2 — PA-3 LIFO self-adjoint `x ^= live;` inverse ────
# The PA-3 `^=` matcher schedules its self-adjoint inverse directly
# (the op is its own adjoint in this shape).  Anchor the search
# AFTER the first `x ^= live;` so we measure a SECOND occurrence.
string(LENGTH "x ^= live;" _xor_len)
math(EXPR second_xor_start "${live_tail_start} + ${first_xor_offset} + ${_xor_len}")
string(SUBSTRING "${gen_body}" ${second_xor_start} -1 post_xor_tail)

string(FIND "${post_xor_tail}" "x ^= live;" second_xor_offset)
if(second_xor_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_dead_ancilla: expected a SECOND "
        "`x ^= live;` (the PA-3 LIFO self-adjoint inverse) AFTER the "
        "forward `x ^= live;` inside main()'s body, but only ONE "
        "instance was found — the Phase A PA-3 matcher did not plant "
        "its self-adjoint inverse.\n"
        "  tail: <<<${post_xor_tail}>>>")
endif()

# ── Assertion 5b: Case 2 — MVP OR `uncompute_or(live, a, b);` at scope close
# The MVP OR matcher schedules `uncompute_or(live, a, b);` against the
# enclosing QScope; the M8 uncompute-synthesis pass plants it before
# the scope's closing `}`.  Anchor the search AFTER the forward decl
# so we cannot collide with any possible earlier occurrence.
string(FIND "${live_tail}" "uncompute_or(live, a, b)"
       uncompute_live_offset)
if(uncompute_live_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_dead_ancilla: expected injected "
        "`uncompute_or(live, a, b);` AFTER the forward "
        "`qbool live = a | b;` decl inside main()'s body, but it was "
        "not found — the MVP OR matcher + M8 uncompute pass did not "
        "plant the Case 2 uncompute.\n"
        "  tail: <<<${live_tail}>>>")
endif()

# ── Assertion 6: SOURCE contains no injected fragment after main() ────────
# The source's top-of-file comment block mentions `uncompute_or` and
# `x ^= live` by bare name in prose.  The full injected fragment
# spellings below never appear outside the generated sibling.  Anchor
# the "no-leak" search after `int main()` to avoid false-positives on
# the documentation prose.  The transpiler must never rewrite its
# input in place; examples/dead_ancilla.cpp must be byte-identical
# before and after the transpile.
string(FIND "${src_content}" "int main()" src_main_offset)
if(src_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_dead_ancilla: source file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${src_content}" ${src_main_offset} -1 src_body)

string(REGEX MATCH "uncompute_or[ \t]*\\([ \t]*live[ \t]*,[ \t]*a[ \t]*,[ \t]*b[ \t]*\\)[ \t]*;"
       src_hit "${src_body}")
if(src_hit)
    message(FATAL_ERROR
        "check_example_dead_ancilla: an injected fragment matched "
        "`uncompute_or(live, a, b);` appeared in SOURCE ${SOURCE} "
        "after `int main()`.\nThe transpiler must never rewrite its "
        "input in place; examples/dead_ancilla.cpp must be "
        "byte-identical before and after the transpile.")
endif()

message(STATUS
    "check_example_dead_ancilla: OK — generated file has the PJ-4a "
    "dead-decl elimination for Case 1 (no `qbool dead = a | b;`, no "
    "`uncompute_or(dead, a, b);` at scope close), the Phase A PA-3 + "
    "MVP OR rewrite for Case 2 (`qbool live = a | b;` + `x ^= live;` "
    "preserved, second `x ^= live;` LIFO inverse + "
    "`uncompute_or(live, a, b);` planted at scope close), and the "
    "source is byte-identical")
