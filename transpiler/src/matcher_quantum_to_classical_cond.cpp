// matcher_quantum_to_classical_cond.cpp — PM3-5 Class 2 diagnostic.
//
// Detects `static_cast<bool>(q)` / `(bool)q` / `bool(q)` (and the
// integral-destination variants `(int)q`, `static_cast<int>(q)`, etc.)
// whose source is a `sturm::qbool` / `sturm::qint_t` AND whose nearest
// enclosing control statement reaches the cast via its condition slot.
// Collapsing a quantum value into a classical bit at branch time would
// abandon the WHEN primitive's lexical-scope control semantics (P4) and
// leaves the qubit's contribution to the superposition unaccounted for —
// the transpiler cannot honour such a program, so compilation aborts
// with an Error-severity `DiagnosticsEngine` diagnostic.
//
// Detection shape (algorithm)
// ---------------------------
// The matcher anchors on every explicit cast kind:
//
//   cxxStaticCastExpr  — `static_cast<bool>(q)`
//   cStyleCastExpr     — `(bool)q`
//   cxxFunctionalCastExpr — `bool(q)`
//
// whose destination type is `booleanType()` or any `isInteger()` type.
// The AST pattern guards on `hasSourceExpression` to peel through an
// inner DeclRefExpr whose referenced decl is `sturm::qbool` /
// `sturm::qint_t` (matched via both the unqualified name `qbool` /
// `qint_t` — `qint_t<N>` is a ClassTemplateSpecializationDecl whose
// underlying record is still named `qint_t`).
//
// On match the callback:
//
//   1. Peels the cast's source expression through paren/implicit-cast/
//      temporary wrappers via `detail::peel_to_payload` to reach the
//      inner DeclRefExpr. Records the source qbool / qint_t identifier
//      for the diagnostic `%0` slot.
//
//   2. Walks `ASTContext::getParents` from the cast node upward,
//      skipping `ImplicitCastExpr`, `ParenExpr`, `ExprWithCleanups`,
//      and `CXXBindTemporaryExpr` / `MaterializeTemporaryExpr` wrappers
//      that Clang may insert between a user-written cast and its
//      enclosing control statement. Tracks the most recent Stmt as
//      `prev_stmt` so the control-stmt branch can identify whether
//      the walk entered via the condition slot.
//
//   3. As soon as the walk reaches an `IfStmt` / `WhileStmt` /
//      `DoStmt` / `ConditionalOperator`, it checks whether
//      `prev_stmt == control->getCond()`. A true answer plus a
//      NOT-WHEN-expansion guard (`detail::is_expansion_of_macro`) is
//      the "fire" signal. Any other shape (the cast lives in the
//      body, the init-stmt, or the increment) is silently rejected.
//
//   4. On a hit the callback funnels the cast's begin loc through
//      `SourceManager::getFileLoc(...)` (matches the PM3-2 / PM3-3 /
//      PM3-4 plumbing so the `TextDiagnosticPrinter` cites the user's
//      file rather than Clang's `<memory-buffer>` fallback) and
//      calls `diag.report_quantum_to_classical_cond(loc, name)`.
//
// Shapes that correctly do NOT fire
// ---------------------------------
//   - `if (q)` — the bare-if is already a C++ error since
//     `qbool::operator bool()` is `explicit`; no explicit cast node
//     exists in the AST, so the matcher never anchors.
//   - `bool b = static_cast<bool>(q);` — the cast's ancestor is a
//     VarDecl / DeclStmt, not a control stmt; the parent walk never
//     reaches an IfStmt / WhileStmt / DoStmt / ConditionalOperator.
//   - `WHEN(q) { body }` — the WHEN macro expands to a three-`if`
//     tower whose init-stmt forwards `q` through `materialize_when`.
//     There is no cast from `qbool` to `bool` in the user-visible
//     source, and even if the expansion contained one, the
//     `is_expansion_of_macro("WHEN")` guard on the enclosing control
//     stmt would skip it.
//
// Registration
// ------------
// PM3-5 is a pure diagnostic matcher; it does not mutate the QUnit. The
// `diag` reference it is handed must outlive the MatchFinder's run; the
// consumer owns the underlying `DiagContext`.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"

