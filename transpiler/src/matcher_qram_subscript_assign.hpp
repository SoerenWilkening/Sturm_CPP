// matcher_qram_subscript_assign.hpp -- sturm-u9ge.6 (Beat H1, post-v1)
// matcher for the existing-target QRAM-read shape.
//
// Plan §11 (post-v1 backlog) / Beat H1; PRD §9 row 1 (existing-target read).
// Recognises the source shape
//
//     b = a[i];        // b is a pre-existing frontend qint variable
//
// where `a[i]` is a QRAM-subscript on one of the three PRD §7 container
// shapes (std::array / C-array / pointer) and `i` carries the qint UDC
// at its index. The sister matchers handle disjoint shapes:
//
//   * `matcher_qram_subscript.{hpp,cpp}` (sturm-u9ge.12 / Beat C1) -- bare
//     init form `qint b = a[i];` (LHS is a fresh VarDecl).
//   * `matcher_qram_subscript_expr.{hpp,cpp}` (sturm-u9ge.9 / Beat H4) --
//     init-position expression-position form `qint c = a[i] + d;`.
//
// H1 is structurally disjoint from both:
//   * C1 anchors on a VarDecl whose initializer IS the subscript -- H1 has
//     no init at all (the assignment is a separate stmt after the decl).
//   * H4 also anchors on a VarDecl initializer (with a strict sub-expr
//     subscript) -- H1 again has no init.
//
// H1 anchors on a `CXXOperatorCallExpr` with `OO_Equal` (assignment
// operator) where:
//   * arg(0) is a frontend `qint` lvalue (typically a `DeclRefExpr` to a
//     prior VarDecl);
//   * arg(1) is the subscript expression `a[i]`, with `i` carrying the
//     qint UDC.
//
// Coexistence with E1 (matcher_qram_oos.{hpp,cpp})
// -----------------------------------------------
// Before H1 lands, the OOS matcher's `ExistingTargetCallback` fires
// `qram-oos-existing-target` on this exact shape. Once H1 lands, the OOS
// callback is updated to skip H1-handled sites (mirrors the H4 update to
// `ExpressionPositionCallback` -- see `is_h4_handled_init_context`). The
// gate is the inverse of H1's anchor: any `OO_Equal` op-call with a
// frontend `qint` LHS DRE and a UDC subscript on the RHS is now H1's
// territory and the OOS diagnostic must NOT fire.
//
// Per-hit width / container kind
// -----------------------------
// `W` is populated via Beat B1's `infer_width()` running rule 2
// (RHS-driven) on the SUBSCRIPT EXPRESSION -- not on the LHS VarDecl,
// because the LHS is a non-templated frontend `qint` whose type-only
// width inference (rule 1) is unavailable. To reuse `infer_width(VarDecl
// const&, ...)`, we synthesise a width-candidate by peeling the
// subscript's container element type directly via the same helper the
// matcher already uses for the H4 path (`element_qint_t_width`).
// Container kind is recovered from the subscript AST node class plus the
// base-expression type, identical to C1's discriminator helper.
//
// LoC budget: <= 300 (plan §1 budget for H1 follow-up).

#ifndef STURM_TRANSPILE_MATCHER_QRAM_SUBSCRIPT_ASSIGN_HPP
#define STURM_TRANSPILE_MATCHER_QRAM_SUBSCRIPT_ASSIGN_HPP

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

/// One matched existing-target QRAM-subscript site (PRD §9 row 1).
///
/// Carries enough state for the H1 emitter to:
///   1. Insert an uncompute call `::sturm::__QRAM_target_uncompute(b);`
///      *before* the assignment so the prior contents of `b` are reset
///      to |0> (PRD §9 row 1 "uncompute of old `b` before QRAM writes").
///   2. Replace the entire `b = a[i];` statement with a forward QRAM
///      call `::sturm::QRAM_read(a, [n,] i, b);` writing the new
///      value into `b`.
///
/// Both rewrites pivot on the SAME source range (the assign op-call's
/// statement extent); the emitter plants them as a two-line sequence.
struct QramSubscriptAssignHit {
    /// Container shape (PRD §7) -- same enum as the C1 / H4 hits.
    QramContainerKind kind = QramContainerKind::StdArray;

    /// The assignment op-call `b = a[i]` itself (the source-range
    /// pivot for the in-place rewrite).
    const clang::Expr* assign_expr = nullptr;

    /// LHS expression `b` of the assignment. Typically a
    /// `DeclRefExpr` wrapping the VarDecl of the existing `qint`
    /// variable; the emitter renders this verbatim into the rewrite
    /// (so `obj.tbl_dst` and `b` both work).
    const clang::Expr* target_expr = nullptr;

    /// The pre-existing VarDecl bound by `target_expr`, or nullptr
    /// if the LHS is not a direct DRE (e.g. member access). Used by
    /// the emitter only as a hint; the rewrite text comes from
    /// `target_expr`'s source range.
    const clang::VarDecl* target_var = nullptr;

    /// The subscript expression `a[i]` itself -- either
    /// `CXXOperatorCallExpr` (std::array arm) or `ArraySubscriptExpr`
    /// (C-array / pointer arms). Used by the emitter to recover the
    /// container + index source text.
    const clang::Expr* subscript_expr = nullptr;

    /// Container expression `a` (arg(0) of the op-call for std::array;
    /// base of the ArraySubscriptExpr otherwise). Verbatim source-text
    /// emit target.
    const clang::Expr* container_expr = nullptr;

    /// Index expression `i`. Verbatim source-text emit target.
    const clang::Expr* index_expr = nullptr;

    /// Inferred width `W` for the rewrite. Populated by mirroring B1's
    /// rule 2 (RHS-driven) on the subscript's container element type.
    /// Falls back to `kDefaultWidth` when the element is not a
    /// `sturm::qint_t<W>` (rule 3).
    unsigned W = 0;

    /// Pointer-arm length-text recovery -- same heuristic as C1 / H4
    /// (sibling integral-typed `ParmVarDecl` immediately after the
    /// container parameter). Empty for `StdArray` / `CArray`. When the
    /// heuristic fails on a `Pointer` hit, the emitter surfaces a
    /// placeholder comment per PRD §11.1.6 (same posture as the v1
    /// emitter).
    std::string length_text;
};

/// Register the H1 matcher against `finder`, directing every matched
/// site into `hits`. One callback per AST anchor (op-call subscript
/// for std::array; built-in subscript for C-array + pointer); both
/// callbacks gate on:
///
///   * the enclosing statement being a `CXXOperatorCallExpr` with
///     `OO_Equal` (the C++ assignment operator -- frontend `qint`
///     overloads `operator=`);
///   * the LHS arg(0) being a frontend `qint` lvalue (canonical type
///     check on a `DeclRefExpr` or other lvalue spelling);
///   * the RHS arg(1) being EXACTLY the subscript expression (after
///     ImplicitCastExpr peeling), NOT a larger expression containing
///     a subscript -- the latter shape lands on H4-via-existing-target
///     (a future combination beat) or stays in OOS for now;
///   * the subscript's index sub-expression carrying the qint UDC
///     (the strong discriminator from PRD §7).
///
/// `hits` must outlive the finder's run. Per-callback unique_ptr
/// pools are owned via function-local statics so the finder's
/// raw-pointer storage stays valid.
void register_qram_subscript_assign_matcher(
    clang::ast_matchers::MatchFinder& finder,
    std::vector<QramSubscriptAssignHit>& hits);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_QRAM_SUBSCRIPT_ASSIGN_HPP
