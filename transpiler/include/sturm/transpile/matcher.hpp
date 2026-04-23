// matcher.hpp — Clang AST matcher for the MVP uncompute pattern (M7).
//
// Purpose
// -------
// This module registers a single Clang ASTMatcher whose job is to identify
// every MVP "qbool tmp = a | b;" assignment in a translation unit and record
// it as a QOperation in a caller-supplied QUnit.
//
// The matcher is strictly scoped to the MVP:
//
//   - The declared variable must be a `qbool` (matched by class name only;
//     this covers both the `sturm::qbool` class and its typedef form).
//   - The initializer must be `operator|` with exactly two arguments.
//   - Both arguments must be plain DeclRefExprs (references to named qbool
//     parameters / locals in the enclosing scope).
//
// Anything broader — `a & b`, `qint tmp = ...`, `foo(a, b)`, compound
// initializers, template-dependent contexts — deliberately does NOT match.
// Widening the matcher is a Phase A/B roadmap deliverable, not this module.
//
// On match the callback synthesizes one `QOperation{kind=OR, result, operands,
// stmt_range}` and appends it to the QScope that corresponds to the matched
// VarDecl's enclosing CompoundStmt. Sibling compound statements become sibling
// QScopes; each QScope is created lazily the first time the matcher fires
// inside it, and subsequent matches in the same block reuse it.
//
// Contract
// --------
//   - The caller owns the QUnit and keeps it alive for the duration of
//     the MatchFinder's run. The matcher holds a raw pointer; no copy.
//   - `register_or_matcher` must be called exactly once per QUnit. Calling
//     it twice with the same QUnit would double-count matches.
//
// Dependencies: Clang ASTMatchers only. The header drags in LibTooling's
// matcher-finder types because the caller needs to instantiate a MatchFinder
// before registering. We keep the surface tiny so downstream modules do not
// transitively pick up the full Clang AST library through this header.

#ifndef STURM_TRANSPILE_MATCHER_HPP
#define STURM_TRANSPILE_MATCHER_HPP

#include "sturm/transpile/qir.hpp"

#include "clang/ASTMatchers/ASTMatchFinder.h"

namespace clang {
class DiagnosticsEngine;
class SourceManager;
} // namespace clang

