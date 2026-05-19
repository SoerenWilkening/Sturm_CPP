// matcher_dead_ancilla.cpp — Phase J PJ-4a dead-ancilla elimination.
//
// Recognises the shape
//
//   qbool t = <qbool-operand bitwise expr>;   // no subsequent reader of `t`
//
// where the qbool-operand bitwise expression is any of `a | b`, `a & b`,
// `a ^ b`, `~a`, or a compound of the above. The enclosing-scope
// reader count for `t` must be zero (the PJ-1a
// `detail::count_readers_in_scope` helper is reused verbatim). When
// both guards pass the matcher emits:
//
//   - one `QReplacement{range=<full decl stmt range>, replacement=""}`
//     deleting the VarDecl verbatim from the rewritten source. The
//     range spans from the decl's source begin to its terminating `;`
//     so the Rewriter removes the whole statement including its
//     trailing semicolon.
//   - one entry into `QUnit::eliminated_stmt_ranges` covering the same
//     range, so downstream matchers (PA-3/PA-4 `^=`, Phase E compound,
//     Phase A OR/NOT/XOR, MVP OR) early-return on any match whose own
//     `stmt_range` lies inside an eliminated entry.
//
// Rejection rules (every rejected decl stays on the runtime path; the
// Phase A / Phase E matchers pick it up as usual):
//
//   - Reader count != 0 — the decl has at least one `t` use somewhere
//     in the enclosing scope. The MVP + PJ-1 path handles this shape.
//   - Initializer is not a qbool-operand bitwise op-call — `qbool t = a;`
//     (plain copy) and `qbool t;` (default-constructed) are out of
//     scope. The transpiler does not uncompute plain copies, so
//     eliminating them is not a meaningful optimization.
//
// The matcher is ORDERING-SENSITIVE with the Phase A/E matchers:
// registering it BEFORE each of them is the PJ-4b wiring rationale —
// the Phase A/E callbacks consult `eliminated_stmt_ranges` and bail
// when they see a covered range. The
// `apply_eliminated_stmt_guards` cleanup pass runs after `matchAST`
// as the authoritative backstop against MatchFinder's Decl/Stmt visit-
// pool interleaving.
//
// Implementation note: the "qbool-operand bitwise expression" set is
// deliberately the SAME AST shape set the Phase A/E per-op matchers
// already recognise. Rather than duplicate the overlapping-shape
// matcher constraints, we use a catch-all CXXOperatorCallExpr guard on
// the qbool VarDecl's initializer (peeled through
// `detail::peel_to_payload`) and filter by whether the overloaded
// operator is one of `|`, `&`, `^`, `~`. This keeps PJ-4a's matcher
// pattern small while catching every shape a Phase A/E matcher would
// have anchored on — which is exactly the coverage we need, since
// PJ-4a's job is to suppress those matchers' output for the dead-
// decl slice.
//
// Fresh-name allocator interaction: the Phase E compound-flatten
// matcher advances a per-TU `FreshNameAllocator` counter to mint
// `__stu_tN` intermediates. When PJ-4a eliminates a compound decl
// before PE-4 would have fired, the counter advances regardless
// (harmless — fresh names remain monotonic across the TU).

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/Casting.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace sturm_matcher_dead_ancilla_anon_ns {

using namespace clang;
using namespace clang::ast_matchers;
using detail::count_readers_in_scope;
using detail::enclosing_scope;

// Classify whether `init` (peeled through `peel_to_payload`) is a
// qbool-operand bitwise op-call — an `operator|`/`&`/`^`/`~` overload
// whose operand arity matches a bitwise op (2 for binary, 1 for NOT).
// Returns true on match (eligible for elimination), false otherwise.
// We do NOT require the operands themselves to be DeclRefExprs — a
// compound init like `qbool t = (a | b) & c;` is also eligible for
// elimination when `t` has zero readers. This is the PJ-4a "every
// qbool-operand bitwise shape" contract.
bool init_is_qbool_bitwise(const Expr* init) {
    if (!init) return false;
    const Expr* inner = detail::peel_to_payload(init);
    if (!inner) return false;
    // `CXXOperatorCallExpr` is the overloaded-operator call shape every
    // Phase A/E qbool op-call matches against. We filter on the
    // overloaded operator being one of the bitwise kinds; other
    // overloads (`operator=`, `operator+=`, ...) are not bitwise
    // op-calls and are not PJ-4a's territory. Non-call initializers
    // (e.g. a bare DRE or ctor expr) never match.
    const auto* call = dyn_cast<CXXOperatorCallExpr>(inner);
    if (!call) return false;
    switch (call->getOperator()) {
    case clang::OO_Pipe:
    case clang::OO_Amp:
    case clang::OO_Caret:
    case clang::OO_Tilde:
        return true;
    default:
        return false;
    }
}

class DeadAncillaCallback : public MatchFinder::MatchCallback {
public:
    explicit DeadAncillaCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* var = r.Nodes.getNodeAs<VarDecl>("var");
        if (!var || !r.Context) return;

        ASTContext& ctx = *r.Context;
        const SourceManager& sm = ctx.getSourceManager();
        const LangOptions& lang = ctx.getLangOpts();

        // Confirm the initializer is a qbool-operand bitwise op-call.
        // The MatchFinder pattern narrows the VarDecl to qbool; this
        // post-filter narrows the initializer shape to the bitwise
        // family PJ-4a targets. A non-bitwise init falls through to
        // the Phase A/E matchers unchanged.
        const Expr* init = var->getInit();
        if (!init_is_qbool_bitwise(init)) return;

        // Resolve the enclosing scope. PJ-4a anchors the reader-count
        // probe on the enclosing CompoundStmt / braceless body — the
        // same shape the Phase A/E matchers use. A VarDecl at file
        // scope (no enclosing scope) is not an elimination candidate;
        // bail defensively.
        const auto es = enclosing_scope(*var, ctx);
        if (!es.valid()) return;

        // Pick the scope anchor Stmt. Braced scopes → the enclosing
        // CompoundStmt; braceless scopes → the body Stmt (same shape
        // PJ-1a's helper descends into via RecursiveASTVisitor's
        // shape-agnostic traversal).
        const clang::Stmt* anchor = (es.kind == detail::QScopeKind::CompoundStmt)
                                      ? static_cast<const clang::Stmt*>(es.compound)
                                      : es.braceless_body;
        if (!anchor) return;

        // PJ-1a reader-count gate. reader_count == 0 is the dead-decl
        // condition; any positive count keeps the decl on the Phase A/E
        // runtime path. A nonzero count for the fusion slice (==1) is
        // PJ-1d's concern, not PJ-4a's.
        const int reader_count = count_readers_in_scope(
            llvm::StringRef(var->getName()),
            var->getLocation(),
            anchor,
            ctx);
        if (reader_count != 0) return;

        // Compute the full decl-stmt range: begin at the VarDecl's
        // source begin, end at the terminating `;` one token past
        // `var->getEndLoc()`. Mirrors the range-extension logic the
        // PJ-1d ccnot-fuse matcher uses to absorb the trailing `;`
        // (see matcher_ccnot_fuse.cpp's fused_range computation for
        // the identical pattern). Without this step the Rewriter
        // would leave the `;` behind, producing a stray `;` on the
        // line where the decl used to live.
        //
        // `var->getEndLoc()` returns the last token of the decl
        // (typically the final token of the initializer); the `;`
        // sits one token past that location. `Lexer::findNextToken`
        // walks the buffer forward until the next token and returns
        // it (or nullopt at EOF / on error). A missing or unexpected
        // terminator bails rather than emitting a partial replacement
        // that would corrupt the user's file.
        std::optional<clang::Token> semi = clang::Lexer::findNextToken(
            var->getEndLoc(), sm, lang);
        if (!semi || semi->getKind() != clang::tok::semi) {
            return;
        }
        SourceRange decl_stmt_range(var->getBeginLoc(), semi->getLocation());

        // Stage the replacement — empty text over the full decl stmt
        // range. The Rewriter drops the region verbatim; surrounding
        // whitespace / indentation is preserved byte-for-byte.
        QReplacement rep;
        rep.range = decl_stmt_range;
        rep.replacement = std::string{};
        unit_->replacements.push_back(std::move(rep));

        // PJ-4a: record the decl stmt's range so downstream matchers
        // (PA-3/PA-4 `^=`, PE-4 compound, M7 OR, PA-1 NOT, PA-2 XOR)
        // can early-return on any match whose stmt_range lies inside
        // this entry. The containment probe is the same
        // `is_range_covered_by_fused` shape PJ-1e uses.
        unit_->eliminated_stmt_ranges.push_back(decl_stmt_range);

        // Note: PJ-4a does NOT append a QOperation to any QScope.
        // The decl is silently eliminated — nothing to uncompute,
        // nothing to render. The scopes list stays untouched unless a
        // sibling decl in the same scope legitimately produces a
        // QOperation (which then anchors the scope creation via
        // `find_or_create_scope`). Keeping this matcher scope-free
        // avoids empty-scope pollution in the IR.
    }

private:
    QUnit* unit_;
};