#include "diag_context.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/Casting.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// Return true iff `qt` resolves to a CXXRecord whose (unqualified) name
// is `qbool` / `qint_t` / `qint`. Mirrors the type classifier used in
// `matcher_when_operand_mutation.cpp` — strip top-level references and
// qualifications so both `qbool` and `const qbool&` resolve to the same
// CXXRecord; `qint_t<N>` is a ClassTemplateSpecializationDecl whose
// `getName()` returns the template name `qint_t` (not `qint_t<1>`).
bool is_quantum_record(QualType qt) {
    if (qt.isNull()) return false;
    QualType stripped = qt.getNonReferenceType().getUnqualifiedType();
    const CXXRecordDecl* rd = stripped->getAsCXXRecordDecl();
    if (!rd) return false;
    const std::string name = rd->getNameAsString();
    return name == "qbool" || name == "qint" || name == "qint_t";
}

// Peel common "implicit-ish" wrappers off a Stmt during the parent
// walk. These nodes (ImplicitCastExpr, ParenExpr, ExprWithCleanups,
// CXXBindTemporaryExpr, MaterializeTemporaryExpr) can sit between a
// user-written cast and the control statement's condition slot, and
// none of them count as a "new" prev_stmt for the control-slot
// comparison — if a ParenExpr wraps the cast in `if ((static_cast
// <bool>(q)))`, the IfStmt's getCond() returns the ParenExpr, not the
// original cast. We treat these wrappers as transparent so the
// slot-comparison sees the un-wrapped node stored at each layer.
//
// Returns true iff the node should be skipped (not updated as the
// new `prev_stmt`). Returns false for any other Stmt kind — those
// count as real ancestors in the walk.
bool is_transparent_wrapper(const Stmt* s) {
    if (!s) return false;
    return llvm::isa<ImplicitCastExpr>(s) ||
           llvm::isa<ParenExpr>(s) ||
           llvm::isa<ExprWithCleanups>(s) ||
           llvm::isa<CXXBindTemporaryExpr>(s) ||
           llvm::isa<MaterializeTemporaryExpr>(s);
}

// Tri-valued classification of the parent-chain result. Hit = the cast
// reaches a control stmt via its condition slot; NotHit = the cast
// reaches a control stmt via a non-condition slot (body / init / inc)
// OR the walk hit a non-control ancestor that breaks the chain (e.g.
// a VarDecl, an assignment target, a return stmt). WhenExpansion = the
// cast reaches a control stmt whose IfLoc is a WHEN macro expansion;
// the matcher silently rejects this case (WHEN's inner tower is not a
// user-written control construct).
enum class ParentWalkResult { Hit, NotHit, WhenExpansion };