namespace sturm::transpile {

// PM3-2: forward declaration so the PH-3 guard's registration helper
// can accept a `DiagContext&` argument without dragging the full
// `transpiler/src/diag_context.hpp` into the public matcher header.
struct DiagContext;

/// Register the MVP `qbool tmp = a | b;` matcher against `finder`, directing
/// every match into `unit`. `unit` must outlive the MatchFinder's run. Call
/// at most once per QUnit.
void register_or_matcher(clang::ast_matchers::MatchFinder& finder,
                         QUnit& unit);

/// Phase A / PA-1: Register the `qbool tmp = ~a;` self-inverse matcher.
/// On match, appends a QOperation{kind=NOT, result, operands=[a]} to the
/// scope corresponding to the VarDecl's enclosing CompoundStmt. The M8
/// pass emits `<result> = ~<result>;` as the inverse (NOT is self-inverse
/// when applied to the result qubit). Contract mirrors register_or_matcher.
void register_not_matcher(clang::ast_matchers::MatchFinder& finder,
                          QUnit& unit);

/// Phase A / PA-2: Register the `qbool tmp = a ^ b;` self-inverse matcher.
/// On match, appends a QOperation{kind=XOR, result, operands=[a, b]} to
/// the scope corresponding to the VarDecl's enclosing CompoundStmt. The
/// M8 pass emits the two-line self-inverse `<result> ^= <a>;` then
/// `<result> ^= <b>;` — exploiting (a^b)^a^b = 0.
void register_xor_matcher(clang::ast_matchers::MatchFinder& finder,
                          QUnit& unit);

/// Phase A / PA-3: Register the `a ^= b;` quantum-operand self-inverse
/// matcher. Unlike PA-1/PA-2 which match VarDecl initializers, this
/// matches a bare compound-assignment statement. On match, records
/// QOperation{kind=XOR_ASSIGN, result=a, operands=[b]}; the M8 pass
/// re-emits `<a> ^= <b>;` at scope exit (self-adjoint under XOR).
/// RHS must be a DeclRefExpr — classical constants are PA-4's scope.
void register_xor_assign_matcher(clang::ast_matchers::MatchFinder& finder,
                                 QUnit& unit);

/// Phase A / PA-4: Register the `a ^= <expr>;` classical-operand matcher.
/// Covers literals (`1`, `true`, `0xff`) and compound classical
/// expressions (`x & y`) — any RHS whose post-implicit-cast form is not
/// a DeclRefExpr. On match, extracts the verbatim source text of the
/// RHS via Lexer::getSourceText and stores it in the IR as
/// QValueRef{name=<text>, decl_loc=invalid}. Shares QOpKind::XOR_ASSIGN
/// with PA-3; the render switch needs no additional case because the
/// operand name is embedded verbatim regardless of whether it came from
/// a DeclRefExpr or a literal.
void register_xor_assign_classical_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Phase B / PB-1..PB-4: Register the four `a <op>= <classical>;` matchers
/// for `+= -= *= /=` against a `qint_t<W>` LHS with a non-qint RHS that
/// reaches the operator via the non-explicit `qint_t(int64_t)` converting
/// constructor (qint_core.hpp:89). Each matcher peels one extra
/// CXXConstructExpr layer beyond PA-4 to reach the classical source text,
/// extracts it verbatim via Lexer::getSourceText, and records one
/// QOperation of the matching ADD/SUB/MUL/DIV_ASSIGN_CONST kind. The LHS
/// type guard on `qint_t` is what separates PB from a future Phase C
/// qint-qint matcher (where the RHS is a DeclRefExpr and no converting
/// constructor fires).
void register_add_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_sub_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_mul_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_div_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Phase N / PN-2: Register the four `q.theta() <op>= d;` / `q.phi() <op>= d;`
/// rotation matchers for `+=` / `-=` against a `qint_t<W>` LHS whose proxy
/// method (`theta()` / `phi()`) returns a `ThetaProxy` / `PhiProxy` value.
/// The AST anchor is **one level deeper** than Phase B: the LHS of the outer
/// `cxxOperatorCallExpr` is a `cxxMemberCallExpr` whose callee is a
/// `cxxMethodDecl(hasName("theta"))` (or `"phi"`) and whose implicit-object
/// argument peels to a `declRefExpr` of `qint_t` type. The RHS is a plain
/// `double`-valued `Expr` — no `CXXConstructExpr` peel is needed because the
/// proxy's `operator+=` takes `double` by value, not a converting ctor. The
/// RHS source text is captured verbatim via `Lexer::getSourceText` (same
/// Phase B policy) and stored as the operand name, with `decl_loc` invalid.
///
/// Each matcher records one QOperation of the matching THETA/PHI
/// _ADD/SUB_ASSIGN_CONST kind; the M8 pass (PN-4) emits the sign-flipped
/// `q.theta() -= d;` (or `+=` for the `SUB` kinds) as the inverse, inline,
/// with no `uncompute_api.hpp` free-function helper — the runtime's
/// `ThetaProxy::operator-=(double)` / `PhiProxy::operator-=(double)` at
/// `include/sturm/qtypes/qint_core.hpp:305,372` are self-dual and forward
/// to `operator+=(-delta)` for sign-agreement on the counter-mode
/// `GateRecord` stream.
///
/// Multi-control rotations are **out of scope** (depth ≥ 2 under nested
/// `WHEN` guards). Phase G AND-fold collapses nested `WHEN` chains to
/// depth 1 before this matcher ever sees the rotation; the example fixture
/// (`examples/rotations.cpp`, PN-7) pins the depth-1 invariant.
///
/// Dogfood: these four registrars are bundled into a `PNRotationPlugin`
/// class in an anonymous namespace at file scope in `matcher_rotation.cpp`
/// and announced via `STURM_REGISTER_PLUGIN(PNRotationPlugin)`. The
/// `TranspileConsumer`'s Registry drain (PM4-3 / PM4-6) picks them up at
/// static-init time and registers them against the shared `finder_` in the
/// same block as the PB-1..PB-4 dogfood — so no consumer-side edit is
/// needed to wire these matchers into the pipeline.
///
/// See `docs/implementation_plan_transpiler_phase_n.md` §3 for the full
/// AST-shape derivation and the registration-order contract.
void register_theta_add_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_theta_sub_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_phi_add_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_phi_sub_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Phase C / PC-1..PC-5: Register the five `a <op>= b;` matchers for
/// `+= -= *= /= %=` against a `qint_t<W>` LHS with another `qint_t<W>` RHS
/// (a bare DeclRefExpr — no converting constructor fires, so the RHS has
/// no CXXConstructExpr wrapper). Structurally disjoint from the Phase B
/// matchers above: PB requires a CXXConstructExpr peel; PC requires a
/// DeclRefExpr RHS, so no CXXOperatorCallExpr can trigger both. Each
/// matcher records one QOperation of the matching ADD/SUB/MUL/DIV/
/// MOD_ASSIGN_QINT kind; the M8 pass emits a single
/// `uncompute_{add,sub,mul,div,mod}_qint(lhs, rhs);` line as inverse.
void register_add_assign_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_sub_assign_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_mul_assign_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_div_assign_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_mod_assign_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Phase D / PD-1..PD-6: Register the six `qbool c = a OP b;` matchers for
/// `== != < <= > >=` against two `qint_t<W>` operands (both bare
/// DeclRefExprs). These are VarDecl-initializer matchers — the declared
/// variable must be `qbool`, and the initializer is a CXXOperatorCallExpr
/// whose overloaded operator is the corresponding comparison. Both
/// arguments are guarded by hasCanonicalType+hasDeclaration → qint_t, so
/// an `int == int` or `qbool == qbool` compare does NOT match.
/// Each matcher records one QOperation of the matching EQ/NE/LT/LE/GT/GE
/// _QINT kind with two operands (LHS ident, RHS ident). The M8 pass emits
/// a single `uncompute_{eq,ne,lt,le,gt,ge}_qint(c, a, b);` line as inverse,
/// re-dispatching to the self-adjoint DSL comparators in
/// include/sturm/lib/compare_dsl.hpp.
void register_eq_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_ne_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_lt_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_le_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_gt_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);
void register_ge_compare_qint_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Phase E / PE-4: Register the compound qbool bitwise-expression matcher.
/// Fires on `VarDecl` of type `qbool` whose initializer is a
/// `CXXOperatorCallExpr` on `|` or `&` where AT LEAST ONE argument (after
/// paren + implicit-cast peel) is itself a `CXXOperatorCallExpr` on `|` or
/// `&`. On match, the callback flattens the nested expression tree into a
/// sequence of single-op VarDecls: each interior sub-expression gets a
/// fresh `__stu_t<N>` intermediate name, and the outermost sub-expression
/// keeps the original VarDecl's name. Every flattened sub-expression
/// becomes one `QOperation` in the enclosing `QScope` (OR or AND); the
/// LIFO uncompute pass then produces the correct reverse-order
/// `uncompute_{and,or}` calls at scope close, with no Phase-E-specific
/// scheduler changes needed. The matcher also appends one `QReplacement`
/// to `unit.replacements` covering the original VarDecl's source range,
/// with the flat decl sequence as its replacement text.
///
/// Disjointness contract: the MVP OR matcher
/// (`register_or_matcher`) requires BOTH outer-call arguments to be bare
/// `DeclRefExpr`s, while this matcher requires AT LEAST ONE argument to be
/// a nested op-call — mutually exclusive, so both matchers can be
/// registered without double-binding any VarDecl.
void register_compound_qbool_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Phase F / PF-2: Register the `WHEN(expr) { body }` macro-invocation
/// detection matcher. The matcher anchors on the middle `if` statement in
/// the three-`if` tower the `WHEN` macro expands to — the one whose
/// init-statement declares `_when_val_` with initializer
/// `sturm::detail::materialize_when(arg)` (see include/sturm/control/when.hpp:293).
///
/// Guard: the matched `IfStmt` must live inside a macro *body* expansion
/// (SourceManager::isMacroBodyExpansion) whose immediate-caller macro
/// spelling is the literal token `WHEN`. This defends against a user
/// calling `sturm::detail::materialize_when(...)` directly — the
/// `_when_val_` VarDecl would bind but the macro-body guard rejects it.
///
/// On match this PF-2 callback performs **detection only** — it validates
/// that the `_when_val_` initializer is a `CallExpr` to `materialize_when`,
/// extracts the single argument, and short-circuits when the argument
/// (after `peel_to_payload`) is a bare `DeclRefExpr` to a qbool
/// (named-passthrough — no rewrite is needed because the user already
/// has a named qbool in hand). The rewrite proper lands in PF-3.
///
/// Contract mirrors the other `register_*_matcher` helpers — call at most
/// once per QUnit; the QUnit must outlive the MatchFinder's run.
void register_when_lift_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Test-only instrumentation (Phase F / PF-2). PF-2 is detection-only —
/// the callback does not mutate the `QUnit`, so the PF-2 unit tests need
/// a separate observable to pin down "how many liftable WHEN invocations
/// did the matcher detect on this TU". These two helpers expose the
/// detection counter maintained by `register_when_lift_matcher`'s
/// callback implementation; `reset_when_lift_detection_count_for_test`
/// zeroes the counter between test cases so the tests do not have to
/// thread state across invocations. Production code must not touch
/// either helper — they are intended strictly for the unit-test harness.
int when_lift_detection_count_for_test();
void reset_when_lift_detection_count_for_test();

