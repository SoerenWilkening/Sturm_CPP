// matcher_qint_const.cpp — Phase B (PB-1..PB-4) qint-classical compound-assign.
//
// Four near-identical matchers for `a <op>= <classical>;` on a `qint_t<W>`:
//
//   - PB-1 : `a += <expr>;` → QOpKind::ADD_ASSIGN_CONST
//   - PB-2 : `a -= <expr>;` → QOpKind::SUB_ASSIGN_CONST
//   - PB-3 : `a *= <expr>;` → QOpKind::MUL_ASSIGN_CONST
//   - PB-4 : `a /= <expr>;` → QOpKind::DIV_ASSIGN_CONST
//
// The RHS reaches the qint_t operator through the implicit qint_t(int64_t)
// converting constructor (qint_core.hpp:89). In the AST this shows up as a
// CXXConstructExpr wrapping the user-written source expression. We peel one
// extra layer beyond PA-4 so the bound `rhs_expr` points at the literal
// token, and Lexer::getSourceText returns the verbatim text ("3" instead
// of "qint_t(3)").
//
// The LHS type guard peels through the typedef + TemplateSpecializationType
// sugar the real qint_t<Width> wears (qint_core.hpp:52):
//     hasCanonicalType(hasDeclaration(cxxRecordDecl(hasName("qint_t"))))
// A bare `hasType(cxxRecordDecl(hasName("qint_t")))` does NOT fire on
// `qint_t<1>` because the DeclRefExpr's type is a sugared
// TemplateSpecializationType, not a RecordType directly.

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

namespace sturm_matcher_qint_const_anon_ns {

using namespace clang;
using namespace clang::ast_matchers;
using detail::enclosing_scope;
using detail::find_or_create_scope;
using detail::make_ref;

// Shared body for all four Phase B callbacks: walk scope, extract RHS
// source text, push one QOperation of the supplied kind. Factoring this
// lets each concrete callback collapse to a one-liner and keeps the
// module size tight.
template <QOpKind Kind>
class QIntAssignConstCallback : public MatchFinder::MatchCallback {
public:
    explicit QIntAssignConstCallback(QUnit* unit) : unit_(unit) {}
    void run(const MatchFinder::MatchResult& r) override {
        const auto* call = r.Nodes.getNodeAs<CXXOperatorCallExpr>("call");
        const auto* lhs  = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        const auto* rhs  = r.Nodes.getNodeAs<Expr>("rhs_expr");
        if (!call || !lhs || !rhs || !r.Context) return;

        // Phase H PH-1: `enclosing_scope` transparently supports braced
        // CompoundStmt and braceless for/while/if/else body positions.
        const auto es = enclosing_scope(*call, *r.Context);
        if (!es.valid()) return;

        const SourceManager& sm_es = r.Context->getSourceManager();
        const LangOptions& lang_es = r.Context->getLangOpts();
        QScope& scope = find_or_create_scope(*unit_, es, sm_es, lang_es);

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

using AddAssignConstCallback =
    QIntAssignConstCallback<QOpKind::ADD_ASSIGN_CONST>;
using SubAssignConstCallback =
    QIntAssignConstCallback<QOpKind::SUB_ASSIGN_CONST>;
using MulAssignConstCallback =
    QIntAssignConstCallback<QOpKind::MUL_ASSIGN_CONST>;
using DivAssignConstCallback =
    QIntAssignConstCallback<QOpKind::DIV_ASSIGN_CONST>;

template <typename Cb>
std::vector<std::unique_ptr<Cb>>& const_callback_pool() {
    static std::vector<std::unique_ptr<Cb>> pool;
    return pool;
}

// Build the matcher pattern for a compound-assign of operator name `op`
// against a qint_t LHS and a CXXConstructExpr-wrapped classical RHS. The
// pattern shape is identical across PB-1..PB-4, only the operator name
// changes, so we build it through a helper rather than re-typing four
// near-identical 14-line blocks.
template <typename OperatorName>
auto make_qint_const_pattern(OperatorName op_name) {
    return cxxOperatorCallExpr(
        hasOverloadedOperatorName(op_name),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(
            declRefExpr(hasType(hasCanonicalType(hasDeclaration(
                cxxRecordDecl(hasName("qint_t"))))))
                .bind("lhs"))),
        hasArgument(1, ignoringImplicit(cxxConstructExpr(
            argumentCountIs(1),
            hasArgument(0, ignoringImplicit(
                expr().bind("rhs_expr"))))))
    ).bind("call");
}

template <typename Cb, typename OperatorName>
void register_qint_const(clang::ast_matchers::MatchFinder& finder,
                         QUnit& unit, OperatorName op_name) {
    auto& pool = const_callback_pool<Cb>();
    pool.push_back(std::make_unique<Cb>(&unit));
    finder.addMatcher(make_qint_const_pattern(op_name), pool.back().get());
}

} // namespace sturm_matcher_qint_const_anon_ns
using namespace sturm_matcher_qint_const_anon_ns;

// PB-1. See matcher.hpp for the rationale behind the LHS canonical-type
// peel and the CXXConstructExpr peel on the RHS; both are identical across
// PB-1..PB-4 so the detailed comment lives there (and on the helpers above).
// Each registrar below is a one-liner that differs only in operator name
// and QOpKind.
void register_add_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_const<AddAssignConstCallback>(finder, unit, "+=");
}

void register_sub_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_const<SubAssignConstCallback>(finder, unit, "-=");
}

void register_mul_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_const<MulAssignConstCallback>(finder, unit, "*=");
}

void register_div_assign_const_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit) {
    register_qint_const<DivAssignConstCallback>(finder, unit, "/=");
}