std::vector<std::unique_ptr<DeadAncillaCallback>>&
dead_ancilla_callback_pool() {
    static std::vector<std::unique_ptr<DeadAncillaCallback>> pool;
    return pool;
}

} // namespace sturm_matcher_dead_ancilla_anon_ns
using namespace sturm_matcher_dead_ancilla_anon_ns;

void register_dead_ancilla_matcher(clang::ast_matchers::MatchFinder& finder,
                                   QUnit& unit) {
    // Anchor: any qbool VarDecl with a non-null initializer. We deliberately
    // keep the pattern loose — the init-shape guard runs in the callback
    // via `init_is_qbool_bitwise`. A tighter AST pattern (e.g. requiring
    // the init to be a CXXOperatorCallExpr on a specific operator list)
    // would duplicate the post-filter logic and force Clang to re-run
    // the same subtree inspection the callback needs to do anyway.
    //
    // Structurally, this anchor overlaps with every Phase A/E qbool-
    // VarDecl matcher (M7 OR, PA-1 NOT, PA-2 XOR, PE-4 compound). The
    // registration order in main.cpp (PJ-4b) guarantees PJ-4a's callback
    // fires before each of those; the per-callback
    // `is_range_covered_by_fused`-style containment check on
    // `eliminated_stmt_ranges` (applied in matcher_qbool_bitwise.cpp,
    // matcher_qbool_compound.cpp, and matcher_qbool_assign.cpp) then
    // bails them out on a hit. The `apply_eliminated_stmt_guards`
    // cleanup pass is the authoritative backstop against
    // MatchFinder's Decl/Stmt interleaving.
    auto pattern = varDecl(
        hasType(cxxRecordDecl(hasName("qbool"))),
        hasInitializer(expr())
    ).bind("var");

    auto& pool = dead_ancilla_callback_pool();
    pool.push_back(std::make_unique<DeadAncillaCallback>(&unit));
    finder.addMatcher(pattern, pool.back().get());
}

