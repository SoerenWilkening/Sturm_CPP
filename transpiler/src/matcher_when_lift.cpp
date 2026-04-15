// matcher_when_lift.cpp — Phase F / PF-2 WHEN(expr) macro detection matcher.
//
// Purpose
// -------
// Detects `WHEN(expr) { body }` macro invocations in user source. PF-2 is
// detection-only; PF-3 will layer the three-point rewrite on top. Today the
// callback:
//
//   (1) Matches the middle `if` statement in the three-`if` tower the
//       `WHEN` macro expands to — specifically, the `if` whose init-stmt
//       declares `_when_val_` with initializer
//       `::sturm::detail::materialize_when(arg)`.
//   (2) Guards the match with `SourceManager::isMacroBodyExpansion` + a
//       walk up the `getImmediateMacroCallerLoc` chain to confirm the
//       immediate-expansion macro spelling is the literal token `WHEN`.
//       This rejects a user call to `materialize_when(...)` outside any
//       macro context — the init-stmt shape alone is not sufficient.
//   (3) Short-circuits named-passthrough: when the materialize_when
//       argument (after `detail::peel_to_payload`) is a bare `DeclRefExpr`
//       to a qbool, the callback returns without scheduling any rewrite.
//       The final lift path (PF-3) would produce no edits for this shape
//       regardless, so the short-circuit is a correctness + performance
//       win both at PF-2 and PF-3.
//
// No `QOperation`, no `QReplacement`, no `QUnit::raw_insertions` is
// produced by this matcher today. Emission is entirely PF-3's concern.
// The detection callback is still valuable standalone: the PF-3 slice
// will only add the scheduling logic, leaving this slice's match-shape
// and guards untouched.
//
// Disjointness from the Phase E compound matcher: that matcher keys on a
// VarDecl whose type is `qbool` and whose initializer is a
// `CXXOperatorCallExpr`. Phase F keys on an `IfStmt` whose init-stmt
// declares `_when_val_` (not a `qbool` VarDecl — it binds `decltype(auto)`,
// which may or may not be `qbool&`). Even if the name collided, the
// initializer of `_when_val_` is a plain `CallExpr` (to `materialize_when`),
// not a `CXXOperatorCallExpr`, so Phase E's `hasInitializer(anyOf(...))`
// rejects it. No double-bind.

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
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

// Test-only detection counter. Incremented by `WhenLiftCallback::run` on
// every run that passes every guard and is NOT short-circuited by the
// named-passthrough path. The PF-2 unit tests read this value via
// `when_lift_detection_count_for_test()` to pin down the "exactly-one
// match for WHEN(b | c)" / "zero matches for WHEN(named_qbool)" etc.
// acceptance criteria. Production code never reads it; PF-3 and later
// will observe `QUnit` mutations instead.
//
// A plain non-atomic int is fine — the transpiler is single-threaded by
// construction (LibTooling drives one AST at a time).
static int g_when_lift_detection_count = 0;

using namespace clang;
using namespace clang::ast_matchers;

// Walk up the macro-expansion chain from `loc` and look for an immediate
// caller whose spelled macro name is `needle` (e.g. "WHEN"). Returns true
// if any step on the caller chain spells that macro. A pure textual
// comparison against `Lexer::getImmediateMacroName` is intentional: the
// `WHEN` macro is defined in a specific header today, but we do not want
// to pin the lookup to a particular FileID / expansion depth — a user
// wrapper `#define MY_WHEN(x) WHEN(x)` should still be recognized at the
// one-level-up step where `WHEN` is spelled.
static bool is_expansion_of_macro(SourceLocation loc,
                                  const SourceManager& sm,
                                  const LangOptions& lang,
                                  llvm::StringRef needle) {
    // Only valid for locations inside some macro body expansion.
    if (!loc.isMacroID()) return false;
    // Cap the walk — pathological circular expansions are impossible in
    // well-formed source but a belt-and-braces bound costs nothing.
    SourceLocation cur = loc;
    for (int hops = 0; hops < 64 && cur.isMacroID(); ++hops) {
        llvm::StringRef name = Lexer::getImmediateMacroName(cur, sm, lang);
        if (name == needle) return true;
        SourceLocation next = sm.getImmediateMacroCallerLoc(cur);
        if (next == cur) break; // fixed point — no more caller info.
        cur = next;
    }
    return false;
}

class WhenLiftCallback : public MatchFinder::MatchCallback {
public:
    explicit WhenLiftCallback(QUnit* unit) : unit_(unit) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* when_if = r.Nodes.getNodeAs<IfStmt>("when_if");
        const auto* when_val =
            r.Nodes.getNodeAs<VarDecl>("when_val");
        const auto* mat_call =
            r.Nodes.getNodeAs<CallExpr>("materialize_call");
        if (!when_if || !when_val || !mat_call || !r.Context) return;

        const SourceManager& sm = r.Context->getSourceManager();
        const LangOptions& lang = r.Context->getLangOpts();