// Walk up the parent chain of `cast_expr` toward the nearest enclosing
// IfStmt / WhileStmt / DoStmt / ConditionalOperator. Returns Hit iff
// the walk reaches such a control stmt AND the stmt's condition slot
// contains `prev_stmt` (the most-recent non-transparent ancestor). The
// parent walk treats ImplicitCastExpr / ParenExpr / ExprWithCleanups /
// CXXBindTemporaryExpr / MaterializeTemporaryExpr wrappers as
// transparent — their presence does not update `prev_stmt`, so the
// slot comparison at the control stmt sees the original cast node (or
// its immediate non-transparent ancestor, e.g. a UnaryOperator).
//
// Returns WhenExpansion iff the control stmt's IfLoc is itself a
// macro-body expansion of `WHEN` — the matcher must silently skip this
// case because WHEN's three-if tower is not a user-written control
// construct. Returns NotHit on any other shape (the cast lives in the
// body, the init-stmt, or the increment; no control stmt is found
// before the walk exits the TU).
ParentWalkResult classify_parent_walk(const Stmt* cast_expr,
                                      ASTContext& ctx,
                                      const SourceManager& sm,
                                      const LangOptions& lang) {
    if (!cast_expr) return ParentWalkResult::NotHit;

    // `prev_stmt` tracks the most recent non-transparent Stmt ancestor
    // we walked through. Starts as the cast itself — every control-
    // stmt slot the walk reaches will be compared against `prev_stmt`
    // to decide whether we entered via the cond slot.
    const Stmt* prev_stmt = cast_expr;
    DynTypedNode node = DynTypedNode::create(*cast_expr);
    // Cap the walk at a generous bound to defend against pathological
    // shapes; real source trees are nowhere near this deep.
    for (int hops = 0; hops < 512; ++hops) {
        const auto parents = ctx.getParents(node);
        if (parents.empty()) return ParentWalkResult::NotHit;
        node = parents[0];

        // IfStmt / WhileStmt / DoStmt: we entered via the cond slot iff
        // `prev_stmt == control->getCond()`. Any other slot (then /
        // else / body / init) means the cast is NOT in the condition
        // — the matcher does not fire.
        if (const auto* is = node.get<IfStmt>()) {
            if (is->getCond() == prev_stmt) {
                // WHEN-expansion guard: if the IfStmt's spelling loc is
                // a macro-body expansion of `WHEN`, silently skip. The
                // WHEN macro expands to a three-`if` tower whose inner
                // `if`s are compiler-generated; they must not be
                // flagged as user-written control constructs.
                const SourceLocation if_loc = is->getIfLoc();
                if (if_loc.isValid() && if_loc.isMacroID() &&
                    detail::is_expansion_of_macro(
                        if_loc, sm, lang, "WHEN")) {
                    return ParentWalkResult::WhenExpansion;
                }
                return ParentWalkResult::Hit;
            }
            return ParentWalkResult::NotHit;
        }
        if (const auto* ws = node.get<WhileStmt>()) {
            if (ws->getCond() == prev_stmt) {
                const SourceLocation while_loc = ws->getWhileLoc();
                if (while_loc.isValid() && while_loc.isMacroID() &&
                    detail::is_expansion_of_macro(
                        while_loc, sm, lang, "WHEN")) {
                    return ParentWalkResult::WhenExpansion;
                }
                return ParentWalkResult::Hit;
            }
            return ParentWalkResult::NotHit;
        }
        if (const auto* ds = node.get<DoStmt>()) {
            if (ds->getCond() == prev_stmt) {
                const SourceLocation do_loc = ds->getDoLoc();
                if (do_loc.isValid() && do_loc.isMacroID() &&
                    detail::is_expansion_of_macro(
                        do_loc, sm, lang, "WHEN")) {
                    return ParentWalkResult::WhenExpansion;
                }
                return ParentWalkResult::Hit;
            }
            return ParentWalkResult::NotHit;
        }
        // ConditionalOperator: `a ? b : c`. The `cond` slot holds the
        // selector; the `true` / `false` arms are the other operands.
        // Fire iff we entered via the cond slot — a qbool appearing in
        // the arm position would be a different pattern (not a branch
        // condition) and is out of scope for PM3-5.
        if (const auto* co = node.get<ConditionalOperator>()) {
            if (co->getCond() == prev_stmt) {
                const SourceLocation q_loc = co->getQuestionLoc();
                if (q_loc.isValid() && q_loc.isMacroID() &&
                    detail::is_expansion_of_macro(
                        q_loc, sm, lang, "WHEN")) {
                    return ParentWalkResult::WhenExpansion;
                }
                return ParentWalkResult::Hit;
            }
            return ParentWalkResult::NotHit;
        }

        // Any other control-flow shape (ForStmt, SwitchStmt) could in
        // principle host a quantum-typed condition, but the issue
        // description locks the target set to If / While / Do /
        // ConditionalOperator. Treat others as chain-breakers so we
        // don't accidentally fire on a slot we weren't asked to check.
        if (node.get<ForStmt>() != nullptr ||
            node.get<SwitchStmt>() != nullptr) {
            return ParentWalkResult::NotHit;
        }

        // Update `prev_stmt` for the next iteration, but ONLY if the
        // current node is not a transparent wrapper (ImplicitCastExpr,
        // ParenExpr, ExprWithCleanups, CXXBindTemporaryExpr,
        // MaterializeTemporaryExpr). Otherwise the control-slot
        // comparison at a higher ancestor would not recognise our node
        // as the condition — the IfStmt's getCond() returns the
        // wrapper, not the original cast.
        if (const auto* as_stmt = node.get<Stmt>()) {
            if (!is_transparent_wrapper(as_stmt)) {
                prev_stmt = as_stmt;
            }
        }
    }
    return ParentWalkResult::NotHit;
}