/// Phase G / PG-1: Register the nested-`WHEN` detection matcher. Anchors on
/// an outer `WHEN(outer) { ... }` IfStmt whose body (after descending through
/// the WHEN macro's three-`if` tower) contains, as a descendant, an inner
/// `WHEN(inner) { ... }` IfStmt of the same shape. Fires only when BOTH the
/// outer and inner `materialize_when` arguments peel (via
/// `detail::peel_to_payload`) to bare `DeclRefExpr`s — the "named + named"
/// shape the Phase G plan targets. Any compound / comparator / unary shape
/// on either side keeps that pair on the runtime path for this slice.
///
/// PG-1 is **detection only**: on a successful pair match the callback
/// increments `when_nested_detection_count_for_test()`'s counter and
/// returns. No `QReplacement`, no `UncomputeInsertion`, and no synthetic
/// `QOperation` are appended — that rewrite logic lands in PG-2 / PG-3.
///
/// Disjointness with `register_when_lift_matcher` (Phase F): the Phase F
/// matcher's named-passthrough short-circuit already early-returns on a
/// bare `DeclRefExpr` WHEN argument without staging any rewrites, so a
/// `WHEN(a) { WHEN(b) { body } }` pair is observed by Phase F but leaves
/// the QUnit untouched; the Phase G matcher layered on top sees the same
/// pair and bumps its own counter. Both matchers can coexist without
/// stepping on each other's output.
///
/// Contract mirrors the other `register_*_matcher` helpers — call at most
/// once per QUnit; the QUnit must outlive the MatchFinder's run.
void register_when_nested_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Test-only instrumentation (Phase G / PG-1). Same rationale as the
/// Phase F helpers above: PG-1 does not mutate the QUnit, so the unit
/// tests need a separate observable to pin down "how many named+named
/// nested-WHEN pairs did the matcher detect on this TU". Production code
/// must not touch either helper.
int when_nested_detection_count_for_test();
void reset_when_nested_detection_count_for_test();

