// matcher_rotation.cpp — Phase N PN-2 rotation compound-assign matchers.
//
// Four near-identical matchers for the continuous-parameter rotations that
// Phase N adds (P5 items 2 and 3):
//
//   - PN-2a : `q.theta() += d;` → QOpKind::THETA_ADD_ASSIGN_CONST
//   - PN-2b : `q.theta() -= d;` → QOpKind::THETA_SUB_ASSIGN_CONST
//   - PN-2c : `q.phi()   += d;` → QOpKind::PHI_ADD_ASSIGN_CONST
//   - PN-2d : `q.phi()   -= d;` → QOpKind::PHI_SUB_ASSIGN_CONST
//
// AST shape is **one level deeper** than Phase B's `a += C;`. The user's
// `q.theta() += 0.5;` lowers to the following CXXOperatorCallExpr:
//
//     operator+=(
//         /*arg0 — the proxy receiver*/
//         CXXMemberCallExpr(
//             on: DeclRefExpr(qint_t q),    // <-- bound as "lhs"
//             callee: CXXMethodDecl("theta")
//         ),
//         /*arg1 — the RHS delta*/
//         Expr(0.5)                          // <-- bound as "rhs_expr"
//     )
//
// Differences from `matcher_qint_const.cpp`'s Phase B template:
//
//   1. The LHS guard is wrapped in a `cxxMemberCallExpr(on(...),
//      callee(cxxMethodDecl(hasName("theta"/"phi"))))` instead of being the
//      top-level `declRefExpr` argument. The inner `DeclRefExpr` is what
//      we bind as "lhs" — that's the qint_t identifier the user spells.
//
//   2. There is NO `CXXConstructExpr` peel on the RHS. The proxy's
//      `operator+=(double)` takes a `double` by value, not a converted
//      `qint_t`, so the argument is already a bare `Expr` pointing at the
//      user-written double-valued expression (a `FloatingLiteral` for
//      `0.5`, a `DeclRefExpr` for `d`, a `CallExpr` for `compute()`,
//      etc.). `Lexer::getSourceText` captures it verbatim.
//
//   3. No free-function inverse helper. The M8 uncompute pass (PN-4) emits
//      the sign-flipped line inline — `q.theta() -= 0.5;` for the
//      `THETA_ADD_ASSIGN_CONST` forward — because the runtime's
//      `ThetaProxy::operator-=(double)` at
//      `include/sturm/qtypes/qint_core.hpp:305` forwards to
//      `operator+=(-delta)` and is self-dual w.r.t. the counter-mode
//      `GateRecord` stream. The same pattern applies to the three other
//      directions.
//
// Dogfood: the four registrars below are bundled into a `PNRotationPlugin`
// class at the bottom of this file and announced via
// `STURM_REGISTER_PLUGIN(PNRotationPlugin)`. The `TranspileConsumer`'s
// Registry drain (PM4-3 / PM4-6 block in `transpile_consumer.cpp`) picks
// them up at static-init time and registers them against the shared
// `MatchFinder` in the same relative position the PB-1..PB-4 matchers
// occupy — so no consumer-side edit is needed to wire Phase N's
// coverage into the pipeline.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/plugin_api.hpp"
#include "sturm/transpile/qir.hpp"
#include "matcher_common.hpp"

#include "clang/AST/ASTContext.h"
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

namespace sturm_matcher_rotation_anon_ns {

using namespace clang;
using namespace clang::ast_matchers;
using detail::enclosing_scope;
using detail::find_or_create_scope;
using detail::make_ref;

// Shared body for all four Phase N rotation callbacks. The `run()` logic
// is identical across `theta` / `phi` and `+=` / `-=` because the AST
// binding names ("lhs", "rhs_expr", "call") are stable and the only
// differences are (a) the QOpKind we stamp into the IR op, and (b) the
// method name / operator name the matcher pattern specifies (decided at
// pattern-build time in `make_rotation_pattern` below). Factoring the
// body keeps the module short and makes the four registrar bodies
// trivially inspectable.
template <QOpKind Kind>
class RotationCallback : public MatchFinder::MatchCallback {
public:
    explicit RotationCallback(QUnit* unit) : unit_(unit) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* call = r.Nodes.getNodeAs<CXXOperatorCallExpr>("call");
        const auto* lhs  = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* rhs  = r.Nodes.getNodeAs<Expr>("rhs_expr");
        if (!call || !lhs || !rhs || !r.Context) return;

