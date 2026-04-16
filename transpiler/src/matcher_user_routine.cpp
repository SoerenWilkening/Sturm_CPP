// matcher_user_routine.cpp — Phase I PI-2 + PI-3 user-defined-routine
// call matcher.
//
// Fires on every `callExpr` whose callee FunctionDecl is registered in the
// PI-1 `RoutineRegistry` (i.e. the user called `STURM_REGISTER_ADJOINT(fn,
// adj)` on it somewhere in the TU). On match, records one
// `QOperation{kind=USER_ROUTINE}` with
//
//   - `routine_name` = callee's source-level identifier,
//   - `operands`     = each argument in source order, with classical
//                      scalars rendered verbatim via Lexer::getSourceText,
//   - `outputs_mask` = bit i set iff the callee's i-th parameter is a
//                      non-const qbool&/qint& reference.
//
// PI-3 adds output-param ownership classification: every OUTPUT slot's
// backing VarDecl is classified via `detail::classify_output` into one
// of {Intermediate, IntermediateOuter, Final, SkipWithDiagnostic}. The
// per-slot classifications aggregate into a single op-level policy:
// any SkipWithDiagnostic or Final → skip the whole uncompute; otherwise
// if any IntermediateOuter slot exists, set
// `QOperation::insert_before_override` to the outermost declaring
// scope's close brace so every output is still in scope when the
// inverse fires. Pure-Intermediate ops leave the override invalid and
// use the default M8 anchor.
//
// PI-4 extends the uncompute pass to emit `invert(<routine_name>)(...)`
// from this IR entry; until then the op is a passive record — it appears
// in `dump()` but no `UncomputeInsertion` is produced. The PI-3 flags
// `skip_uncompute` and `insert_before_override` are still honoured by
// the M8 synthesis pass for every other kind, so once PI-4 lands the
// USER_ROUTINE render case will inherit them for free.
//
// Registration ordering (main.cpp):
// ---------------------------------
// The issue description pins this matcher to register AFTER the Phase H
// PH-2 brace-wrap matcher and AFTER the Phase A / B / C compound-assign
// matchers. MatchFinder callback invocation order follows registration
// order, but PI-2's anchor (`callExpr` on a FunctionDecl present in the
// registry) is structurally disjoint from every pre-PI matcher's anchor
// — which means the ordering is an invariant for diagnostic locality
// rather than a correctness dependency. We document both rationales in
// main.cpp next to the `register_user_routine_matcher` call.

#include "matcher_user_routine.hpp"

#include "sturm/transpile/qir.hpp"
#include "matcher_common.hpp"
#include "routine_registry.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/Type.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/Casting.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;
using detail::classify_output;
using detail::declaring_scope_close_brace;
using detail::emit_outer_mutation_diagnostic;
using detail::enclosing_scope;
using detail::find_or_create_scope;
using detail::make_ref;
using detail::OutputClass;

// Test-only counter. The transpiler is single-threaded under matchAST, so
// a plain int is safe. Incremented once per successfully-appended
// USER_ROUTINE QOperation.
static int g_user_routine_detection_count = 0;

// Classify a parameter's declared type. Returns true iff the parameter
// is a non-const lvalue reference to `qbool` or `qint` (i.e. an OUTPUT
// slot). Const references, value types, and any other shape (pointers,
// rvalue refs, etc.) return false — they are treated as INPUT.
//
// We key the decision on the parameter's `QualType` rather than the
// argument expression's static type so that callers passing an
// out-of-place temporary still classify correctly relative to the
// callee's contract.
bool is_output_param(const ParmVarDecl* param) {
    if (!param) return false;
    const QualType qt = param->getType();
    // Only lvalue references reach non-const qbool/qint by reference.
    // Rvalue references and pointers do not participate in the
    // STURM routine contract, so any non-lvalue-ref shape is an
    // input as far as we are concerned.
    if (!qt->isLValueReferenceType()) return false;
    const QualType pointee = qt->getPointeeType();
    // Const-qualified reference → input (the callee cannot mutate it).
    if (pointee.isConstQualified()) return false;
    // The pointee must be a CXXRecord named `qbool` or `qint` / `qint_t`.
    const CXXRecordDecl* rd = pointee->getAsCXXRecordDecl();
    if (!rd) return false;
    const std::string name = rd->getNameAsString();
    // The public sturm types are `qbool` and `qint` (the latter is the
    // typedef surface for `qint_t<W>`). The matcher accepts either
    // spelling — a non-const lvalue reference to any of them is an
    // OUTPUT slot in the routine contract.
    return name == "qbool" || name == "qint" || name == "qint_t";
}

// Resolve the adjusted callee FunctionDecl for a call expression. This
// walks through any `UsingShadowDecl` / overload resolution sugar via
// `getCanonicalDecl()` so the comparison against the registry keys
// (which are themselves canonical decls — see `routine_registry.cpp`
// and its `getCanonicalDecl()` call) is apples-to-apples.
const FunctionDecl* canonical_callee(const CallExpr* call) {
    if (!call) return nullptr;
    const Decl* callee = call->getCalleeDecl();
    if (!callee) return nullptr;
    const auto* fd = llvm::dyn_cast<FunctionDecl>(callee);
    if (!fd) return nullptr;
    return fd->getCanonicalDecl();
}