/// Phase H / PH-2: Register the auto-brace-wrap matcher. Anchors on every
/// `forStmt` / `whileStmt` / `ifStmt` whose body (or then / else arm) is a
/// non-compound Stmt at a file (non-macro) spelling AND whose body
/// transitively contains a recognised quantum op (CXXOperatorCallExpr on
/// a qbool / qint operand, a qbool / qint VarDecl, or a WHEN macro
/// expansion). On each match the callback appends two
/// `UncomputeInsertion` records to `QUnit::raw_insertions`:
///
///   - `{` at `body->getBeginLoc()` — the exact loc PH-1's synthetic
///     BracelessBody QScope keys its `open_brace` on.
///   - `}` at `Lexer::getLocForEndOfToken(body->getEndLoc(), 0, sm, lang)`
///     — the loc immediately past the body's terminating token. PH-1's
///     synthetic QScope uses this same loc as its `close_brace`, so the
///     M8 uncompute insertion stacks with the PH-2 `}` insertion under
///     the emitter's reverse-InsertTextBefore ordering and the final
///     output is well-formed `{ ...; uncompute_*(...); }`.
///
/// Macro-expanded inner `if`s (WHEN's three-`if` tower, user-side
/// helper macros) are rejected by the `!begin.isMacroID()` guard so
/// PH-2 never fires on compiler-synthesised scopes.
///
/// Per-scope dedup on the body's begin-loc raw encoding prevents the
/// same body from being wrapped twice when multiple AST patterns
/// (`hasThen` vs `hasElse`, nested if/else) could bind it.
///
/// Contract mirrors the other `register_*_matcher` helpers — call at
/// most once per QUnit; the QUnit must outlive the MatchFinder's run.
/// Unlike the Phase F / G WHEN-lift matchers, PH-2 is ORDERING-
/// INSENSITIVE with respect to the Phase A–G matchers: it only appends
/// to `QUnit::raw_insertions`, which the M8 pass concatenates at the
/// end of its insertion vector. The Phase A–G per-op insertions go
/// into the same vector ahead of the raw insertions, and the emitter's
/// reverse-iteration over `InsertTextBefore` stacks them correctly at
/// co-located anchors.
void register_brace_wrap_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Test-only instrumentation (Phase H / PH-2). Same rationale as the
/// other matchers' `_for_test()` helpers: PH-2 does not mutate
/// `QUnit::scopes`, so the unit tests need a separate observable to
/// pin down "how many braceless bodies did the matcher wrap on this
/// TU". Production code must not touch either helper.
int brace_wrap_detection_count_for_test();
void reset_brace_wrap_detection_count_for_test();

/// Phase H / PH-3: Register the outer-variable-mutation guard matcher.
/// Scans for compound-assign ops (XOR_ASSIGN, *_ASSIGN_CONST,
/// *_ASSIGN_QINT) whose target is declared in an outer scope relative to
/// the mutation site AND where the mutation lives inside a `for`, `while`,
/// `if` (then or else), or `WHEN` body. For every such mutation, the
/// matcher flags the corresponding `QOperation` with `skip_uncompute=true`
/// (so the M8 synthesis pass emits no inverse) and prints a diagnostic
/// to stderr. Automatic uncomputation of an outer-scoped mutation inside
/// a loop/branch/WHEN would require reverse-loop synthesis (contradicts
/// P9): this matcher makes that boundary explicit rather than silently
/// producing semantically-wrong output.
///
/// The matcher is a POST-processor: it must be registered AFTER the
/// Phase A–C compound-assign matchers so that the QOperations those
/// matchers pushed are already in scope by the time this callback runs.
/// Rather than re-matching the AST, the callback scans `unit.scopes` on
/// each MatchFinder run and walks parents from each op's `stmt_range`
/// begin loc to classify it. In practice we anchor on the same AST
/// shapes (CXXOperatorCallExpr on `^=`, `+=`, `-=`, `*=`, `/=`, `%=`
/// with a qbool / qint LHS) and perform the parent-chain walk directly
/// in the callback, which keeps the module self-contained.
///
/// Pairs with the Phase H PH-3 QOperation::skip_uncompute flag
/// introduced in sturm/transpile/qir.hpp — the M8 pass
/// (uncompute_pass.cpp) skips flagged ops, and dump() renders a
/// `[skip_uncompute]` suffix so users of the IR can see which ops were
/// flagged. Contract mirrors the other `register_*_matcher` helpers —
/// call at most once per QUnit; the QUnit must outlive the MatchFinder's
/// run.
///
/// PM3-2: the matcher's stderr diagnostic is now routed through the
/// shared `DiagContext` so the report lands on the parent
/// CompilerInstance's `DiagnosticsEngine` (the PM3-1
/// `TextDiagnosticPrinter` under the standalone driver, the in-process
/// printer under the plugin). The `diag` reference must outlive the
/// MatchFinder's run; the consumer owns it as a member.
void register_outer_var_guard_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit,
    DiagContext& diag);

/// Test-only instrumentation (Phase H / PH-3). Counts the number of
/// outer-variable mutations the guard matcher has flagged (i.e. the
/// number of `skip_uncompute=true` ops the matcher appended) since the
/// last reset. The unit tests use this counter to discriminate "matcher
/// correctly ignored this mutation" from "matcher correctly flagged
/// this mutation and emitted a diagnostic". Production code must not
/// touch either helper.
int outer_var_guard_detection_count_for_test();
void reset_outer_var_guard_detection_count_for_test();

/// Test-only instrumentation (Phase S / S-B, sturm-ha2k.3). Counts the
/// number of outer-variable for-loop mutations the guard matcher has
/// handed off to Phase S's loop-reversal synthesis path (i.e. the
/// number of ops whose `needs_loop_reversal=true` was set because the
/// enclosing FunctionDecl carries `[[sturm::reversible]]`) since the
/// last reset. Disjoint from `outer_var_guard_detection_count_for_test`
/// — the S-B tests assert that the handoff counter bumps AND that the
/// PH-3 counter stays at zero when the reversible opt-in fires.
int loop_reversal_handoff_count_for_test();
void reset_loop_reversal_handoff_count_for_test();

