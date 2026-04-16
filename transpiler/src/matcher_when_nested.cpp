// matcher_when_nested.cpp — Phase G / PG-2 nested-WHEN rewrite matcher.
//
// Detects `WHEN(outer) { WHEN(inner) { body } }` pairs where BOTH `outer`
// and `inner` peel (via `detail::peel_to_payload`) to a bare `DeclRefExpr`
// naming a non-synthetic qbool. Compound / comparator / unary shapes on
// either side stay on the runtime path (Phase F handles a compound inner
// separately).
//
// PG-1 introduced detection-only behaviour; PG-2 extends the callback to
// emit the two source-level edits every matched pair needs:
//
//   (a) `qbool __stu_ctrl<M> = <outer_name> & <inner_name>;\n` injected
//       immediately before the inner WHEN macro's spelling;
//   (b) a `QReplacement` over the inner `materialize_when` argument's
//       file char range (spelling locs normalised via
//       `Lexer::makeFileCharRange`) rewriting `WHEN(<inner>)` to
//       `WHEN(__stu_ctrl<M>)`.
//
// `uncompute_and` scheduling is deferred to PG-3: no `QOperation` is
// scheduled here. The detection counter bumps on every successful match,
// so the PG-1 matcher tests continue to observe firing.
//
// Pairwise cascade: `WHEN(a) { WHEN(b) { WHEN(c) { ... } } }` produces
// two pairs (a,b) and (b,c) — never the transitive (a,c) — because the
// callback walks up the AST parent chain and stops at the NEAREST
// enclosing WHEN IfStmt. Each pair gets its own `__stu_ctrl<M>` temp
// from a persistent per-callback `FreshNameAllocator`.
//
// Disjointness with Phase F: Phase F's `register_when_lift_matcher`
// short-circuits on bare DRE args (named-passthrough), so a given inner
// WHEN is rewritten by at most one matcher — Phase G on the named+named
// shape, Phase F on anything else.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "fresh_names.hpp"
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
#include "clang/Lex/Lexer.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
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

// Render the Phase G AND-temp decl line. Shape:
//     qbool <ctrl_name> = <outer_name> & <inner_name>;\n
// The trailing newline matches the Phase F `render_decl_block` convention
// so the inserted text lands on its own line immediately above the inner
// WHEN invocation. Kept as a free helper so the callback body stays lean;
// any future change to the decl spelling (e.g. a qualified type name) is
// a one-line edit here.
static std::string render_ctrl_decl(const std::string& ctrl_name,
                                    const std::string& outer_name,
                                    const std::string& inner_name) {
    std::string out;
    out.reserve(8 + ctrl_name.size() + outer_name.size() + inner_name.size() + 8);
    out += "qbool ";
    out += ctrl_name;
    out += " = ";
    out += outer_name;
    out += " & ";
    out += inner_name;
    out += ";\n";
    return out;
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
        // the leaf identifier as a by-product for use in the decl line.
        std::string outer_name;
        std::string inner_name;
        if (!peeled_is_named_qbool(outer_call->getArg(0), outer_name)) return;
        if (!peeled_is_named_qbool(inner_call->getArg(0), inner_name)) return;

        // ── PG-2 emission ─────────────────────────────────────────────────
        //
        // Normalise the inner materialize_when arg's source range to a
        // pure-file char range so the M9 emitter's `Rewriter::ReplaceText`
        // call can operate on it. The arg was reached through the WHEN
        // macro's materialize_when(expr) call, so its begin/end locations
        // carry macro-body encodings; `SourceManager::getSpellingLoc` peels
        // those to the underlying file locations. The `Lexer::makeFileCharRange`
        // round-trip then validates the result — if the spelling locs sit
        // in a non-representable region (scratch buffer / macro expansion
        // without a spelling) we bail without scheduling any edit. This is
        // identical to the pattern `matcher_when_lift.cpp:433-444` uses for
        // Phase F compound-arg replacement.
        const Expr* inner_arg = inner_call->getArg(0);
        if (!inner_arg) return;
        const SourceLocation spelling_begin =
            sm.getSpellingLoc(inner_arg->getBeginLoc());
        const SourceLocation spelling_end =
            sm.getSpellingLoc(inner_arg->getEndLoc());
        const CharSourceRange file_char_range =
            Lexer::makeFileCharRange(
                CharSourceRange::getTokenRange(spelling_begin, spelling_end),
                sm, lang);
        if (file_char_range.isInvalid()) return;

        // Resolve the decl-injection anchor: the file loc where the inner
        // WHEN macro is spelled. `sm.getExpansionLoc(inner_loc)` maps the
        // macro-body `if` location to the outermost spelling of `WHEN(` in
        // user source — the exact character where `InsertTextBefore` needs
        // to plant the decl so the new line sits immediately above the
        // inner WHEN invocation. Bail on any invalid result.
        const SourceLocation decl_anchor = sm.getExpansionLoc(inner_loc);
        if (decl_anchor.isInvalid()) return;

        // Allocate the control-temp name from the PERSISTENT per-callback
        // allocator (PG-0's `FreshNameAllocator::next_ctrl()`). A single
        // allocator instance spans every invocation of this callback across
        // the translation unit, so a three-deep `WHEN(a) { WHEN(b) { WHEN(c)
        // { ... } } }` gets `__stu_ctrl0` for one pair and `__stu_ctrl1`
        // for the other — no collisions, monotone numbering in source
        // order. Re-running the matcher on a single TU a second time (e.g.
        // inside a unit test) does not reset the counter; the test harness
        // constructs a fresh callback per run so each invocation starts
        // from zero naturally.
        const std::string ctrl_name = ctrl_alloc_.next_ctrl();

        // Stage the replacement: inner materialize_when arg → ctrl_name.
        // The `QReplacement::range` uses the SourceRange form (Rewriter's
        // ReplaceText(SourceRange, text) extends through the last token
        // via `Lexer::MeasureTokenLength`). We already validated the
        // equivalent char range above, so this range is guaranteed to be
        // rewritable by the emitter.
        QReplacement rep;
        rep.range = SourceRange(spelling_begin, spelling_end);
        rep.replacement = ctrl_name;
        unit_->replacements.push_back(std::move(rep));

        // Stage the decl-block insertion: `qbool __stu_ctrl<M> = <outer>
        // & <inner>;\n` immediately before the inner WHEN spelling. The
        // M9 emitter walks `raw_insertions` after applying every
        // replacement, so the two edits compose without overlap: the
        // replacement sits inside the `WHEN(...)` arg list, the insertion
        // sits just before the `W`. Nothing in this slice schedules an
        // `uncompute_and` op — that lands in PG-3.
        UncomputeInsertion decl_block;
        decl_block.insert_before = decl_anchor;
        decl_block.code = render_ctrl_decl(ctrl_name, outer_name, inner_name);
        unit_->raw_insertions.push_back(std::move(decl_block));

        // All guards passed and both edits staged. Detection counter bump
        // remains intact so PG-1's tests stay green.
        ++g_when_nested_detection_count;
    }

private:
    QUnit* unit_;

    // Persistent allocator — lives for the callback's lifetime, which ties
    // to the MatchFinder's run. A single TU with multiple matched pairs
    // (e.g. the depth-3 cascade) consumes multiple `next_ctrl()` calls in
    // source order, producing monotonically-numbered `__stu_ctrl<M>` names
    // with no cross-match collisions. The M24/PG-0 plan required this
    // behaviour explicitly.
    FreshNameAllocator ctrl_alloc_;
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
