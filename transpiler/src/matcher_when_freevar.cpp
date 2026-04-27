// matcher_when_freevar.cpp — E7.M3 wiring for the WHEN free-variable
// write-set checker.
//
// E7.M1 (`compute_when_freevar_readset`) and E7.M2
// (`check_when_freevar_writes`) ship the analytical pieces; this TU
// glues them onto the same `MatchFinder` anchor pattern the Phase F /
// PM3-4 WHEN matchers use, so the checker fires automatically on every
// `WHEN(expr) { body }` invocation in a translation unit. The matcher
// is a hard-error pass: each detected write becomes a
// `DiagnosticsEngine::Error` (severity is owned by the M2 helper's
// `getCustomDiagID(Error, ...)` call), so the standalone driver exits
// non-zero whenever a free variable of the WHEN control expression is
// mutated inside the body.
//
// Anchor shape
// ------------
// The same three-`if` WHEN tower the Phase F lift matcher and the
// PM3-4 operand-mutation matcher key on. Anchoring on the middle
// `IfStmt` whose init-stmt declares `_when_val_` and whose initializer
// is `materialize_when(expr)` gives the callback both the WHEN's
// argument expression (E7.M1's input) and a path into the user's body
// (E7.M2's input) without re-walking the AST.
//
// Pipeline ordering
// -----------------
// Issue spec: "ensure it runs after WHEN scope is identified". The
// Phase F lift matcher (`register_when_lift_matcher`) is what
// identifies and rewrites the WHEN scope structurally; both matchers
// anchor on the same node but Phase F is registered earlier in
// `transpile_consumer.cpp`. MatchFinder invokes callbacks in
// registration order on a matched node, so registering
// `register_when_freevar_matcher` AFTER the WHEN-lift matcher
// guarantees the lift callback has fired (and the QUnit's WHEN-scope
// bookkeeping is current) before the freevar diagnostic runs.
//
// Disjointness with PM3-4
// -----------------------
// PM3-4 (`matcher_when_operand_mutation.cpp`) flags mutations of a
// quantum-typed (`qbool` / `qint`) operand of the WHEN control
// expression — a *narrow* shape, scoped to types it can identify by
// name. E7's read-set is *broader*: any classical or quantum variable
// that the WHEN expression reads (transitively through callee bodies)
// is in scope, and any write to any of those is a hard error. The two
// matchers fire on disjoint mutation shapes when the WHEN argument has
// a quantum operand: PM3-4 catches `WHEN(a | b) { a ^= 1; }` (qbool
// shape) and the freevar checker catches `WHEN(threshold > 0) { ++threshold; }`
// (classical shape) — together they cover both halves of the P4
// "operands of WHEN are immutable" invariant. Both report through
// independent diag IDs so the same line cannot double-fire.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"

#include "diag_context.hpp"
#include "matcher_common.hpp"
#include "when_freevar_check.hpp"
#include "when_freevar_readset.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/Casting.h"

#include <memory>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

class WhenFreeVarCallback : public MatchFinder::MatchCallback {
public:
    explicit WhenFreeVarCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* when_if = r.Nodes.getNodeAs<IfStmt>("when_if");
        const auto* mat_call =
            r.Nodes.getNodeAs<CallExpr>("materialize_call");
        if (!when_if || !mat_call || !r.Context) return;

        const SourceManager& sm = r.Context->getSourceManager();
        const LangOptions& lang = r.Context->getLangOpts();

        // Macro-body expansion guards — same three-stage filter the
        // PM3-4 matcher uses so user code that happens to spell a
        // local `_when_val_` cannot accidentally trip the diagnostic.
        const SourceLocation if_loc = when_if->getIfLoc();
        if (!if_loc.isValid() || !if_loc.isMacroID()) return;
        if (!sm.isMacroBodyExpansion(if_loc)) return;
        if (!detail::is_expansion_of_macro(if_loc, sm, lang, "WHEN")) {
            return;
        }

        // Pull the WHEN argument expression out of the matched
        // `materialize_when(<arg>)` call.
        if (mat_call->getNumArgs() != 1) return;
        const Expr* arg = mat_call->getArg(0);
        if (!arg) return;

        // Descend `IfStmt::getThen()` twice to reach the user's body
        // CompoundStmt — the same pattern the Phase F lift matcher
        // and PM3-4 operand-mutation matcher use. Any structural
        // mismatch (e.g. the macro body shape is not the two-`if`
        // tower we expect) bails cleanly.
        const Stmt* first = when_if->getThen();
        const auto* inner_if = llvm::dyn_cast_or_null<IfStmt>(first);
        if (!inner_if) return;
        const Stmt* second = inner_if->getThen();
        const auto* body =
            llvm::dyn_cast_or_null<CompoundStmt>(second);
        if (!body) return;

        // E7.M1: read-set of the WHEN control expression. Transitive
        // through callee bodies the TU can see; flips conservative on
        // unanalyzed callees but the checker still fires on whatever
        // the visible analysis collected.
        const WhenFreeVarReadSet readset =
            compute_when_freevar_readset(arg);
        if (readset.vars.empty()) return;

        // E7.M2: walk the body for assignment-shape writes whose
        // target sits in the read-set and emit one hard-error
        // diagnostic per write through the parent CompilerInstance's
        // engine. The M2 helper does its own `getFileLoc` funnelling
        // so the diagnostic cites user source rather than the
        // plugin's nested memory buffer.
        DiagnosticsEngine& engine = r.Context->getDiagnostics();
        check_when_freevar_writes(
            body,
            readset,
            engine,
            sm,
            arg->getBeginLoc());

        (void)unit_; // intentionally not mutated — advisory only.
    }

private:
    QUnit* unit_;
};

// Callback pool — matches the lifetime convention used by every other
// matcher_*.cpp module. The MatchFinder stores raw callback pointers so
// the pool keeps the unique_ptrs alive for the duration of the run.
std::vector<std::unique_ptr<WhenFreeVarCallback>>&
when_freevar_callback_pool() {
    static std::vector<std::unique_ptr<WhenFreeVarCallback>> pool;
    return pool;
}

} // namespace

void register_when_freevar_matcher(
    clang::ast_matchers::MatchFinder& finder,
    QUnit& unit) {
    // Anchor: middle `IfStmt` of the three-`if` WHEN tower. Identical
    // to the Phase F / PM3-4 anchor — `_when_val_` init-stmt with a
    // `materialize_when(...)` initializer.
    auto materialize_call = callExpr(
        callee(functionDecl(hasName("materialize_when"))),
        argumentCountIs(1)
    ).bind("materialize_call");

    auto pattern = ifStmt(
        hasInitStatement(declStmt(hasSingleDecl(
            varDecl(
                hasName("_when_val_"),
                hasInitializer(ignoringImplicit(materialize_call))
            )
        )))
    ).bind("when_if");

    auto& pool = when_freevar_callback_pool();
    pool.push_back(std::make_unique<WhenFreeVarCallback>(&unit));
    finder.addMatcher(pattern, pool.back().get());
}

} // namespace sturm::transpile