/// Phase J PJ-3d: Register the uncompute-hoisting matcher. Iterates
/// `unit.scopes` and, for every scope that `detail::classify_scope_kind`
/// identifies as a `LoopBody` (PJ-3a), scans decl-producing ops (OR, AND,
/// NOT, XOR, EQ_QINT, NE_QINT, LT_QINT, LE_QINT, GT_QINT, GE_QINT —
/// NOT compound-assigns on outer vars, which PH-3 already marks with
/// `skip_uncompute=true`) whose operands are ALL loop-invariant per
/// `detail::expr_is_loop_invariant` (PJ-3b). Matching ops have both
/// `hoist_to_override` set to the loop-enclosing scope's close_brace
/// (where the uncompute lands, AFTER the loop end) AND
/// `insert_before_override` set to the loop-begin location (where the
/// forward compute moves, BEFORE the loop begin). A defensive
/// `if (op.skip_uncompute) continue;` preserves PH-3 disjointness —
/// PH-3 already suppresses compound-assign uncomputes and the hoist
/// matcher must never touch those ops.
///
/// Registration order (PJ-3e, tracked in sturm-0v9i): this matcher must
/// run LAST — after every Phase A..I per-op matcher, after the PJ-1f
/// CCX-fuse peephole, and after the PJ-4b dead-ancilla eliminator.
/// Running LAST guarantees the per-op matchers have finished populating
/// `unit.scopes` so this pass can read a fully-formed IR.
///
/// Contract mirrors the other `register_*_matcher` helpers — call at
/// most once per QUnit; the QUnit must outlive the MatchFinder's run.
void register_hoist_invariant_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Test-only instrumentation (Phase J / PJ-3d). Counts the number of
/// ops the matcher has hoisted (i.e. the number of ops whose
/// `hoist_to_override` / `insert_before_override` were set in the
/// current run) since the last reset. The unit tests use this
/// counter to discriminate "matcher correctly skipped this op" from
/// "matcher correctly hoisted this op". Production code must not
/// touch either helper.
int hoist_invariant_detection_count_for_test();
void reset_hoist_invariant_detection_count_for_test();

/// Phase J PJ-1d: Register the zero-ancilla fusion peephole matcher.
/// Anchors on a `qbool __t = a & b;` VarDecl whose initializer is a
/// bare `operator&` call with two DeclRefExpr operands (nested init is
/// rejected — those are handled by the Phase E compound-flatten matcher).
/// On a successful anchor the callback consults the adjacent next
/// statement inside the enclosing CompoundStmt; it fuses the pair iff
/// that stmt is `x ^= __t;` on a qbool target AND the PJ-1a reader-count
/// helper (`detail::count_readers_in_scope`) reports exactly one reader
/// of `__t` within the enclosing scope (namely, the XOR RHS itself).
///
/// On a successful fuse the matcher appends:
///   - one `QReplacement` whose range spans BOTH statements (the
///     VarDecl plus the `^=` statement) with replacement text
///     `ccnot_inplace(x, a, b);` — the forward emission.
///   - one `QOperation{kind=CCNOT_INPLACE, result=x, operands=[a, b]}`
///     to the enclosing QScope so the M8 uncompute pass emits a second
///     `ccnot_inplace(x, a, b);` at scope close (the PJ-1c render case
///     exploits CCX self-adjointness — running the helper twice on the
///     live state returns to identity).
///
/// Fusion-rejected shapes stay on the runtime path: the unmodified
/// `qbool __t = a & b;` VarDecl flows through the Phase E compound
/// matcher, which is registered AFTER this one in `main.cpp` per
/// PJ-1f. The `fused_stmt_ranges` downstream early-return (PJ-1e,
/// tracked separately) prevents double-emission on fused pairs.
///
/// Contract mirrors the other `register_*_matcher` helpers — call at
/// most once per QUnit; the QUnit must outlive the MatchFinder's run.
void register_ccnot_fuse_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Phase J PJ-1e: post-matcher cleanup pass for the zero-ancilla fusion
/// peephole. Walks every `QScope` in `unit` and removes any `QOperation`
/// whose `stmt_range` lies inside some entry of `unit.fused_stmt_ranges`.
///
/// This is a backstop for the early-return guards in
/// `matcher_qbool_assign.cpp` and `matcher_qbool_compound.cpp`: those
/// guards suppress a PA-3 / PA-4 / PE-4 push at the moment the matcher
/// fires ONLY IF `fused_stmt_ranges` was already populated (i.e. the
/// PJ-1d callback ran first). `clang::ast_matchers::MatchFinder::matchAST`
/// does NOT strictly pre-order callbacks across Decl and Stmt matcher
/// pools, so the Stmt-anchored PA-3/PA-4 callbacks sometimes fire BEFORE
/// the Decl-anchored PJ-1d callback within the same translation unit.
/// This cleanup pass restores the invariant by filtering the scope op
/// lists after `matchAST` has completed — at that point every PJ-1d
/// push has landed in `fused_stmt_ranges`, so membership is final.
///
/// Called from `main.cpp` between `finder.matchAST(ctx)` and
/// `synthesize(unit)`. When `unit.fused_stmt_ranges` is empty (every
/// pre-Phase-J snapshot), the pass is a no-op — existing fixtures stay
/// byte-identical.
void apply_fused_stmt_guards(QUnit& unit, const clang::SourceManager& sm);