        // PF-2 guard #1: the IfStmt must originate inside a macro *body*
        // expansion. A direct user `if (auto _when_val_ = ...)` is spelled
        // in file, not in a macro body, and must not match.
        const SourceLocation if_loc = when_if->getIfLoc();
        if (!if_loc.isValid() || !if_loc.isMacroID()) return;
        if (!sm.isMacroBodyExpansion(if_loc)) return;

        // PF-2 guard #2: the immediate-expansion macro must spell `WHEN`.
        // This rejects users calling `sturm::detail::materialize_when(x)`
        // from inside some *other* macro (e.g. a test harness) that
        // happens to emit an `if (auto _when_val_ = ...)` init-stmt.
        if (!is_expansion_of_macro(if_loc, sm, lang, "WHEN")) return;

        // PF-2 guard #3: the materialize call must have exactly one
        // argument (the user's WHEN expression). Any other arity is a
        // sign we've matched an unrelated overload shape.
        if (mat_call->getNumArgs() != 1) return;
        const Expr* arg = mat_call->getArg(0);
        if (!arg) return;

        // Named-passthrough short-circuit: when the user wrote
        // `WHEN(named_qbool)` the peeled argument is a bare DeclRefExpr
        // to a qbool and no rewrite is needed — the existing named temp
        // already satisfies the WHEN contract.
        const Expr* peeled = detail::peel_to_payload(arg);
        if (clang::isa_and_nonnull<DeclRefExpr>(peeled)) {
            return;
        }

        // Reaching this point means PF-2 has detected a liftable WHEN
        // invocation: the argument is some non-DRE expression (e.g.
        // `b | c`, `(b | c) & d`, `a == b`). PF-3 will schedule:
        //   - one QReplacement for the argument spelling range,
        //   - one QUnit::raw_insertions entry for the flat-decl block,
        //   - one QOperation per lifted sub-expression, each with
        //     `insert_before_override = post-body-brace`.
        //
        // PF-2 is detection only. We deliberately do NOT mutate `unit_`
        // so the acceptance-criterion snapshot diffs stay byte-identical
        // for every pre-Phase-F fixture. Instead we bump a test-only
        // counter (see g_when_lift_detection_count above) that the PF-2
        // unit tests observe via
        // `when_lift_detection_count_for_test()`.
        ++g_when_lift_detection_count;
        (void)unit_; // unused until PF-3
        (void)when_val;
    }

private:
    QUnit* unit_;
};

// Callback pool — same pattern as the other matcher_*.cpp TUs. The
// callback is owned by the pool so its lifetime ties to the shared-library
// instance, matching the MatchFinder's non-owning `add_matcher` API.
std::vector<std::unique_ptr<WhenLiftCallback>>& when_callback_pool() {
    static std::vector<std::unique_ptr<WhenLiftCallback>> pool;
    return pool;
}

} // namespace

void register_when_lift_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    // PF-2 pattern — anchor on the middle IfStmt in the three-`if`
    // `WHEN(expr)` tower. The init-stmt binds a VarDecl named exactly
    // `_when_val_` whose initializer is a CallExpr to
    // `::sturm::detail::materialize_when(...)`. The macro-body guard is
    // enforced at callback time (AST matchers do not have a direct
    // "inside macro X" predicate; the SourceManager API is the path).
    //
    // Why match the IfStmt rather than the VarDecl directly? Two reasons:
    //   (a) The PF-3 rewrite needs the IfStmt to walk down to the inner
    //       body's closing brace (see plan §3 "Body close-brace anchor").
    //       Binding the IfStmt here lets PF-3 reuse the same node without
    //       re-walking the AST in a second matcher.
    //   (b) Matching on the IfStmt naturally filters out user code that
    //       happens to name a local `_when_val_` — a VarDecl-only match
    //       would fire on any such decl regardless of enclosing shape.
    //
    // `hasName("_when_val_")` is a string compare on the declaration
    // identifier. Clang's AST matchers treat this as an exact-spelling
    // match (no namespace qualification is needed — the VarDecl's name
    // is `_when_val_` verbatim). The macro-body expansion ensures this
    // spelling is the one the WHEN macro emitted.
    auto materialize_call = callExpr(
        callee(functionDecl(hasName("materialize_when"))),
        argumentCountIs(1)
    ).bind("materialize_call");

    auto pattern = ifStmt(
        hasInitStatement(declStmt(hasSingleDecl(
            varDecl(
                hasName("_when_val_"),
                hasInitializer(ignoringImplicit(materialize_call))
            ).bind("when_val")
        )))
    ).bind("when_if");

    auto& pool = when_callback_pool();
    pool.push_back(std::make_unique<WhenLiftCallback>(&unit));
    finder.addMatcher(pattern, pool.back().get());
}

int when_lift_detection_count_for_test() {
    return g_when_lift_detection_count;
}

void reset_when_lift_detection_count_for_test() {
    g_when_lift_detection_count = 0;
}

} // namespace sturm::transpile
