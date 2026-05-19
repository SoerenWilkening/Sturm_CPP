// matcher_when_nested.cpp — Phase G nested-WHEN rewrite matcher (PG-1/2/3).
//
// Detects `WHEN(outer) { WHEN(inner) { body } }` pairs where BOTH args
// peel (via `detail::peel_to_payload`) to a bare `DeclRefExpr` naming a
// non-synthetic qbool. Compound / comparator / unary shapes on either
// side stay on the runtime path (Phase F handles a compound inner).
//
// Per-pair emission (three staged effects):
//   (a) `qbool __stu_ctrl<M> = <outer_name> & <inner_name>;\n` injected
//       on `QUnit::raw_insertions` immediately before the inner WHEN
//       macro's spelling.
//   (b) a `QReplacement` over the inner `materialize_when` arg's file
//       char range (spelling locs normalised via `Lexer::makeFileCharRange`)
//       rewriting `WHEN(<inner>)` to `WHEN(__stu_ctrl<M>)`.
//   (c) a synthetic `QOperation{kind=AND, result=<ctrl>, operands=
//       [<outer>, <inner>], insert_before_override=<post_inner_brace>}`
//       pushed onto the enclosing CompoundStmt's `QScope`. The existing
//       M8 uncompute pass renders it as `uncompute_and(<ctrl>, <outer>,
//       <inner>);` at the override anchor (immediately past the inner
//       WHEN body's closing brace) — no emitter changes required.
//
// Pairwise cascade: `WHEN(a) { WHEN(b) { WHEN(c) { ... } } }` yields two
// pairs (a,b) and (b,c) — never the transitive (a,c). The callback walks
// up the AST parent chain and stops at the NEAREST enclosing WHEN IfStmt.
// Each pair gets its own `__stu_ctrl<M>` temp from a per-callback
// persistent `FreshNameAllocator`.
//
// Disjointness with Phase F: Phase F's `register_when_lift_matcher`
// short-circuits on bare DRE args (named-passthrough), so a given inner
// WHEN is rewritten by at most one matcher.

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

