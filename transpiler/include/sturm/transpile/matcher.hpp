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

namespace sturm::transpile {

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

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_HPP
