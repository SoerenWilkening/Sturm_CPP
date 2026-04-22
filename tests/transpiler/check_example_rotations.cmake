# check_example_rotations.cmake — Phase N PN-7 ctest driver.
#
# Invoked by tests/transpiler/CMakeLists.txt with:
#   -DSOURCE=<abs path>    — examples/rotations.cpp
#   -DGENERATED=<abs path> — the transpiler-generated sibling under
#                            <CMAKE_BINARY_DIR>/sturm_gen/examples/
#                            rotations.cpp
#
# Contract:
#   After a clean build that routes examples/rotations.cpp through
#   sturm-transpile, the generated file must reflect the current Phase N
#   pipeline emission for the four rotation compound-assigns plus the
#   depth-1 WHEN-guarded rotation inside main()'s inner scope:
#
#     Source shape (source-file layout preserved inside main()):
#         {
#             qint_t<1> a;
#             qint_t<1> b;
#             qbool c(0.5);
#             a.theta() += 0.3;
#             a.theta() -= 0.1;
#             b.phi()   += 0.7;
#             b.phi()   -= 0.2;
#             WHEN(c) {
#                 a.theta() += 0.5;
#             }
#         }
#
#     Generated rewrite (current pipeline):
#         {
#             qint_t<1> a;
#             qint_t<1> b;
#             qbool c(0.5);
#             a.theta() += 0.3;           // PN-2a forward — preserved
#             a.theta() -= 0.1;           // PN-2b forward — preserved
#             b.phi() += 0.7;             // PN-2c forward — preserved
#             b.phi() -= 0.2;             // PN-2d forward — preserved
#             WHEN(c) {
#                 a.theta() += 0.5;       // PN-2a forward (depth-1) — preserved
#                 a.theta() -= 0.5;       // PN-4 inverse INSIDE WHEN body
#             }
#             b.phi() += 0.2;             // PN-4 dual (for b.phi() -= 0.2)
#             b.phi() -= 0.7;             // PN-4 dual (for b.phi() += 0.7)
#             a.theta() += 0.1;           // PN-4 dual (for a.theta() -= 0.1)
#             a.theta() -= 0.3;           // PN-4 dual (for a.theta() += 0.3)
#         }
#
#   The five injected lines land in LIFO (reverse-source) order.  The
#   depth-1 WHEN-guarded rotation's inverse lands INSIDE the WHEN body
#   (not after its closing brace) because the PN-2 matcher anchored its
#   QOperation on the inner WHEN body's QScope, so the PN-4 uncompute
#   pass emits the inverse when THAT scope closes — preserving the
#   B5 invariant (the control scope covers both forward and inverse
#   rotations).
#
#   AND the source file must be byte-identical before and after the
#   transpile (the transpiler never rewrites its input in place — it
#   only writes into sturm_gen/).
#
# Assertions:
#   1. GENERATED exists on disk.
#   2. GENERATED contains the four user-written forward rotations
#      preserved verbatim inside main()'s body: `a.theta() += 0.3;`,
#      `a.theta() -= 0.1;`, `b.phi() += 0.7;`, `b.phi() -= 0.2;`
#      (anchored after `int main()` so the top-of-file prose mentions
#      do not false-positive).
#   3. GENERATED contains the PN-4 LIFO inverse chain for the four
#      outer-scope rotations in reverse-source order:
#         `b.phi() += 0.2;` → `b.phi() -= 0.7;` →
#         `a.theta() += 0.1;` → `a.theta() -= 0.3;`
#      each AFTER the final forward rotation / WHEN body, and each
#      appearing BEFORE the next inverse in the chain.
#   4. GENERATED contains the depth-1 WHEN-guarded forward rotation
#      `a.theta() += 0.5;` followed by its in-body inverse
#      `a.theta() -= 0.5;` — both INSIDE the WHEN body, so the
#      controlled scope covers both directions (B5 invariant).
#   5. SOURCE does NOT contain any of the PN-4 injected inverse
#      fragments AFTER `int main()` — the transpiler must not
#      contaminate its input.  We anchor on `int main()` so the
#      top-of-file comment block's prose mentions (which spell
#      `a.theta() -= 0.3;` etc. in its before/after schematic) cannot
#      false-positive.