namespace sturm_matcher_when_nested_anon_ns {

using namespace clang;
using namespace clang::ast_matchers;

// Test-only detection counter. Incremented once per successfully matched
// (outer, inner) named+named pair by `WhenNestedCallback::run`. The
// transpiler is single-threaded (LibTooling drives one AST at a time) so a
// plain non-atomic int is fine.
static int g_when_nested_detection_count = 0;

// A WHEN-IfStmt is the MIDDLE `if` in the three-`if` WHEN tower. Its
// init-stmt declares one VarDecl `_when_val_` whose initializer reaches
// a CallExpr to `materialize_when` through implicit-cast wrappers. This
// helper extracts that CallExpr if the IfStmt matches, else nullptr.
// Used on raw Stmt* ancestors in the parent-chain walk, where the AST
// matcher's bindings aren't available.
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
// ancestor with the WHEN shape. Returns nullptr if none exists. Realises
// the pairwise-cascade semantic: a three-deep cascade yields two pairs
// (middle+inner, outer+middle), never the transitive (outer, inner).
static const IfStmt*
find_nearest_enclosing_when_if(const IfStmt* inner_if, ASTContext& ctx) {
    if (!inner_if) return nullptr;
    DynTypedNode node = DynTypedNode::create(*inner_if);
    // Multi-parent shapes only arise in template instantiations (which
    // the matcher does not enter), so a linear first-parent walk is fine.
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

// Phase G guard: the peeled WHEN arg must be a bare DeclRefExpr to a
// non-synthetic named qbool (spelled name does NOT start with `__stu_`,
// which is reserved for transpiler-generated temps).
//
// Outparams `leaf_name` / `leaf_decl_loc` receive the identifier and
// the referenced decl's SourceLocation (NOT the DeclRefExpr's loc).
// PG-3 uses the decl_loc on the synthesised `QValueRef`s so two
// shadowing qbools with identical spellings produce distinct IR refs
// per the QValueRef equality contract.
static bool peeled_is_named_qbool(const Expr* arg,
                                  std::string& leaf_name,
                                  clang::SourceLocation& leaf_decl_loc) {
    leaf_name.clear();
    leaf_decl_loc = {};
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
    leaf_decl_loc = nd->getLocation();
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
        // the leaf identifier AND the referenced decl's SourceLocation as
        // by-products. The decl locs feed the PG-3 `QValueRef::decl_loc`
        // slots on the synthesised AND op, preserving shadow discrimination
        // per the QValueRef equality contract.
        std::string outer_name;
        std::string inner_name;
        clang::SourceLocation outer_decl_loc;
        clang::SourceLocation inner_decl_loc;
        if (!peeled_is_named_qbool(outer_call->getArg(0),
                                   outer_name, outer_decl_loc)) return;
        if (!peeled_is_named_qbool(inner_call->getArg(0),
                                   inner_name, inner_decl_loc)) return;

        // ── PG-2 emission: arg replacement + decl injection ───────────────
        //
        // Normalise the inner materialize_when arg's source range to a
        // pure-file char range so the Rewriter can edit it. The arg
        // carries macro-body encodings; `getSpellingLoc` peels them to
        // file locations, and `Lexer::makeFileCharRange` validates the
        // result (bail if the spelling locs sit in a non-representable
        // region). Identical pattern to `matcher_when_lift.cpp:433-444`.
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

        // Decl-injection anchor: file loc where the inner WHEN macro is
        // spelled. `sm.getExpansionLoc` maps the macro-body `if` to the
        // `W` of user-source `WHEN(`.
        const SourceLocation decl_anchor = sm.getExpansionLoc(inner_loc);
        if (decl_anchor.isInvalid()) return;

        // Allocate ctrl temp from the PERSISTENT per-callback allocator.
        // Spans every invocation across the TU, so a deep cascade picks
        // up monotonically increasing `__stu_ctrl<M>` indices without
        // colliding with Phase E/F's `__stu_t<N>` family.
        const std::string ctrl_name = ctrl_alloc_.next_ctrl();

        // Replacement: inner materialize_when arg → ctrl_name. The
        // SourceRange form works because we just validated the equivalent
        // char range above.
        QReplacement rep;
        rep.range = SourceRange(spelling_begin, spelling_end);
        rep.replacement = ctrl_name;
        unit_->replacements.push_back(std::move(rep));

        // Decl-block insertion: `qbool __stu_ctrl<M> = <outer> & <inner>;\n`
        // immediately before the inner WHEN spelling. The emitter walks
        // `raw_insertions` after applying replacements, so the decl
        // sits just before the `W` and the replacement sits inside the
        // arg list — no overlap.
        UncomputeInsertion decl_block;
        decl_block.insert_before = decl_anchor;
        decl_block.code = render_ctrl_decl(ctrl_name, outer_name, inner_name);
        unit_->raw_insertions.push_back(std::move(decl_block));

        // ── PG-3 scheduling: post-inner-body `uncompute_and` ──────────────
        //
        // Push a synthetic `QOperation{kind=AND, ...}` into the enclosing
        // CompoundStmt's QScope. The existing uncompute pass renders the
        // AND kind as `uncompute_and(<ctrl>, <outer>, <inner>);` and the
        // per-op `insert_before_override` (PF-1) plants the call at the
        // post-inner-body anchor — not the scope's close brace. Pairwise
        // cascade: each matched pair fires independently and anchors its
        // own uncompute at its own inner-body close, yielding one
        // `uncompute_and` per level, innermost-first in LIFO order via
        // the uncompute pass's `stable_sort` + reverse walk over
        // `scope.ops` keyed on `stmt_range.getBegin()`.
        const clang::SourceLocation post_inner_brace =
            detail::compute_post_body_brace(inner_if, sm, lang);
        if (post_inner_brace.isInvalid()) return;

        // Park the op in the enclosing scope that contains the inner WHEN.
        // The `insert_before_override` forces the post-inner-body anchor
        // regardless, but we still need a concrete scope so the uncompute
        // pass's reverse iteration fires. Phase H PH-1: `enclosing_scope`
        // transparently supports braced CompoundStmt and braceless
        // for/while/if/else body positions.
        const auto enc = detail::enclosing_scope(*inner_if, *r.Context);
        if (!enc.valid()) return;
        QScope& scope = detail::find_or_create_scope(*unit_, enc, sm, lang);

        QOperation and_op;
        and_op.kind = QOpKind::AND;
        and_op.result.name = ctrl_name;
        and_op.result.decl_loc = {}; // synthetic
        QValueRef outer_ref;
        outer_ref.name = outer_name;
        outer_ref.decl_loc = outer_decl_loc;
        QValueRef inner_ref;
        inner_ref.name = inner_name;
        inner_ref.decl_loc = inner_decl_loc;
        and_op.operands.push_back(std::move(outer_ref));
        and_op.operands.push_back(std::move(inner_ref));
        // stmt_range drives the uncompute pass's `stable_sort` tiebreaker
        // over the inner WHEN's argument expression — the same range we
        // just replaced. A cascade's deeper pair has a deeper inner-arg
        // begin loc (source-later), so the sort orders them correctly
        // before the reverse walk produces outer-first, inner-last LIFO.
        and_op.stmt_range = clang::SourceRange(spelling_begin, spelling_end);
        and_op.insert_before_override = post_inner_brace;
        scope.ops.push_back(std::move(and_op));

        // All guards passed; decl + replacement + uncompute op staged.
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

} // namespace sturm_matcher_when_nested_anon_ns
using namespace sturm_matcher_when_nested_anon_ns;

void register_when_nested_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    // Anchor on the INNER WHEN's middle `IfStmt` — the one whose init-stmt
    // declares `_when_val_` with a `materialize_when(...)` initializer.
    // Outer-side discovery is deferred to the callback's ParentMap walk,
    // which enforces "nearest enclosing WHEN" pairwise-cascade semantics.
    // A `hasDescendant`-based match would spuriously pair transitive
    // grandparents (a, c) in a three-deep cascade; anchoring on inner and
    // walking up to the nearest WHEN avoids that.
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