        // Phase H PH-1: `enclosing_scope` transparently supports braced
        // CompoundStmt and braceless for/while/if/else body positions.
        // A rotation inside a `for (...) q.theta() += d;` braceless body
        // therefore creates a synthetic QScope the same way Phase B's
        // compound-assigns do.
        const auto es = enclosing_scope(*call, *r.Context);
        if (!es.valid()) return;

        const SourceManager& sm_es = r.Context->getSourceManager();
        const LangOptions& lang_es = r.Context->getLangOpts();
        QScope& scope = find_or_create_scope(*unit_, es, sm_es, lang_es);

        // Capture the RHS source text verbatim — same policy as Phase B's
        // `matcher_qint_const.cpp` — so the M8 uncompute pass can emit
        // `q.theta() -= <rhs_text>;` without knowing whether the user
        // wrote a literal, a variable, or a compound expression. The
        // Phase N §14 risk 2 note points out that side-effecting RHS is
        // the user's responsibility (same posture as Phase B).
        const SourceManager& sm = r.Context->getSourceManager();
        const LangOptions& lo = r.Context->getLangOpts();
        auto text = clang::Lexer::getSourceText(
            clang::CharSourceRange::getTokenRange(rhs->getSourceRange()),
            sm, lo);
        if (text.empty()) return;

        QValueRef rhs_ref;
        rhs_ref.name = text.str();

        QOperation op;
        op.kind   = Kind;
        op.result = make_ref(*lhs);
        op.operands.push_back(std::move(rhs_ref));
        op.stmt_range = call->getSourceRange();
        scope.ops.push_back(std::move(op));
    }
private:
    QUnit* unit_;
};

using ThetaAddCallback = RotationCallback<QOpKind::THETA_ADD_ASSIGN_CONST>;
using ThetaSubCallback = RotationCallback<QOpKind::THETA_SUB_ASSIGN_CONST>;
using PhiAddCallback   = RotationCallback<QOpKind::PHI_ADD_ASSIGN_CONST>;
using PhiSubCallback   = RotationCallback<QOpKind::PHI_SUB_ASSIGN_CONST>;

// Per-callback storage pool. Same idiom the Phase B / Phase C matchers use:
// MatchFinder does not own the callbacks it receives, so we keep them
// alive in a TU-local static vector for the lifetime of the process. One
// pool per callback type so unrelated `register_*_matcher` re-invocations
// do not clash (the Phase B pool keeps its own storage).
template <typename Cb>
std::vector<std::unique_ptr<Cb>>& rotation_callback_pool() {
    static std::vector<std::unique_ptr<Cb>> pool;
    return pool;
}

// Build the matcher pattern for a rotation compound-assign. The two free
// parameters are the operator name (`"+="` or `"-="`) and the proxy-method
// name (`"theta"` or `"phi"`). The LHS guard uses the same
// `hasCanonicalType(hasDeclaration(cxxRecordDecl(hasName("qint_t"))))`
// peel the Phase B matcher does — without it the sugared
// TemplateSpecializationType on `qint_t<W>` does not reduce to the
// RecordDecl.
template <typename OperatorName, typename MethodName>
auto make_rotation_pattern(OperatorName op_name, MethodName method_name) {
    return cxxOperatorCallExpr(
        hasOverloadedOperatorName(op_name),
        argumentCountIs(2),
        // arg-0: the proxy receiver — `q.theta()` or `q.phi()`. We bind
        // the INNER `DeclRefExpr` (the qint_t identifier) as "lhs" so the
        // callback can resolve it directly without re-walking the member
        // call. `on(...)` peels the implicit-object argument; its
        // `ignoringImplicit` wrapper discards the `CXXThisExpr` /
        // materialize-temp wrappers Clang may insert there.
        hasArgument(0, ignoringImplicit(cxxMemberCallExpr(
            on(ignoringImplicit(
                declRefExpr(hasType(hasCanonicalType(hasDeclaration(
                    cxxRecordDecl(hasName("qint_t"))))))
                    .bind("lhs"))),
            callee(cxxMethodDecl(hasName(method_name)))))),
        // arg-1: the RHS delta. Plain Expr — no `CXXConstructExpr` peel
        // needed because the proxy's `operator+=(double)` takes `double`
        // by value. `ignoringImplicit` peels any `LValueToRValue` /
        // `IntegralCast` wrappers so `Lexer::getSourceText` lands on
        // the user-written token range.
        hasArgument(1, ignoringImplicit(expr().bind("rhs_expr")))
    ).bind("call");
}