/// Phase J PJ-4a: Register the dead-ancilla elimination matcher. Anchors
/// on every qbool VarDecl whose initializer is a qbool-operand bitwise
/// op-call (`a | b`, `a & b`, `a ^ b`, `~a`, or compound forms —
/// anything whose post-impl-cast shape is a `CXXOperatorCallExpr` on a
/// qbool bitwise operator). Reuses the PJ-1a `count_readers_in_scope`
/// helper; when the reader count is zero within the decl's enclosing
/// scope, the matcher emits:
///
///   - one `QReplacement{range=<full decl stmt range>, replacement=""}`
///     that deletes the decl verbatim from the rewritten source.
///   - one entry in `QUnit::eliminated_stmt_ranges` covering the same
///     range, so downstream matchers (PA-3 / PA-4 / PE-4 / PA-1 / PA-2
///     / M7 OR) can early-return on any match whose own `stmt_range`
///     lies inside the eliminated range.
///
/// Non-zero reader count (>= 1) is the MVP+PJ-1 path — the decl stays,
/// flows through the Phase A/E matchers as usual, and PJ-1d's fuse
/// peephole still fires if the "exactly one reader" gate holds. PJ-4a
/// is strictly for the reader-count == 0 slice that PJ-1d's gate
/// excludes.
///
/// Registration order (PJ-4b, tracked in sturm-kih8): register BEFORE
/// the Phase A/E per-op matchers (OR, NOT, XOR, compound_qbool, the
/// `^=` xor-assign family). MatchFinder's Decl-vs-Stmt visit
/// interleaving makes the in-callback early-return a best-effort fast
/// path — the authoritative backstop is the
/// `apply_eliminated_stmt_guards` cleanup pass invoked after
/// `matchAST` completes, which removes any op whose `stmt_range` lies
/// inside a recorded eliminated range.
///
/// Contract mirrors the other `register_*_matcher` helpers — call at
/// most once per QUnit; the QUnit must outlive the MatchFinder's run.
void register_dead_ancilla_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Phase J PJ-4a: post-matcher cleanup pass for the dead-ancilla
/// elimination matcher. Mirrors `apply_fused_stmt_guards` exactly,
/// substituting `eliminated_stmt_ranges` for `fused_stmt_ranges`.
/// Walks every `QScope` in `unit` and removes any `QOperation` whose
/// `stmt_range` lies inside some entry of
/// `unit.eliminated_stmt_ranges`. Same rationale: MatchFinder's
/// Decl/Stmt visit-pool interleaving means a Stmt-anchored downstream
/// callback can fire before the Decl-anchored PJ-4a callback that
/// populates `eliminated_stmt_ranges`; this cleanup restores the
/// invariant after `matchAST` completes.
///
/// Called from `main.cpp` between `finder.matchAST(ctx)` and
/// `synthesize(unit)`. When `unit.eliminated_stmt_ranges` is empty
/// (every pre-Phase-J snapshot), the pass is a no-op — existing
/// fixtures stay byte-identical.
void apply_eliminated_stmt_guards(QUnit& unit,
                                  const clang::SourceManager& sm);

/// Phase M PM5-5: Register the peephole gate-reordering matcher. The
/// matcher is a POST-PROCESSOR: it anchors on `translationUnitDecl()`
/// only to capture the `ASTContext&` in `run()`, then does the real
/// work in `onEndOfTranslationUnit()` after every other matcher's
/// callbacks have completed. Anchoring at the TU terminal phase is
/// the same idiom `register_hoist_invariant_matcher` uses (PJ-3d);
/// the pass needs a fully-populated `unit.scopes` in order to reason
/// about adjacent op triples.
///
/// Per-scope pass: for each `QScope` whose classify_scope_kind is
/// `LoopBody` or `Function`, the matcher walks `QScope.ops` looking
/// at adjacent triples `(A, B, C)` by index. A candidate triple is
/// accepted when EVERY one of these four gates passes:
///
///   Gate 1 — kind shape. A is `QOpKind::AND` whose result name starts
///     with the synthetic `__stu_t` prefix. C is `QOpKind::XOR_ASSIGN`
///     whose FIRST operand (the RHS of the user's `x ^= __t;` stmt)
///     has the SAME name AND decl_loc as A's result. B is any other
///     kind except `QOpKind::PLUGIN` (fully opaque — the Registry has
///     no footprint hook in v1) and `QOpKind::USER_ROUTINE` (the body
///     is not re-analysed; every operand collapses to the universal
///     sentinel via the alias extractor, which would refuse anyway).
///
///   Gate 2 — hoist / fuse / eliminated guards. No op in the triple
///     has `hoist_to_override` set (PJ-3 anchors forward/uncompute
///     halves to specific pre-loop/post-loop locations; commuting
///     past them breaks the pairing). No op's `stmt_range` lies
///     inside `unit.fused_stmt_ranges` (PJ-1d has already fused
///     those pairs) or `unit.eliminated_stmt_ranges` (PJ-4a has
///     already deleted those decls). The probe is the same
///     `detail::is_range_covered_by_fused` helper both PJ-1e and
///     PJ-4a's backstops consume.
///
///   Gate 3 — footprint disjointness. Extract `QubitFootprint`s for
///     A's result, every operand of B, and every operand of C
///     (minus the already-matched `__t` read). Call `may_overlap()`
///     pairwise; any overlap between B's operand(s) and A's result
///     OR C's operands refuses the reorder. Uses the PM5-2 / PM5-3
///     alias extractor via `sturm/transpile/alias.hpp`.
///
///   Gate 4 — fuse precondition. The post-reorder `(A, C)` pair must
///     satisfy PJ-1d's `ccnot_fuse` trigger: __t has exactly one
///     reader in the enclosing scope (via
///     `detail::count_readers_in_scope`). If the pair would not fuse
///     after the reorder the reorder has no payoff, so we bail.
///
/// On all four gates passing the matcher emits ONE `QReplacement`
/// covering B's statement and C's statement, replacing it with
/// `C; B;` text (equivalently, moving B past C). B's emitted text is
/// prefixed with a `#line` directive via `format_line_directive()`
/// (PM2-4) so any compile-error diagnostic inside B still cites the
/// user's original B line — the source position changes in the
/// rewritten buffer, but the `#line` points back at the source. This
/// leaves `(A, C)` adjacent in both the scope's op list and the
/// rewritten source text, letting downstream fusion absorb them.
///
/// Single-pass design: one pass per scope; no iterate-to-fixed-point.
/// If a reorder exposes a new triple it will land in the next
/// transpile invocation (PM1's nested pipeline is not re-entered by
/// PM5; the user's build runs sturm-transpile once per TU).
///
/// Registration in `transpile_consumer.cpp` is the PM5-6 concern
/// (must run LAST, after `register_hoist_invariant_matcher`). PM5-5
/// (this issue) ships only the matcher source + the export decl —
/// the consumer-side wiring is orthogonal.
///
/// Contract mirrors the other `register_*_matcher` helpers — call at
/// most once per QUnit; the QUnit must outlive the MatchFinder's run.
void register_peephole_reorder_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit);

