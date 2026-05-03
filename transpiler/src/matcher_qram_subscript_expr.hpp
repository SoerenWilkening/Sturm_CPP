// matcher_qram_subscript_expr.hpp — sturm-u9ge.9 (Beat H4, post-v1) matcher
// for the expression-position QRAM-read shape.
//
// Plan §11 (post-v1 backlog) / Beat H4; PRD §9 row 4 (expression-position
// read). Recognises the source shape
//
//     qint c = a[i] + d;        // and other binary-op shapes that wrap a[i]
//
// where the QRAM subscript `a[i]` is embedded *inside* a larger
// expression at VarDecl-init position. The sister matcher
// `matcher_qram_subscript.{hpp,cpp}` (sturm-u9ge.12 / Beat C1) handles the
// bare shape `qint b = a[i];`; this matcher handles the strictly-larger
// shape where the subscript is a sub-expression of the initializer rather
// than the initializer itself.
//
// Discriminator (mirrors C1 + E1)
// -------------------------------
// All three matchers share the same pivot: the index sub-expression
// contains an `ImplicitCastExpr` of `CK_UserDefinedConversion` whose
// conversion function is declared on a class named `qint`. This is the
// load-bearing surface signal — no other surface type carries an
// implicit `qint -> integer` conversion.
//
// Disjointness vs C1 + E1
// -----------------------
// - C1 anchors on `varDecl(hasInitializer(ignoringImplicit(
//   cxxConstructExpr(hasArgument(0, ignoringParenImpCasts(<subscript>))))))` —
//   i.e. the subscript IS the immediate initializer expression. H4
//   anchors on `varDecl(hasInitializer(<larger expr containing subscript>))`
//   so the two are structurally disjoint by construction.
// - E1's `ExpressionPositionCallback` fires on every `CXXOperatorCallExpr`
//   non-assign op containing a UDC subscript regardless of context.
//   Once H4 lands, E1 must skip cases where the enclosing op is the
//   immediate initializer of a frontend `qint` VarDecl — that work is
//   covered by the H4 matcher. The E1 callback is updated in the same
//   beat to suppress the diagnostic in the H4-handled context (search
//   `matcher_qram_oos.cpp::ExpressionPositionCallback` for the gate).
//   E1 still fires for true non-init expression-position uses such as
//   `f(a[i] + d)`, `if (a[i] + d > 0)`, `return a[i] + d;`, etc.
//
// Per-hit width / container kind
// -----------------------------
// The width `W` is populated via Beat B1's `infer_width()` running rule 2
// (RHS-driven) on the initializer expression — same posture as C1, so the
// "single source of truth" rule (PRD §11.3 / D0c.3) is preserved.
// Container kind is recovered from the subscript AST node class plus the
// base-expression type, identical to C1's discriminator helper.
//
// Multiple subscripts in a single initializer
// -------------------------------------------
// `qint c = a[i] + a[j];` has two distinct subscript reads embedded in a
// single initializer. The matcher publishes ONE hit per matched subscript
// site, so the emitter can extract each into its own ancilla. The hits
// share a `target_var` but carry distinct `subscript_expr` /
// `container_expr` / `index_expr` triples and (for the pointer arm)
// `length_text`. Ordering is matcher-traversal order — typically left-to-
// right per Clang's RecursiveASTVisitor — but the emitter does not depend
// on this order beyond consistency within a single hit's spelling.
//
// LoC budget: <= 300 (plan §1 budget for H4 follow-up).

#ifndef STURM_TRANSPILE_MATCHER_QRAM_SUBSCRIPT_EXPR_HPP
#define STURM_TRANSPILE_MATCHER_QRAM_SUBSCRIPT_EXPR_HPP

#include "matcher_qram_subscript.hpp"  // QramContainerKind reuse

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <string>
#include <vector>

namespace clang {
class Expr;
class Stmt;
class VarDecl;
} // namespace clang