if(NOT DEFINED SOURCE)
    message(FATAL_ERROR "check_example_rotations: SOURCE not set")
endif()
if(NOT DEFINED GENERATED)
    message(FATAL_ERROR "check_example_rotations: GENERATED not set")
endif()

if(NOT EXISTS "${GENERATED}")
    message(FATAL_ERROR
        "check_example_rotations: generated file missing: ${GENERATED}")
endif()
if(NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR
        "check_example_rotations: source file missing: ${SOURCE}")
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
        "check_example_rotations: generated file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${gen_content}" ${gen_main_offset} -1 gen_body)

# ── Assertion 2: user-written forward rotations preserved verbatim ─────────
# PN-2 anchors a QOperation on each compound-assign; the forward text
# is passed through unchanged (no rewrite).  All four outer-scope
# rotations must survive verbatim inside main()'s body.  A failure
# here would signal that the PN-2 matcher's forward-path pass-through
# regressed — the matcher stages the op on the QScope but does NOT
# schedule a QReplacement on the forward range (same policy as
# Phase B's `a += 3;` matcher).
string(REGEX MATCH "a[ \t]*\\.[ \t]*theta[ \t]*\\([ \t]*\\)[ \t]*\\+=[ \t]*0\\.3[ \t]*;"
       theta_add_fwd "${gen_body}")
if(NOT theta_add_fwd)
    message(FATAL_ERROR
        "check_example_rotations: the user-written "
        "`a.theta() += 0.3;` stmt did NOT survive in main()'s body of "
        "the generated file — a PN-2a forward-path regression stripped "
        "the forward text unexpectedly.")
endif()

string(REGEX MATCH "a[ \t]*\\.[ \t]*theta[ \t]*\\([ \t]*\\)[ \t]*-=[ \t]*0\\.1[ \t]*;"
       theta_sub_fwd "${gen_body}")
if(NOT theta_sub_fwd)
    message(FATAL_ERROR
        "check_example_rotations: the user-written "
        "`a.theta() -= 0.1;` stmt did NOT survive in main()'s body of "
        "the generated file — a PN-2b forward-path regression stripped "
        "the forward text unexpectedly.")
endif()

string(REGEX MATCH "b[ \t]*\\.[ \t]*phi[ \t]*\\([ \t]*\\)[ \t]*\\+=[ \t]*0\\.7[ \t]*;"
       phi_add_fwd "${gen_body}")
if(NOT phi_add_fwd)
    message(FATAL_ERROR
        "check_example_rotations: the user-written "
        "`b.phi() += 0.7;` stmt did NOT survive in main()'s body of "
        "the generated file — a PN-2c forward-path regression stripped "
        "the forward text unexpectedly.")
endif()

string(REGEX MATCH "b[ \t]*\\.[ \t]*phi[ \t]*\\([ \t]*\\)[ \t]*-=[ \t]*0\\.2[ \t]*;"
       phi_sub_fwd "${gen_body}")
if(NOT phi_sub_fwd)
    message(FATAL_ERROR
        "check_example_rotations: the user-written "
        "`b.phi() -= 0.2;` stmt did NOT survive in main()'s body of "
        "the generated file — a PN-2d forward-path regression stripped "
        "the forward text unexpectedly.")
endif()

# ── Assertion 4: depth-1 WHEN-guarded rotation + in-body inverse ──────────
# The PN-2a matcher anchors a THETA_ADD_ASSIGN_CONST QOperation on
# the inner WHEN body's QScope (not the enclosing inner scope), so
# the PN-4 uncompute pass emits the `a.theta() -= 0.5;` inverse
# INSIDE the WHEN body — the control scope covers both directions
# (B5 invariant: depth-1 controlled rotations have their inverse in
# the same controlled scope).
#
# We verify order: the forward `a.theta() += 0.5;` must appear
# BEFORE the in-body inverse `a.theta() -= 0.5;` inside main()'s body.
string(FIND "${gen_body}" "a.theta() += 0.5" when_fwd_offset)
if(when_fwd_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_rotations: the user-written depth-1 WHEN-guarded "
        "`a.theta() += 0.5;` stmt did NOT survive in main()'s body of "
        "the generated file — PN-2a forward-path regressed inside the "
        "WHEN body.")