/// Test-only instrumentation (Phase M / PM5-5). Counts the number of
/// reorders the matcher has emitted (i.e. the number of `QReplacement`s
/// it pushed into `unit.replacements`) since the last reset. PM5-7's
/// unit tests use this counter to discriminate "matcher correctly
/// refused this triple" from "matcher correctly accepted this triple
/// and emitted a reorder replacement". Production code must not touch
/// either helper.
int peephole_reorder_detection_count_for_test();
void reset_peephole_reorder_detection_count_for_test();

/// PM3-4 / Class 1: Register the WHEN-operand-mutation diagnostic matcher.
/// Anchors on the middle `IfStmt` in the three-`if` tower the `WHEN`
/// macro expands to (same init-stmt pattern as `register_when_lift_
/// matcher`). On match the callback:
///
///   1. Walks the `materialize_when` argument via a
///      `RecursiveASTVisitor`, collecting every `DeclRefExpr` whose
///      referenced `VarDecl` has declared quantum type (`qbool`, `qint`,
///      `qint_t`) into a `SmallPtrSet<const VarDecl*, 4>`. The walk is
///      exhaustive so compound, comparator, ternary, and unary shapes
///      contribute their operands uniformly.
///   2. Descends `IfStmt::getThen()` twice to reach the user's body
///      `CompoundStmt` (pattern copied from `matcher_when_lift.cpp:
///      55-57`).
///   3. Runs a second `RecursiveASTVisitor` over the body looking for
///      `CXXOperatorCallExpr`s with an assignment-shape operator (`=`,
///      `^=`, `+=`, `-=`, `*=`, `/=`, `%=`, `|=`, `&=`) or a
///      `UnaryOperator` with an increment / decrement opcode (`++`,
///      `--`, both pre and post). For every such node whose LHS /
///      operand resolves to a `VarDecl` in the operand set, funnel the
///      mutation's begin loc through `SourceManager::getFileLoc(...)`
///      and call `diag.report_when_operand_mutation(loc, name)`.
///
/// The matcher is advisory only — it does NOT mutate `unit.scopes` or
/// schedule any `QReplacement`. The Error severity on
/// `DiagContext::report_when_operand_mutation` aborts compilation with
/// a non-zero exit; the reviewer sees the mutation line cited in the
/// user's source file (NOT `<memory-buffer>`).
///
/// Contract mirrors the PM3-2 / PM3-3 guard helpers — call at most
/// once per QUnit; the QUnit and DiagContext must outlive the
/// MatchFinder's run.
void register_when_operand_mutation_matcher(
    clang::ast_matchers::MatchFinder& finder,
    QUnit& unit,
    DiagContext& diag);

/// PM3-5 / Class 2: Register the quantum->classical-in-branch-condition
/// diagnostic matcher. Anchors on any explicit cast
/// (`cxxStaticCastExpr`, `cStyleCastExpr`, or `cxxFunctionalCastExpr`)
/// whose destination type is boolean or integral AND whose source
/// operand resolves through `peel_to_payload` to a `DeclRefExpr` to a
/// `sturm::qbool` / `sturm::qint_t` VarDecl. On match the callback
/// walks `ASTContext::getParents` (skipping `ImplicitCastExpr`,
/// `ParenExpr`, `ExprWithCleanups` wrappers) to find the nearest
/// enclosing `IfStmt` / `WhileStmt` / `DoStmt` / `ConditionalOperator`
/// and fires iff the cast was reached via the control stmt's condition
/// slot AND the control stmt is NOT itself inside a WHEN macro
/// expansion (via `detail::is_expansion_of_macro`).
///
/// Bare `if(q)` on a qbool is already a C++ error via the
/// `explicit operator bool()` on `qbool`; this matcher targets the
/// user who reached for `static_cast<bool>(q)` (or a C-style /
/// functional cast) to silence the error — and, in doing so, collapse
/// the qubit state into a classical bit prematurely.
///
/// The matcher is advisory only — it does NOT mutate `unit.scopes` or
/// schedule any `QReplacement`. The Error severity on
/// `DiagContext::report_quantum_to_classical_cond` aborts compilation
/// with a non-zero exit; the reviewer sees the cast line cited in the
/// user's source file.
///
/// Contract mirrors the PM3-2 / PM3-3 / PM3-4 guard helpers — call at
/// most once per QUnit; the QUnit and DiagContext must outlive the
/// MatchFinder's run.
void register_quantum_to_classical_cond_matcher(
    clang::ast_matchers::MatchFinder& finder,
    QUnit& unit,
    DiagContext& diag);