// Pull the source qbool / qint_t identifier out of a cast's source
// expression and verify the source type resolves to qbool / qint_t /
// qint. The `getSubExpr()` goes through the cast's operand; we peel
// through paren / implicit-cast / temporary / CXXMemberCallExpr
// (user-defined-conversion) wrappers via `detail::peel_to_payload`
// to reach the innermost DeclRefExpr. Returns the VarDecl's name on
// success (when the referenced VarDecl's type is qbool / qint_t /
// qint), or an empty string on any structural mismatch — the caller
// treats an empty string as a defensive "skip this match" signal.
//
// Load-bearing detail for `static_cast<bool>(q)`: the AST sub-tree
// Clang builds for an `explicit operator bool()` conversion is
//
//   CXXStaticCastExpr 'bool' static_cast<_Bool>
//   `-ImplicitCastExpr 'bool' UserDefinedConversion part_of_explicit_cast
//     `-CXXMemberCallExpr 'bool'
//       `-MemberExpr .operator bool
//         `-ImplicitCastExpr 'const sturm::qbool' lvalue
//           `-DeclRefExpr 'q'
//
// `peel_to_payload` skips the ImplicitCastExpr + CXXMemberCallExpr
// (zero-arg member call targeting a CXXConversionDecl) + the inner
// ImplicitCastExpr, landing on the `q` DeclRefExpr directly.
std::string extract_source_qbool_name(const Expr* src) {
    if (!src) return {};
    const Expr* inner = detail::peel_to_payload(src);
    if (!inner) return {};
    const auto* dre = llvm::dyn_cast_or_null<DeclRefExpr>(inner);
    if (!dre) return {};
    const NamedDecl* nd = dre->getDecl();
    if (!nd) return {};
    // Type-guard: the referenced decl must be a VarDecl whose type
    // resolves to a qbool / qint_t / qint record. A C++ cast whose
    // operand is a bare DeclRefExpr to a non-quantum value (e.g.
    // `static_cast<bool>(some_int)`) must not fire the PM3-5
    // matcher — that's not a quantum-to-classical collapse.
    const auto* vd = llvm::dyn_cast_or_null<VarDecl>(nd);
    if (!vd) return {};
    if (!is_quantum_record(vd->getType())) return {};
    return nd->getNameAsString();
}

class QuantumToClassicalCondCallback : public MatchFinder::MatchCallback {
public:
    QuantumToClassicalCondCallback(QUnit* unit, DiagContext* diag)
        : unit_(unit), diag_(diag) {}