endif()

string(LENGTH "a.theta() += 0.5" _when_fwd_len)
math(EXPR post_when_fwd "${when_fwd_offset} + ${_when_fwd_len}")
string(SUBSTRING "${gen_body}" ${post_when_fwd} -1 post_when_fwd_tail)

string(FIND "${post_when_fwd_tail}" "a.theta() -= 0.5" when_inv_offset)
if(when_inv_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_rotations: the PN-4 in-body inverse "
        "`a.theta() -= 0.5;` was NOT emitted after the user-written "
        "`a.theta() += 0.5;` inside the WHEN body — the PN-4 uncompute "
        "arm for THETA_ADD_ASSIGN_CONST did not fire, or the "
        "enclosing-scope lookup placed the inverse outside the WHEN "
        "body (which would violate the B5 single-control-scope "
        "invariant for controlled rotations).\n"
        "  tail: <<<${post_when_fwd_tail}>>>")
endif()

# ── Assertion 3: PN-4 LIFO inverse chain for the four outer rotations ──────
# The PN-4 uncompute pass emits the four dual compound-assigns in
# LIFO (reverse-source) order before the inner scope's closing `}`.
# The chain lands AFTER the WHEN body's closing brace (because the
# WHEN-guarded rotation's inverse is IN the WHEN body, not here) and
# in the exact reverse-source order:
#     `b.phi() += 0.2;` → dual of `b.phi() -= 0.2;`
#     `b.phi() -= 0.7;` → dual of `b.phi() += 0.7;`
#     `a.theta() += 0.1;` → dual of `a.theta() -= 0.1;`
#     `a.theta() -= 0.3;` → dual of `a.theta() += 0.3;`
#
# Anchor all four searches after the in-body inverse `a.theta() -= 0.5;`
# so a false-positive on the forward `a.theta() -= 0.1;` (which
# textually appears BEFORE the LIFO chain) cannot match.
string(LENGTH "a.theta() -= 0.5" _when_inv_len)
math(EXPR post_when_inv "${post_when_fwd} + ${when_inv_offset} + ${_when_inv_len}")
string(SUBSTRING "${gen_body}" ${post_when_inv} -1 lifo_tail)

string(FIND "${lifo_tail}" "b.phi() += 0.2" phi_sub_inv_offset)
if(phi_sub_inv_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_rotations: the PN-4 inverse "
        "`b.phi() += 0.2;` was NOT emitted after the WHEN body — "
        "the PN-4 uncompute arm for PHI_SUB_ASSIGN_CONST did not "
        "fire at scope close.\n"
        "  tail: <<<${lifo_tail}>>>")
endif()

string(FIND "${lifo_tail}" "b.phi() -= 0.7" phi_add_inv_offset)
if(phi_add_inv_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_rotations: the PN-4 inverse "
        "`b.phi() -= 0.7;` was NOT emitted after the WHEN body — "
        "the PN-4 uncompute arm for PHI_ADD_ASSIGN_CONST did not "
        "fire at scope close.\n"
        "  tail: <<<${lifo_tail}>>>")
endif()

string(FIND "${lifo_tail}" "a.theta() += 0.1" theta_sub_inv_offset)
if(theta_sub_inv_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_rotations: the PN-4 inverse "
        "`a.theta() += 0.1;` was NOT emitted after the WHEN body — "
        "the PN-4 uncompute arm for THETA_SUB_ASSIGN_CONST did not "
        "fire at scope close.\n"
        "  tail: <<<${lifo_tail}>>>")
endif()

string(FIND "${lifo_tail}" "a.theta() -= 0.3" theta_add_inv_offset)
if(theta_add_inv_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_rotations: the PN-4 inverse "
        "`a.theta() -= 0.3;` was NOT emitted after the WHEN body — "
        "the PN-4 uncompute arm for THETA_ADD_ASSIGN_CONST did not "
        "fire at scope close.\n"
        "  tail: <<<${lifo_tail}>>>")
endif()

