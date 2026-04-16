// matcher_when_nested.cpp — Phase G / PG-1 nested-WHEN detection matcher.
//
// Purpose
// -------
// Detects `WHEN(outer) { WHEN(inner) { body } }` pairs in user source where
// BOTH `outer` and `inner` peel (via `detail::peel_to_payload`) to a bare
// `DeclRefExpr` that names a non-synthetic qbool local / parameter. This is
// the "named + named" shape the Phase G plan targets — any compound,
// comparator, or unary shape on either side keeps the pair on the runtime
// path (and the Phase F matcher handles a compound inner separately).
//
// PG-1 is a **detection-only** slice: the callback validates every guard
// and, on a full hit, increments a module-local counter exposed via
// `when_nested_detection_count_for_test()`. No `QReplacement`,
// `UncomputeInsertion`, or `QOperation` is appended to the QUnit. The
// rewrite logic (pre-WHEN `qbool __stu_ctrl<M> = outer & inner;` decl
// injection, inner-WHEN argument replacement, and `uncompute_and` scheduling
// via a synthetic `QOperation{kind=AND}`) lands in PG-2 / PG-3. This split
// mirrors the PF-2 → PF-3 pattern Phase F used.
//
// Pairwise cascade semantics
// --------------------------
// For `WHEN(a) { WHEN(b) { WHEN(c) { body } } }` the detection counter must
// reach 2 — one increment per adjacent (outer, inner) pair: (a, b) and
// (b, c). Crucially, (a, c) must NOT be counted because the PG-2 / PG-3
// emission steps schedule ONE `__stu_ctrl<M>` temp per pair and would emit
// the wrong AND shape for a transitive (a, c) match.
//
// The matcher achieves this by anchoring on the INNER WHEN `IfStmt` and,
// inside the callback, walking up the AST parent chain to find the NEAREST
// enclosing `IfStmt` whose init-stmt declares `_when_val_`. If such an
// ancestor exists and both args are bare DREs, we record one pair. For the
// depth-3 case, the inner-most WHEN finds its immediate parent (`b`),
// incrementing once; the middle WHEN finds its immediate parent (`a`),
// incrementing a second time. No transitive (a, c) pair can form because
// the search stops at the nearest ancestor.
//
// Disjointness with Phase F
// -------------------------
// Phase F's `register_when_lift_matcher` fires on every `_when_val_`
// IfStmt but its named-passthrough short-circuit early-returns when the
// WHEN arg peels to a bare DeclRefExpr — no rewrites are staged for the
// shapes Phase G cares about. Conversely, Phase G ignores any pair where
// EITHER side is not a bare DRE. A given inner WHEN thus gets rewritten
// by at most one of the two matchers (PG-2 / PG-3 will later claim the
// inner arg's replacement exclusively for the named+named shape).

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// Test-only detection counter. Incremented once per successfully matched
// (outer, inner) named+named pair by `WhenNestedCallback::run`. The
// transpiler is single-threaded (LibTooling drives one AST at a time) so a
// plain non-atomic int is fine.
static int g_when_nested_detection_count = 0;

// A WHEN-IfStmt is the MIDDLE `if` in the three-`if` tower the `WHEN` macro
// expands to. Its init-stmt declares exactly one VarDecl named
// `_when_val_` whose initializer reaches a CallExpr to `materialize_when`
// (possibly through implicit-cast wrappers). This helper extracts that
// CallExpr if the IfStmt has the expected shape, or nullptr otherwise.
//
// We check the shape at this level rather than relying on the AST matcher
// alone because the parent-chain walk operates on raw Stmt* nodes with no
// pre-attached bindings — we need to classify each ancestor on the fly.
static const CallExpr* materialize_call_in_when_if(const IfStmt* if_stmt) {
    if (!if_stmt) return nullptr;
    const Stmt* init = if_stmt->getInit();
    if (!init) return nullptr;
    const auto* decl_stmt = dyn_cast<DeclStmt>(init);
    if (!decl_stmt || !decl_stmt->isSingleDecl()) return nullptr;
    const auto* vd = dyn_cast<VarDecl>(decl_stmt->getSingleDecl());
    if (!vd) return nullptr;
    if (vd->getName() != "_when_val_") return nullptr;
    const Expr* init_expr = vd->getInit();
    if (!init_expr) return nullptr;
    // `IgnoreImplicit` peels CXXBindTemporaryExpr / MaterializeTemporaryExpr
    // / implicit-cast wrappers until we reach the underlying `CallExpr`.
    // The Phase F matcher uses `ignoringImplicit(materialize_call)` at the
    // pattern level to the same effect; here we do it by hand because we
    // are inspecting an arbitrary ancestor.
    const Expr* peeled = init_expr->IgnoreImplicit();
    const auto* call = dyn_cast_or_null<CallExpr>(peeled);
    if (!call) return nullptr;
    const FunctionDecl* fd = call->getDirectCallee();
    if (!fd) return nullptr;
    if (fd->getName() != "materialize_when") return nullptr;
    if (call->getNumArgs() != 1) return nullptr;
    return call;
}