/// Phase N / PN-5: Register the qbool(p) prep diagnostic matcher. Anchors
/// on every `VarDecl` whose declared type resolves to `sturm::qbool` and
/// whose initializer is a single-argument `CXXConstructExpr`. On match
/// the callback:
///
///   1. Peels the init argument through `ignoringParenImpCasts`. If the
///      peeled argument is a `CXXBoolLiteralExpr` (e.g. `qbool x(true);`)
///      or the init expression's static type satisfies
///      `isBooleanType()` (a `bool`-typed variable was passed through
///      the converting constructor), the callback early-returns silently
///      — these are classical inits, not probabilistic prep.
///   2. Walks outward from the VarDecl via `detail::enclosing_scope` to
///      find its lexically enclosing scope anchor, then uses
///      `detail::classify_scope_kind` to decide whether the enclosing
///      scope is a `Function` (top-level — silent, prep is valid at
///      function-body scope) or something else (a WHEN body /
///      compound-expression intermediate — the uncompute-eligible
///      window).
///   3. For non-Function scopes the callback walks the parent chain
///      looking for an enclosing `IfStmt` whose begin loc is inside a
///      `WHEN` macro expansion (via `detail::is_expansion_of_macro`) OR
///      a scope whose `unit.scopes[i].ops` already owns the VarDecl as
///      a synthesized intermediate (Phase E compound-expression pathway).
///      Any hit fires `diag.report_prep_in_uncompute_scope(loc, name)`
///      with the VarDecl's file loc and identifier spelling; the
///      callback returns silently on no hit.
///
/// Registration order: in `transpile_consumer.cpp` PN-5 places this
/// matcher AFTER `register_outer_var_guard_matcher` (PH-3). Neither
/// depends on the other's state — they flag orthogonal error classes
/// — so the ordering is a diagnostic-grouping convention.
///
/// The matcher does NOT mutate `unit` (pure diagnostic). The `diag`
/// reference it is handed must outlive the MatchFinder's run; the
/// consumer owns the underlying `DiagContext`.
///
/// Contract mirrors the other `register_*_matcher` helpers — call at
/// most once per QUnit; the QUnit and DiagContext must outlive the
/// MatchFinder's run.
void register_qbool_prep_matcher(
    clang::ast_matchers::MatchFinder& finder,
    QUnit& unit,
    DiagContext& diag);

/// Test-only instrumentation (Phase N / PN-5). PN-5 is detection-only —
/// the callback does not mutate the `QUnit`, so the PN-5 unit tests
/// need a separate observable to pin down "how many qbool(p) preps in
/// uncompute-eligible scopes did the matcher detect on this TU". These
/// two helpers expose the detection counter maintained by
/// `register_qbool_prep_matcher`'s callback implementation;
/// `reset_qbool_prep_detection_count_for_test` zeroes the counter
/// between test cases so the tests do not have to thread state across
/// invocations. Production code must not touch either helper — they
/// are intended strictly for the unit-test harness.
int qbool_prep_detection_count_for_test();
void reset_qbool_prep_detection_count_for_test();

/// PM3-6 / Class 4: Register the "caller drops returned qbool" diagnostic
/// matcher. Anchors on any `CallExpr` whose return type resolves to a
/// NamedDecl named `sturm::qbool` or `sturm::qint_t` AND whose parent
/// stmt is either a `CompoundStmt` directly, or an `ExprWithCleanups`
/// that sits under a `CompoundStmt`. The second alternative is
/// load-bearing because Clang inserts an `ExprWithCleanups` above any
/// by-value temporary whose type has a non-trivial destructor — which
/// `sturm::qbool` does, via the qubit slot in its `qint_t<1>` base.
///
/// On match the callback emits a `DiagnosticsEngine::Warning` (NOT
/// `Error` — compilation must continue so the user sees every leak in
/// one pass). The message cites the callee's qualified name:
///
///     STURM: discarded quantum return from '<callee>' — the qubit
///     will be released immediately; bind it to a named variable if
///     you intend to use it.
///
/// Shapes that correctly escape the matcher:
///
///   - `qbool x = make_qbool();` — the CallExpr's parent is a VarDecl /
///     DeclStmt, not a CompoundStmt.
///   - `(void)make_qbool();`     — the CallExpr's parent is a
///     `CStyleCastExpr` (the `(void)` cast), which breaks the
///     `hasParent(compoundStmt())` chain. The user has signalled an
///     explicit discard and the matcher respects that.
///
/// Unlike the Phase A–I matchers, this diagnostic matcher does not
/// append anything to the `QUnit` — it reports and returns. The
/// `diag` reference must outlive the MatchFinder's run; the consumer
/// hands in `ci.getDiagnostics()` so the report path lines up with the
/// PM3-1 `TextDiagnosticPrinter` wired into the standalone driver.
void register_dropped_quantum_return_matcher(
    clang::ast_matchers::MatchFinder& finder,
    clang::DiagnosticsEngine& diag);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_HPP