namespace sturm::transpile {

/// One matched expression-position QRAM-subscript site (PRD §9 row 4).
/// Carries enough state for the H4 emitter to:
///   1. Allocate a fresh ancilla `__qram_h4_<N>` and emit
///      `qint_t<W> __qram_h4_<N>; ::sturm::QRAM_read(a, [n,] i, __qram_h4_<N>);`
///      *before* the user's `qint c = ...;` line.
///   2. Replace the `a[i]` subscript expression in the user's
///      initializer with the ancilla reference (in-place via Rewriter).
///   3. Insert the matching adjoint
///      `::sturm::__QRAM_read_adj(a, [n,] i, __qram_h4_<N>);` *after*
///      the user's line so the ancilla is uncomputed before going out
///      of scope.
///
/// Multiple `QramSubscriptExprHit`s may share a `target_var` if the
/// initializer contains multiple `a[i]` subscripts (e.g.
/// `qint c = a[i] + a[j];`). Per-hit `subscript_expr` /
/// `container_expr` / `index_expr` triples disambiguate.
struct QramSubscriptExprHit {
    /// Container shape (PRD §7) — same enum as the C1 hit. Populated
    /// from the subscript AST node class plus the base-expression
    /// type, identical to `discriminate_array_subscript`.
    QramContainerKind kind = QramContainerKind::StdArray;

    /// VarDecl for the LHS `c` of `qint c = a[i] + d;`. The H4 emitter
    /// reads this for the `qint -> qint_t<W>` rewrite of the LHS type
    /// and for source-range placement of the pre-call ancilla decl
    /// and the post-call adjoint.
    const clang::VarDecl* target_var = nullptr;

    /// The subscript expression node `a[i]` itself (either
    /// `CXXOperatorCallExpr` for the std::array arm or
    /// `ArraySubscriptExpr` for the C-array / pointer arms). The
    /// emitter's source-range replacement pivots on this node — not
    /// on the enclosing initializer — so a single `a[i] + a[j]`
    /// initializer becomes two independent in-place rewrites.
    const clang::Expr* subscript_expr = nullptr;

    /// Container expression `a` (arg(0) of the op-call for std::array;
    /// base of the ArraySubscriptExpr otherwise). The emitter renders
    /// this verbatim into the `QRAM_read(a, ...)` call.
    const clang::Expr* container_expr = nullptr;

    /// Index expression `i`. The emitter renders this verbatim into
    /// the `QRAM_read(..., i, ...)` call.
    const clang::Expr* index_expr = nullptr;

    /// Inferred width `W` for the ancilla's `qint_t<W>` and the
    /// rewritten LHS type. Populated via B1's `infer_width()` running
    /// rule 2 (RHS-driven) on the initializer's subscript subtree —
    /// PRD §11.3 / D0c.3 single-source-of-truth contract.
    unsigned W = 0;

    /// Pointer-arm length-text recovery — same heuristic as C1's
    /// `recover_pointer_length_text` (sibling integral-typed
    /// `ParmVarDecl` immediately after the container parameter).
    /// Empty for `StdArray` / `CArray`. When the heuristic fails on a
    /// `Pointer` hit, the emitter surfaces a placeholder comment per
    /// PRD §11.1.6 (same posture as the v1 emitter).
    std::string length_text;
};

/// Register the H4 matcher against `finder`, directing every matched
/// site into `hits`. One callback per AST anchor (op-call subscript
/// for std::array; built-in subscript for C-array + pointer); both
/// callbacks gate on:
///
///   * the enclosing `VarDecl` being a frontend `qint`;
///   * the index sub-expression carrying the qint UDC (the strong
///     discriminator from PRD §7);
///   * the subscript NOT being the immediate initializer of the
///     VarDecl — i.e. the initializer must contain the subscript
///     strictly-inside some larger expression (binary op, function
///     call, paren, cast, etc.). This is the inverse of C1's
///     immediate-init gate so the two matchers are structurally
///     disjoint.
///
/// PRD §11.3 / D0c.3 contract: B1's `infer_width()` is invoked inside
/// the callback to populate `QramSubscriptExprHit::W`, mirroring the
/// C1 matcher's posture.
///
/// `hits` must outlive the finder's run. Per-callback unique_ptr
/// pools are owned via function-local statics so the finder's
/// raw-pointer storage stays valid.
void register_qram_subscript_expr_matcher(
    clang::ast_matchers::MatchFinder& finder,
    std::vector<QramSubscriptExprHit>& hits);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_QRAM_SUBSCRIPT_EXPR_HPP