# LIFO order contract: `b.phi() += 0.2;` (dual of the LAST forward)
# must land BEFORE `b.phi() -= 0.7;`, which must land BEFORE
# `a.theta() += 0.1;`, which must land BEFORE `a.theta() -= 0.3;`
# (dual of the FIRST forward). Each offset is relative to
# `lifo_tail` so comparing them directly checks source order.
if(NOT phi_add_inv_offset GREATER phi_sub_inv_offset)
    message(FATAL_ERROR
        "check_example_rotations: the LIFO inverse order is wrong. "
        "`b.phi() += 0.2;` must land BEFORE `b.phi() -= 0.7;`.\n"
        "  b.phi() += 0.2 offset: ${phi_sub_inv_offset}\n"
        "  b.phi() -= 0.7 offset: ${phi_add_inv_offset}")
endif()

if(NOT theta_sub_inv_offset GREATER phi_add_inv_offset)
    message(FATAL_ERROR
        "check_example_rotations: the LIFO inverse order is wrong. "
        "`b.phi() -= 0.7;` must land BEFORE `a.theta() += 0.1;`.\n"
        "  b.phi() -= 0.7   offset: ${phi_add_inv_offset}\n"
        "  a.theta() += 0.1 offset: ${theta_sub_inv_offset}")
endif()

if(NOT theta_add_inv_offset GREATER theta_sub_inv_offset)
    message(FATAL_ERROR
        "check_example_rotations: the LIFO inverse order is wrong. "
        "`a.theta() += 0.1;` must land BEFORE `a.theta() -= 0.3;`.\n"
        "  a.theta() += 0.1 offset: ${theta_sub_inv_offset}\n"
        "  a.theta() -= 0.3 offset: ${theta_add_inv_offset}")
endif()

# ── Assertion 5: SOURCE contains no injected fragment after main() ────────
# The source's top-of-file comment block mentions injected spellings
# like `a.theta() -= 0.3;` in its before/after schematic prose.  The
# full injected dual-compound-assign lines never appear outside the
# generated sibling (the user did not write them by hand).  Anchor
# the no-leak search after `int main()` so the documentation prose
# cannot false-positive.  The transpiler must never rewrite its
# input in place; examples/rotations.cpp must be byte-identical
# before and after the transpile.
string(FIND "${src_content}" "int main()" src_main_offset)
if(src_main_offset EQUAL -1)
    message(FATAL_ERROR
        "check_example_rotations: source file is missing "
        "`int main()` — file is malformed.")
endif()
string(SUBSTRING "${src_content}" ${src_main_offset} -1 src_body)

# Probe for the FIRST LIFO inverse line (`a.theta() -= 0.3;`) that
# the PN-4 pass would have emitted.  If this fragment leaks into
# the SOURCE after `int main()`, the transpiler contaminated its
# input — a contract violation.
string(REGEX MATCH "a[ \t]*\\.[ \t]*theta[ \t]*\\([ \t]*\\)[ \t]*-=[ \t]*0\\.3[ \t]*;"
       src_theta_dual_hit "${src_body}")
if(src_theta_dual_hit)
    message(FATAL_ERROR
        "check_example_rotations: an injected fragment "
        "matched `a.theta() -= 0.3;` appeared in SOURCE "
        "${SOURCE} after `int main()`.\n"
        "The transpiler must never rewrite its input in place; "
        "examples/rotations.cpp must be byte-identical before and "
        "after the transpile.")
endif()

string(REGEX MATCH "b[ \t]*\\.[ \t]*phi[ \t]*\\([ \t]*\\)[ \t]*-=[ \t]*0\\.7[ \t]*;"
       src_phi_dual_hit "${src_body}")
if(src_phi_dual_hit)
    message(FATAL_ERROR
        "check_example_rotations: an injected fragment "
        "matched `b.phi() -= 0.7;` appeared in SOURCE "
        "${SOURCE} after `int main()`.\n"
        "The transpiler must never rewrite its input in place; "
        "examples/rotations.cpp must be byte-identical before and "
        "after the transpile.")
endif()

message(STATUS
    "check_example_rotations: OK — generated file reflects the "
    "current Phase N pipeline emission (four PN-2 forward rotations "
    "preserved verbatim, depth-1 WHEN-guarded rotation + in-body "
    "PN-4 inverse, four-line LIFO dual chain at scope close), and "
    "the source is byte-identical")