// ── PM4-6: dogfood — migrate PB-1..PB-4 to the plugin Registry API ──────────
//
// Pre-PM4-6 the four Phase B matchers above were wired into the
// transpiler via direct calls at `transpile_consumer.cpp:178-181`:
//
//     sturm::transpile::register_add_assign_const_matcher(finder_, unit_);
//     sturm::transpile::register_sub_assign_const_matcher(finder_, unit_);
//     sturm::transpile::register_mul_assign_const_matcher(finder_, unit_);
//     sturm::transpile::register_div_assign_const_matcher(finder_, unit_);
//
// PM4-6 removes those four lines in favour of the new Registry API:
// registration now flows through `STURM_REGISTER_PLUGIN(...)` at
// namespace scope below. The macro expands to a file-local
// `StaticRegistrar` whose ctor runs during static-init of this TU and
// pushes a `LinkTimeRegisterFn` onto the Meyer's-singleton vector
// returned by `::sturm::transpile::plugin::registrars()`. The host
// `TranspileConsumer`'s constructor drains that vector (plan §6:
// in-tree → runtime-dlopen → link-time) and invokes every entry against
// its per-consumer `Registry&`. Each entry calls
// `register_matcher(name, fn)` four times — one per PB kind — and
// `Registry::invoke_all(finder, unit)` at the tail of the consumer ctor
// fires each fn against the shared `finder_` + `unit_`.
//
// Implementation detail: the `register_all(Registry&)` method calls
// through to the four existing free functions (`register_{add,sub,mul,
// div}_assign_const_matcher`). This keeps the matcher-unit-test path
// (which registers the free functions directly against a hand-built
// MatchFinder in `test_matcher_qint_const.cpp`) working byte-for-byte
// while adding the new Registry-side entrypoint. There is NO change to
// any `QOpKind` emitted by the four matchers, and NO change to the
// rendered uncompute text — only the registration plumbing moves.
//
// Ordering (happy-path, plan §6)
// ------------------------------
// The consumer ctor drains `registrars()` and calls `invoke_all` BEFORE
// the Phase H PH-3 outer-variable-mutation guard is registered, so the
// PB-1..PB-4 matchers are added to `finder_` in the same relative
// position as before (after PA-3/PA-4 xor-assign, before PC-1..PC-5
// qint-qint). This preserves PH-3's documented invariant that the
// Phase A–C compound-assign matchers have already populated
// `unit.scopes` by the time PH-3's callback fires. The four PB ops
// therefore remain eligible for PH-3's `skip_uncompute=true` flagging
// on outer-var mutations, identical to pre-PM4-6 behaviour. See
// `transpile_consumer.cpp`'s PM4-3 drain block for the exact ordering
// comment.
//
// Snapshot gate
// -------------
// All 64 byte-identical snapshot fixtures remain green post-migration
// because the four PB matchers use the SAME `QIntAssignConstCallback<
// Kind>` template shared with the pre-migration path — registration
// flows through a different entrypoint, but the AST shapes matched,
// the QOperation fields populated, and the M8 uncompute text rendered
// are untouched.
} // namespace sturm::transpile

// STURM_REGISTER_PLUGIN must be invoked at namespace scope OUTSIDE the
// `sturm::transpile` namespace — the macro's token-paste on `TypeName`
// (`_sturm_reg_##TypeName`) expects an unqualified identifier. We
// park both the registrar struct and its `STURM_REGISTER_PLUGIN` call
// in a TU-local anonymous namespace so two PM4 dogfood TUs that each
// bake a `PBDogfoodPlugin` cannot clash at link time. This mirrors the
// trick `pm4_link_demo_registrar.cpp` uses for the same reason
// (`LinkTimeDemoPluginAlias`).
namespace {

struct PBDogfoodPlugin {
    void register_all(::sturm::transpile::plugin::Registry& r) const {
        // Each `register_matcher` call binds a human-readable name to
        // a `MatcherRegisterFn`. The lambda captures nothing — the
        // host-supplied `finder` + `unit` are the two parameters
        // `register_matcher`'s signature threads in. Delegating to the
        // four free functions above keeps the Registry-side glue thin
        // and makes the dogfood shape trivially inspectable: "every
        // entry is a one-line forward to the pre-existing registrar".
        //
        // The matcher names use dotted prefixes to keep the dogfood
        // family namespace-scoped — a future plugin registering its
        // own "add" matcher cannot collide with ours, and the hard-
        // error collision-check in `Registry::register_matcher`
        // prints the exact key on conflict, which makes the source of
        // the clash obvious from the build log alone.
        r.register_matcher("sturm.pb.add_assign_const",
            [](clang::ast_matchers::MatchFinder& finder,
               ::sturm::transpile::QUnit& unit) {
                ::sturm::transpile::register_add_assign_const_matcher(
                    finder, unit);
            });
        r.register_matcher("sturm.pb.sub_assign_const",
            [](clang::ast_matchers::MatchFinder& finder,
               ::sturm::transpile::QUnit& unit) {
                ::sturm::transpile::register_sub_assign_const_matcher(
                    finder, unit);
            });
        r.register_matcher("sturm.pb.mul_assign_const",
            [](clang::ast_matchers::MatchFinder& finder,
               ::sturm::transpile::QUnit& unit) {
                ::sturm::transpile::register_mul_assign_const_matcher(
                    finder, unit);
            });
        r.register_matcher("sturm.pb.div_assign_const",
            [](clang::ast_matchers::MatchFinder& finder,
               ::sturm::transpile::QUnit& unit) {
                ::sturm::transpile::register_div_assign_const_matcher(
                    finder, unit);
            });
    }
};

} // anonymous namespace

STURM_REGISTER_PLUGIN(PBDogfoodPlugin);