// Extract the verbatim source text of a classical scalar argument.
// Returns an empty string if the Lexer cannot produce text (e.g. the
// expression is macro-expanded to a loc with no file representation).
// The caller falls back to an empty-named QValueRef in that case so
// the op still records the argument position.
std::string arg_source_text(const Expr* arg,
                            const SourceManager& sm,
                            const LangOptions& lang) {
    if (!arg) return {};
    auto range = CharSourceRange::getTokenRange(arg->getSourceRange());
    auto text = Lexer::getSourceText(range, sm, lang);
    return text.str();
}

class UserRoutineCallback : public MatchFinder::MatchCallback {
public:
    UserRoutineCallback(QUnit* unit, const RoutineRegistry* registry)
        : unit_(unit), registry_(registry) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* call = r.Nodes.getNodeAs<CallExpr>("call");
        if (!call || !r.Context || !unit_ || !registry_) return;

        // Registry filter — reject calls to functions that are not
        // registered as forward routines. The registry stores canonical
        // FunctionDecl pointers, so we compare against the canonical
        // callee.
        const FunctionDecl* fd = canonical_callee(call);
        if (!fd) return;
        if (!registry_->contains(fd)) return;

        // Enclosing scope — same helper every other matcher uses. Phase
        // H PH-1 support for both braced CompoundStmts and braceless
        // for/while/if bodies is transparent.
        const auto es = enclosing_scope(*call, *r.Context);
        if (!es.valid()) return;

        const SourceManager& sm = r.Context->getSourceManager();
        const LangOptions&   lang = r.Context->getLangOpts();
        QScope& scope = find_or_create_scope(*unit_, es, sm, lang);

        // Build the operand list + outputs_mask by walking parameters
        // and arguments in lockstep. The callee's parameter count is
        // authoritative — we never look past `fd->getNumParams()`, even
        // if the AST happens to expose more arguments (variadic is not
        // supported for STURM routines and the PI-1 registrar won't
        // register variadic callees in the first place).
        const unsigned num_params = fd->getNumParams();
        const unsigned num_args   = call->getNumArgs();
        const unsigned num_slots  = (num_params < num_args) ? num_params
                                                            : num_args;

        QOperation op;
        op.kind         = QOpKind::USER_ROUTINE;
        op.routine_name = fd->getNameAsString();
        // No single named result: USER_ROUTINE mutates through its output
        // parameters (each flagged by outputs_mask). Leaving
        // `op.result` default-constructed — name empty, decl_loc
        // invalid — is the explicit signal downstream consumers use.
        op.stmt_range = call->getSourceRange();
        op.outputs_mask = 0;

        // ── PI-3 per-slot output-ownership classification ───────────────
        // As we fill the op, we also classify each OUTPUT slot via the
        // PI-3 helper and aggregate across the slots. The aggregate
        // drives three downstream effects:
        //
        //   - any `SkipWithDiagnostic` slot → `skip_uncompute=true` and
        //     a stderr diagnostic is emitted (once per Skip slot).
        //   - any `Final` slot (and no Skip slot) → `skip_uncompute=true`
        //     with NO diagnostic (escaping value, user's final compute).
        //   - mixed `Intermediate` + `IntermediateOuter` slots with no
        //     Skip / Final → leave `skip_uncompute=false` and plant
        //     `insert_before_override` at the OUTERMOST declaring scope's
        //     close brace so every output is still in scope when the
        //     inverse fires.
        //
        // The PI-3 classifier consumes the call's enclosing *CompoundStmt*
        // (not the enclosing PH-1 BracelessBody). For a call sitting in
        // a braceless for/while/if body, `call_scope` is null — the
        // helper treats that case as unconditionally inside a control-
        // flow body, which matches the intended PH-3-equivalent semantics.
        const CompoundStmt* call_scope =
            (es.kind == detail::QScopeKind::CompoundStmt) ? es.compound
                                                          : nullptr;

        bool saw_skip_with_diag = false;
        bool saw_final          = false;
        bool saw_outer          = false;
        SourceLocation outermost_close_brace{};
        // The outermost declaring scope has the FEWEST CompoundStmt
        // ancestors (a variable declared at function-body level has
        // depth 1; a variable declared in a nested block has depth 2,
        // etc.). We initialise to UINT_MAX so the first valid candidate
        // wins, and overwrite only on a STRICTLY SMALLER depth — which
        // is the scope whose close brace encloses every other output's
        // declaring scope.
        unsigned outermost_depth = static_cast<unsigned>(-1);