    void run(const MatchFinder::MatchResult& r) override {
        // Try each of the three explicit-cast anchors. The matcher
        // bindings are disjoint — at most one of these returns
        // non-null on a single match result — but for robustness we
        // fall through on a miss.
        const Expr* cast_expr = nullptr;
        if (const auto* sc = r.Nodes.getNodeAs<CXXStaticCastExpr>(
                "static_cast")) {
            cast_expr = sc;
        } else if (const auto* cc = r.Nodes.getNodeAs<CStyleCastExpr>(
                "c_style_cast")) {
            cast_expr = cc;
        } else if (const auto* fc = r.Nodes.getNodeAs<CXXFunctionalCastExpr>(
                "functional_cast")) {
            cast_expr = fc;
        }
        if (!cast_expr || !r.Context || !diag_) return;

        const SourceManager& sm = r.Context->getSourceManager();
        const LangOptions& lang = r.Context->getLangOpts();

        // Peel the cast's source expression to a DeclRefExpr and pull
        // out the VarDecl's name for the %0 slot. Defensive: the AST
        // pattern already guarded on a qbool / qint_t source, but the
        // peel may return empty if the AST shape is unexpected
        // (e.g. a chained cast of a chained cast). Bail in that case.
        const auto* as_cast =
            llvm::dyn_cast_or_null<CastExpr>(cast_expr);
        if (!as_cast) return;
        const Expr* src = as_cast->getSubExpr();
        const std::string name = extract_source_qbool_name(src);
        if (name.empty()) return;

        // Walk the parent chain toward the nearest control stmt.
        // Returns Hit only if the cast is in the cond slot of an
        // IfStmt / WhileStmt / DoStmt / ConditionalOperator AND the
        // stmt is not a WHEN-expansion.
        const ParentWalkResult res = classify_parent_walk(
            cast_expr, *r.Context, sm, lang);
        if (res != ParentWalkResult::Hit) return;

        // Funnel through `getFileLoc` so diagnostics cite the user's
        // filename under the plugin's nested CompilerInvocation as
        // well as under the standalone driver. Matches the PM3-2 /
        // PM3-3 / PM3-4 plumbing.
        const SourceLocation cast_loc = cast_expr->getBeginLoc();
        const SourceLocation file_loc = sm.getFileLoc(cast_loc);
        diag_->report_quantum_to_classical_cond(
            file_loc, std::string_view(name));
        (void)unit_; // intentionally not mutated — advisory only.
    }

private:
    QUnit* unit_;
    DiagContext* diag_;
};

// Callback pool — matches the lifetime convention used by every other
// matcher_*.cpp module. The callback is owned here so the MatchFinder
// (which stores a raw pointer) does not outlive it.
std::vector<std::unique_ptr<QuantumToClassicalCondCallback>>&
quantum_to_classical_cond_callback_pool() {
    static std::vector<std::unique_ptr<QuantumToClassicalCondCallback>> pool;
    return pool;
}

} // namespace

void register_quantum_to_classical_cond_matcher(
    clang::ast_matchers::MatchFinder& finder,
    QUnit& unit,
    DiagContext& diag) {
    // Destination-type guard: boolean OR any integral type. The issue
    // description calls out `booleanType() OR isInteger()`; the
    // `isInteger()` matcher covers `bool`, `char`, `int`, `long`, and
    // their signed/unsigned variants, so we explicitly combine both
    // so the guard is resilient to any Clang version where
    // `isInteger()` semantics shift.
    auto classical_dest = hasType(qualType(anyOf(booleanType(), isInteger())));

    // The source-operand guard is done in the callback via
    // `extract_source_qbool_name`. A declarative AST-pattern match
    // using `hasSourceExpression(ignoringParenImpCasts(declRefExpr))`
    // would bypass the user-defined-conversion layer that Clang
    // inserts between `static_cast<bool>` and the `qbool` DeclRefExpr
    // — the sub-expr is a `CXXMemberCallExpr` to `operator bool()`,
    // not a bare DeclRefExpr. `peel_to_payload` (used in the
    // callback) handles the conversion call correctly.
    //
    // Three disjoint cast shapes — static_cast<T>(q), (T)q, T(q).
    // Bind each so the callback can tell which fired.
    auto static_cast_pat = cxxStaticCastExpr(
        classical_dest
    ).bind("static_cast");

    auto c_style_cast_pat = cStyleCastExpr(
        classical_dest
    ).bind("c_style_cast");

    auto functional_cast_pat = cxxFunctionalCastExpr(
        classical_dest
    ).bind("functional_cast");

    auto& pool = quantum_to_classical_cond_callback_pool();
    pool.push_back(std::make_unique<QuantumToClassicalCondCallback>(
        &unit, &diag));
    finder.addMatcher(static_cast_pat, pool.back().get());
    finder.addMatcher(c_style_cast_pat, pool.back().get());
    finder.addMatcher(functional_cast_pat, pool.back().get());
}

} // namespace sturm::transpile