// Walk up the parent chain of `inner_if` looking for the NEAREST `IfStmt`
// ancestor whose init-stmt has the `_when_val_ = materialize_when(...)`
// shape. Returns nullptr if no such ancestor exists (i.e. the inner WHEN
// has no enclosing WHEN macro expansion). This realises the "pairwise
// cascade" semantic: every `WHEN(inner)` binds at most ONE outer partner,
// the immediately enclosing one — so a three-deep cascade produces two
// pairs (middle+inner, outer+middle), not three.
static const IfStmt*
find_nearest_enclosing_when_if(const IfStmt* inner_if, ASTContext& ctx) {
    if (!inner_if) return nullptr;
    DynTypedNode node = DynTypedNode::create(*inner_if);
    // Follow only the first parent at each step — multi-parent shapes
    // only arise in template instantiations, which the matcher does not
    // enter. The WHEN macro expands inline into the user's function body,
    // so a linear walk is both correct and fast.
    for (int hops = 0; hops < 256; ++hops) {
        const auto parents = ctx.getParents(node);
        if (parents.empty()) return nullptr;
        node = parents[0];
        if (const auto* if_anc = node.get<IfStmt>()) {
            if (materialize_call_in_when_if(if_anc) != nullptr) {
                return if_anc;
            }
        }
    }
    return nullptr;
}

// Phase G PG-1 guard: the peeled WHEN argument must be a bare DeclRefExpr
// referring to a non-synthetic named qbool. Non-synthetic means the
// declaration's spelled name does NOT start with the `__stu_` prefix — that
// prefix is reserved for transpiler-generated temporaries (Phase E/F
// `__stu_t<N>` and Phase G `__stu_ctrl<M>`), and firing on one would
// double-lift a name the previous pass already introduced.
//
// Returns true when the guard passes. The outparam `leaf_name` receives
// the identifier so the caller can use it in diagnostics / future PG-2
// emission. On a reject (non-DRE leaf, anonymous decl, or `__stu_` prefix)
// `leaf_name` is left empty and the function returns false.
static bool peeled_is_named_qbool(const Expr* arg, std::string& leaf_name) {
    leaf_name.clear();
    const Expr* peeled = detail::peel_to_payload(arg);
    if (!peeled) return false;
    const auto* dre = dyn_cast<DeclRefExpr>(peeled);
    if (!dre) return false;
    const NamedDecl* nd = dre->getDecl();
    if (!nd) return false;
    const std::string name = nd->getNameAsString();
    if (name.empty()) return false;
    // Reject synthetic transpiler-generated names. A user could in theory
    // declare a variable named `__stu_foo` by hand — that is their
    // problem; the prefix is documented as reserved.
    static constexpr std::string_view kSyntheticPrefix = "__stu_";
    if (std::string_view(name).substr(0, kSyntheticPrefix.size()) ==
        kSyntheticPrefix) {
        return false;
    }
    leaf_name = name;
    return true;
}

class WhenNestedCallback : public MatchFinder::MatchCallback {
public:
    explicit WhenNestedCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        // The matcher binds the INNER WHEN's middle `if` as `inner_if`.
        // We walk up from that node to find its nearest WHEN ancestor
        // (the outer). All Phase G guards then apply to BOTH endpoints.
        const auto* inner_if = r.Nodes.getNodeAs<IfStmt>("inner_if");
        if (!inner_if || !r.Context) return;

        const SourceManager& sm = r.Context->getSourceManager();
        const LangOptions& lang = r.Context->getLangOpts();

        // Inner-side structural guards — mirror the Phase F shape test so
        // this callback remains robust even if the pattern widens later.
        const CallExpr* inner_call = materialize_call_in_when_if(inner_if);
        if (!inner_call) return;