        for (unsigned i = 0; i < num_slots; ++i) {
            const ParmVarDecl* param = fd->getParamDecl(i);
            const Expr* arg = call->getArg(i);
            const bool is_output = is_output_param(param);
            if (is_output) {
                // Guard against arities wider than 64 parameters — see
                // qir.hpp's `outputs_mask` comment. In practice the
                // registrar would reject callees with >64 params
                // upstream, but we guard here for safety.
                if (i < 64u) {
                    op.outputs_mask |= (std::uint64_t{1} << i);
                }
            }

            // Operand capture. Prefer the DeclRefExpr-lifting path for
            // named arguments (both output refs AND input refs to named
            // qbool/qint locals), so the emitter can cite the user's
            // identifier directly. Fall back to Lexer-extracted source
            // text for classical scalars / compound expressions.
            const Expr* payload = detail::peel_to_payload(arg);
            const DeclRefExpr* dre =
                llvm::dyn_cast_or_null<DeclRefExpr>(payload);
            if (dre) {
                op.operands.push_back(make_ref(*dre));
            } else {
                QValueRef ref;
                ref.name = arg_source_text(arg, sm, lang);
                op.operands.push_back(std::move(ref));
            }

            // Classification is only meaningful for OUTPUT slots — input
            // arguments don't drive uncompute placement. Inputs that
            // happen to be file-scope / parameters stay untouched.
            if (!is_output || !dre) continue;
            const auto* vd = llvm::dyn_cast_or_null<VarDecl>(dre->getDecl());
            if (!vd) continue;

            const OutputClass cls =
                classify_output(vd, call, call_scope, *r.Context, sm, lang);
            switch (cls) {
            case OutputClass::SkipWithDiagnostic:
                saw_skip_with_diag = true;
                // Emit the stderr diagnostic per-slot so the user can
                // identify which output argument triggered the skip.
                // Matches PH-3's text verbatim (shared helper).
                emit_outer_mutation_diagnostic(sm, vd, call);
                break;
            case OutputClass::Final:
                saw_final = true;
                break;
            case OutputClass::IntermediateOuter: {
                saw_outer = true;
                const SourceLocation close =
                    declaring_scope_close_brace(vd, *r.Context);
                // Track the OUTERMOST (highest-depth) scope so mixed
                // intermediate / intermediate-outer slots uncompute at
                // a loc that keeps every output in scope. We key by
                // parent-hop count from the VD so we do not compare
                // SourceLocations (which lack a total order across
                // nested scopes in general).
                unsigned depth = 0;
                {
                    DynTypedNode node = DynTypedNode::create(*vd);
                    for (int hops = 0; hops < 512; ++hops) {
                        const auto parents = r.Context->getParents(node);
                        if (parents.empty()) break;
                        node = parents[0];
                        if (node.get<CompoundStmt>()) {
                            ++depth;
                        }
                    }
                }
                if (close.isValid() && depth < outermost_depth) {
                    outermost_depth = depth;
                    outermost_close_brace = close;
                }
                break;
            }
            case OutputClass::Intermediate:
                // Default case — the call scope's close brace is the
                // right anchor and no flag needs adjusting.
                break;
            }
        }

        // Apply the aggregate rule.
        if (saw_skip_with_diag) {
            // Skip dominates every other class — we cannot plant a
            // reverse-loop adjoint automatically. PH-3-style skip.
            op.skip_uncompute = true;
        } else if (saw_final) {
            // Escape dominates Intermediate / IntermediateOuter. No
            // diagnostic — the user chose to return this value from
            // the call, which is a valid pattern.
            op.skip_uncompute = true;
        } else if (saw_outer) {
            // Mixed Intermediate / IntermediateOuter: uncompute at the
            // outermost declaring scope's close brace. A pure-
            // Intermediate op leaves `insert_before_override` invalid
            // so the M8 anchor stays at the call scope's close brace
            // (the default).
            op.insert_before_override = outermost_close_brace;
        }

        scope.ops.push_back(std::move(op));
        ++g_user_routine_detection_count;
    }

private:
    QUnit* unit_;
    const RoutineRegistry* registry_;
};

// Static pool so repeated registrations across tests do not leak
// callback lifetimes (mirrors every other matcher module in the
// directory).
std::vector<std::unique_ptr<UserRoutineCallback>>&
user_routine_callback_pool() {
    static std::vector<std::unique_ptr<UserRoutineCallback>> pool;
    return pool;
}

} // namespace

void register_user_routine_matcher(
    clang::ast_matchers::MatchFinder& finder,
    QUnit& unit,
    const RoutineRegistry& registry) {
    // The AST pattern is intentionally broad — any `callExpr` to a
    // `functionDecl`. The callback then narrows on registry membership.
    // A tighter pattern (hasName against each registered fn) would
    // require regenerating the matcher whenever the registry changes,
    // which does not happen during a single `matchAST` run but adds
    // no observable benefit.
    auto pattern = callExpr(callee(functionDecl())).bind("call");

    auto& pool = user_routine_callback_pool();
    pool.push_back(std::make_unique<UserRoutineCallback>(&unit, &registry));
    finder.addMatcher(pattern, pool.back().get());
}

int user_routine_detection_count_for_test() {
    return g_user_routine_detection_count;
}

void reset_user_routine_detection_count_for_test() {
    g_user_routine_detection_count = 0;
}

} // namespace sturm::transpile