// Thin registrar helper — same shape as `register_qint_const` /
// `register_qint_qint` in the Phase B / Phase C matcher files. Allocates
// one fresh callback per invocation, parks it in the per-callback pool
// for lifetime ownership, and adds the matcher to `finder`.
template <typename Cb, typename OperatorName, typename MethodName>
void register_rotation(clang::ast_matchers::MatchFinder& finder,
                       QUnit& unit,
                       OperatorName op_name,
                       MethodName method_name) {
    auto& pool = rotation_callback_pool<Cb>();
    pool.push_back(std::make_unique<Cb>(&unit));
    finder.addMatcher(make_rotation_pattern(op_name, method_name),
                      pool.back().get());
}

} // namespace sturm_matcher_rotation_anon_ns
using namespace sturm_matcher_rotation_anon_ns;

// PN-2a. `q.theta() += d;` → THETA_ADD_ASSIGN_CONST.
void register_theta_add_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_rotation<ThetaAddCallback>(finder, unit, "+=", "theta");
}

// PN-2b. `q.theta() -= d;` → THETA_SUB_ASSIGN_CONST.
void register_theta_sub_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_rotation<ThetaSubCallback>(finder, unit, "-=", "theta");
}

// PN-2c. `q.phi() += d;` → PHI_ADD_ASSIGN_CONST.
void register_phi_add_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_rotation<PhiAddCallback>(finder, unit, "+=", "phi");
}

// PN-2d. `q.phi() -= d;` → PHI_SUB_ASSIGN_CONST.
void register_phi_sub_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_rotation<PhiSubCallback>(finder, unit, "-=", "phi");
}

} // namespace sturm::transpile

// ── PN-2 dogfood: plugin Registry registration ──────────────────────────────
//
// Following the PM4-6 pattern established by `matcher_qint_const.cpp`'s
// `PBDogfoodPlugin`, the four Phase N rotation matchers above are bundled
// into a single plugin type that registers every one via
// `Registry::register_matcher`. The `TranspileConsumer`'s PM4-3 / PM4-6
// drain block (in `transpile_consumer.cpp`) iterates the link-time
// `registrars()` vector and invokes each registered
// `MatcherRegisterFn` against its per-consumer `finder_` + `unit_`. This
// is what installs the PN-2 matchers into the transpiler pipeline at
// run time — there is NO consumer-side call to any of the four
// `register_*` free functions above.
//
// `STURM_REGISTER_PLUGIN` must be invoked at namespace scope OUTSIDE the
// `sturm::transpile` namespace — the macro's token-paste on `TypeName`
// (`_sturm_reg_##TypeName`) expects an unqualified identifier. We park
// both the registrar struct and its `STURM_REGISTER_PLUGIN` call in a
// TU-local anonymous namespace so two PM4 dogfood TUs that each bake a
// `PNRotationPlugin` cannot clash at link time. Mirrors the
// `PBDogfoodPlugin` / `LinkTimeDemoPluginAlias` trick.
namespace {

struct PNRotationPlugin {
    void register_all(::sturm::transpile::plugin::Registry& r) const {
        // One `register_matcher` call per direction. The matcher keys use
        // the `sturm.pn.*` dotted prefix so a future plugin registering
        // a same-named matcher cannot collide — the `Registry::register_
        // matcher` collision check prints the exact key on conflict.
        r.register_matcher("sturm.pn.theta_add",
            [](clang::ast_matchers::MatchFinder& finder,
               ::sturm::transpile::QUnit& unit) {
                ::sturm::transpile::register_theta_add_matcher(finder, unit);
            });
        r.register_matcher("sturm.pn.theta_sub",
            [](clang::ast_matchers::MatchFinder& finder,
               ::sturm::transpile::QUnit& unit) {
                ::sturm::transpile::register_theta_sub_matcher(finder, unit);
            });
        r.register_matcher("sturm.pn.phi_add",
            [](clang::ast_matchers::MatchFinder& finder,
               ::sturm::transpile::QUnit& unit) {
                ::sturm::transpile::register_phi_add_matcher(finder, unit);
            });
        r.register_matcher("sturm.pn.phi_sub",
            [](clang::ast_matchers::MatchFinder& finder,
               ::sturm::transpile::QUnit& unit) {
                ::sturm::transpile::register_phi_sub_matcher(finder, unit);
            });
    }
};

} // anonymous namespace

STURM_REGISTER_PLUGIN(PNRotationPlugin);