        // Inner must live inside a WHEN macro body expansion. The
        // `_when_val_` init-stmt only appears inside the macro tower, but
        // a user writing a raw `if (auto _when_val_ = materialize_when(...))`
        // could still reach this callback — the macro-body guard rejects.
        const SourceLocation inner_loc = inner_if->getIfLoc();
        if (!inner_loc.isValid() || !inner_loc.isMacroID()) return;
        if (!sm.isMacroBodyExpansion(inner_loc)) return;
        if (!detail::is_expansion_of_macro(inner_loc, sm, lang, "WHEN")) return;

        // Find the nearest enclosing WHEN IfStmt. If none, this inner
        // WHEN is top-level — Phase G does not apply.
        const IfStmt* outer_if =
            find_nearest_enclosing_when_if(inner_if, *r.Context);
        if (!outer_if) return;

        const CallExpr* outer_call = materialize_call_in_when_if(outer_if);
        if (!outer_call) return; // defensive — the finder just classified it.

        // Outer must also live inside a WHEN macro body expansion. We
        // re-check symmetrically: a hand-rolled outer `if (auto _when_val_
        // = ...)` wrapping a real inner WHEN must not pair up.
        const SourceLocation outer_loc = outer_if->getIfLoc();
        if (!outer_loc.isValid() || !outer_loc.isMacroID()) return;
        if (!sm.isMacroBodyExpansion(outer_loc)) return;
        if (!detail::is_expansion_of_macro(outer_loc, sm, lang, "WHEN")) return;

        // Named+named guard: both args must peel to bare DeclRefExpr
        // naming a non-synthetic qbool. `peeled_is_named_qbool` captures
        // the leaf identifier as a by-product — PG-2 / PG-3 will consume
        // it to build the `qbool __stu_ctrl<M> = <outer> & <inner>;` decl
        // text. For PG-1 we only need the booleans.
        std::string outer_name;
        std::string inner_name;
        if (!peeled_is_named_qbool(outer_call->getArg(0), outer_name)) return;
        if (!peeled_is_named_qbool(inner_call->getArg(0), inner_name)) return;

        // All guards passed. PG-1 is detection-only: bump the counter and
        // return without mutating the QUnit. PG-2 / PG-3 will expand this
        // block to schedule the decl injection, arg replacement, and
        // `uncompute_and` call.
        ++g_when_nested_detection_count;
        (void)unit_;
        (void)outer_name;
        (void)inner_name;
    }

private:
    QUnit* unit_;
};

// Callback pool — same pattern as the other matcher_*.cpp TUs. The
// callback is owned by the pool so its lifetime ties to the shared-library
// instance, matching the MatchFinder's non-owning `add_matcher` API.
std::vector<std::unique_ptr<WhenNestedCallback>>& nested_callback_pool() {
    static std::vector<std::unique_ptr<WhenNestedCallback>> pool;
    return pool;
}

} // namespace

void register_when_nested_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    // Anchor on the INNER WHEN's middle `IfStmt` — the one whose init-stmt
    // declares `_when_val_` with a `materialize_when(...)` initializer.
    // This is the same pattern shape Phase F uses, but we bind it as
    // `inner_if` and defer the outer-side discovery to the callback's
    // ParentMap walk (which enforces "nearest enclosing WHEN" pairwise
    // cascade semantics). The callback's additional guards then:
    //
    //   - confirm the inner IfLoc is inside a `WHEN` macro body,
    //   - find the nearest enclosing WHEN IfStmt ancestor,
    //   - confirm that outer IfLoc is inside a `WHEN` macro body too,
    //   - check both materialize_when args peel to bare DeclRefExprs
    //     naming a non-synthetic qbool.
    //
    // Matching only the inner anchor (rather than binding outer AND inner
    // in the matcher itself) avoids a hasDescendant-based match which
    // would spuriously pair transitive grandparents (a, c) for a
    // three-deep cascade. The parent-chain walk stops at the nearest
    // ancestor, which is exactly the pairwise semantic the Phase G plan
    // demands.
    auto materialize_call = callExpr(
        callee(functionDecl(hasName("materialize_when"))),
        argumentCountIs(1)
    );

    auto inner_pattern = ifStmt(
        hasInitStatement(declStmt(hasSingleDecl(
            varDecl(
                hasName("_when_val_"),
                hasInitializer(ignoringImplicit(materialize_call))
            )
        )))
    ).bind("inner_if");

    auto& pool = nested_callback_pool();
    pool.push_back(std::make_unique<WhenNestedCallback>(&unit));
    finder.addMatcher(inner_pattern, pool.back().get());
}

int when_nested_detection_count_for_test() {
    return g_when_nested_detection_count;
}

void reset_when_nested_detection_count_for_test() {
    g_when_nested_detection_count = 0;
}

} // namespace sturm::transpile