// Phase J PJ-4a: post-matcher cleanup pass — removes every QOperation
// whose stmt_range lies inside some entry of `unit.eliminated_stmt_ranges`.
// See `matcher.hpp`'s `apply_eliminated_stmt_guards` docstring for the full
// rationale. Mirrors `apply_fused_stmt_guards` shape-for-shape; the only
// difference is the list consulted.
void apply_eliminated_stmt_guards(QUnit& unit,
                                  const clang::SourceManager& sm) {
    if (unit.eliminated_stmt_ranges.empty()) return;  // fast path

    for (auto& scope : unit.scopes) {
        auto& ops = scope.ops;
        // Walk in-place; erase any op whose stmt_range is covered by
        // an eliminated range. Unlike PJ-1e's fuse-aware guard (which
        // whitelists `CCNOT_INPLACE` because its stmt_range sits
        // inside the fused pair), PJ-4a's elimination does not
        // produce any replacement op — nothing to whitelist, so the
        // filter is unconditional.
        ops.erase(
            std::remove_if(
                ops.begin(), ops.end(),
                [&](const QOperation& op) {
                    return detail::is_range_covered_by_fused(
                        op.stmt_range, unit.eliminated_stmt_ranges, sm);
                }),
            ops.end());
    }
}

} // namespace sturm::transpile
