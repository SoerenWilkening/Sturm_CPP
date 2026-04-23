// matcher_outer_var_guard.cpp — Phase H PH-3 outer-scoped mutation guard.
//
// Detects compound-assign mutations (`a ^= b`, `a += C`, `a += b` for qint,
// ...) whose target variable is declared in an outer scope relative to the
// mutation site AND where the mutation lives inside a `for` / `while` /
// `if` (then or else) / `WHEN` body. Such mutations would need reverse-loop
// synthesis to uncompute correctly — a large architectural project that
// contradicts P9 ("routines are invertible by explicit adjoint"). Phase H
// refuses to auto-generate an inverse for them; instead this matcher
// flags the corresponding `QOperation` with `skip_uncompute=true` (so the
// M8 synthesis pass emits nothing for that op) AND prints a stderr
// diagnostic pointing the user at the precise line + column so they can
// either supply a manual adjoint or restructure.
//
// Phase S S-B opt-in
// ------------------
// When the enclosing `FunctionDecl` carries `[[sturm::reversible]]`
// (detected via `is_reversible()`) AND the outer mutation's scoping
// barrier is a `ForStmt`, the matcher sets `needs_loop_reversal=true`
// on the op and skips the diagnostic — the op is handed to Phase S's
// `loop_reversal` synthesis path instead. For every other shape
// (non-reversible FD, or while/if/WHEN/else barrier even inside a
// reversible routine), the PH-3 default path above runs unchanged.
//
// Shapes covered (the operator-call kinds that mutate a named LHS):
//
//   - `a ^= b;` / `a ^= <classical>;` (QOpKind::XOR_ASSIGN)
//   - `a += C;` / `a -= C;` / `a *= C;` / `a /= C;` on qint_t
//     (QOpKind::{ADD,SUB,MUL,DIV}_ASSIGN_CONST)
//   - `a += b;` / `a -= b;` / `a *= b;` / `a /= b;` / `a %= b;` on qint_t
//     (QOpKind::{ADD,SUB,MUL,DIV,MOD}_ASSIGN_QINT)
//
// Detection algorithm
// -------------------
// The matcher anchors on `cxxOperatorCallExpr` for each of the mutating
// operator-name sets. On match, the callback:
//
//   1. Resolves the LHS's declaration (`VarDecl::getLocation()`) so it
//      knows where the mutated variable was introduced.
//   2. Walks the parent chain of the call expression (via
//      ASTContext::getParents). If it encounters a `ForStmt` / `WhileStmt`
//      / `IfStmt` whose body/then/else contains the call BEFORE it
//      reaches the CompoundStmt that contains the VarDecl, the mutation
//      is classified as "outer" and the guard fires.
//   3. WHEN detection: the WHEN macro expands to three nested `if`s, so
//      the raw parent walk naturally encounters those `IfStmt`s. We
//      treat any `IfStmt` whose begin loc is a macro expansion of WHEN
//      (via `detail::is_expansion_of_macro`) as a scoping barrier,
//      exactly as we do for a user-written `if`.
//   4. If all guards fire: find the QOperation in `unit.scopes` that the
//      Phase A–C matcher pushed for this call (keyed on
//      `stmt_range.getBegin()`), set `skip_uncompute=true` on it, and
//      emit the stderr diagnostic.
//
// Registration order matters
// --------------------------
// This matcher must run AFTER the Phase A / Phase B / Phase C compound-
// assign matchers so their QOperations are already in `unit.scopes` when
// this callback fires. MatchFinder invokes all callbacks whose pattern
// matched on the same AST node, and the order of invocation follows the
// order of registration — so `main.cpp` registers this matcher LAST among
// the compound-assign family.
//
// Disjointness / ordering with Phase A–C
// --------------------------------------
// The Phase A–C callbacks push their QOperation when they match. This
// callback matches the same CXXOperatorCallExpr nodes (same AST shape,
// same operator names) and runs AFTER, so the op it wants to flag is
// guaranteed to already be in `unit.scopes` by the time this callback
// runs. Looking the op up by `stmt_range` is the simplest, least
// invasive way to locate it without changing the Phase A–C matchers.

#include "sturm/transpile/matcher.hpp"
#include "sturm/transpile/qir.hpp"
#include "diag_context.hpp"
#include "matcher_common.hpp"
#include "reversible_attribute.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"

#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sturm::transpile {

namespace {

using namespace clang;
using namespace clang::ast_matchers;

// Test-only counter. Incremented once per successfully flagged (i.e.
// per QOperation whose `skip_uncompute` we just set to true). The
// transpiler is single-threaded so a plain int is fine.
static int g_outer_var_guard_detection_count = 0;

// Phase S S-B (sturm-ha2k.3) test-only counter. Incremented once per
// QOperation whose `needs_loop_reversal` we just set to true (i.e. per
// for-loop outer mutation that was handed off to Phase S synthesis
// because the enclosing FD carries `[[sturm::reversible]]`). Disjoint
// from `g_outer_var_guard_detection_count` so the S-B test can assert
// that a reversible routine's in-for mutation is routed to synthesis
// AND that PH-3's legacy counter stays at zero.
static int g_loop_reversal_handoff_count = 0;

// Return the innermost user-declared VarDecl the `lhs` DeclRefExpr
// references. Returns nullptr if the bound node does not point at a
// VarDecl (e.g. a synthetic / template-dependent reference) — the
// callback treats that as "unclassifiable" and bails.
static const VarDecl* var_decl_of(const DeclRefExpr* lhs) {
    if (!lhs) return nullptr;
    return dyn_cast_or_null<VarDecl>(lhs->getDecl());
}

// Classification result for the parent walk.
enum class MutationKind {
    // The mutation sits inside a `for` body, BEFORE the walk reached
    // the CompoundStmt that contains the LHS's declaration. This
    // subclass exists so Phase S S-B can route the op to the loop-
    // reversal synthesis path when the enclosing FD is reversible.
    OuterMutationInForLoop,
    // The mutation sits inside a `while` / `if` / `WHEN` body, BEFORE
    // the walk reached the CompoundStmt that contains the LHS's
    // declaration. Phase S's S-A loop-reversal module does NOT cover
    // these shapes; the existing PH-3 behaviour (skip_uncompute +
    // diagnostic) applies unchanged, even inside a reversible
    // routine — the P-C validation pass owns the separate "reject
    // non-for control-flow in a reversible routine" diagnostic.
    OuterMutationOther,
    // The walk reached the CompoundStmt that contains the LHS's
    // declaration (or the top of the translation unit) without passing
    // through any control-flow barrier — the variable is mutated within
    // its own declaring scope, which is fine for auto-uncompute.
    LocalMutation,
};

// Convenience predicate: "did the classifier find an outer mutation?"
// — either subclass counts. Keeps the top-level callback body readable.
inline bool is_outer_mutation(MutationKind k) {
    return k == MutationKind::OuterMutationInForLoop ||
           k == MutationKind::OuterMutationOther;
}

// Walk up the parent chain of `call` and decide whether the mutation
// is OuterMutation or LocalMutation. The `decl_stmt_scope` parameter
// is the CompoundStmt that lexically contains the LHS's declaration
// (may be null if the variable is a function parameter, in which case
// the declaring scope is the function body CompoundStmt — we look up
// through ASTContext::getParents too). Either way: as soon as we see
// a ForStmt / WhileStmt / IfStmt (or a WHEN-expanded IfStmt) that we
// entered via its body / then / else (NOT its cond/init), the mutation
// is OUTER — return immediately. If instead we reach the containing
// CompoundStmt first (or the top of the TU), it's LOCAL.
static MutationKind classify_mutation(
    const CXXOperatorCallExpr* call,
    const CompoundStmt* decl_scope,
    ASTContext& ctx,
    const SourceManager& sm,
    const LangOptions& lang) {
    // `prev_stmt` tracks the most recent Stmt we walked through so that
    // when we reach a ForStmt / WhileStmt / IfStmt we can tell whether
    // our original node lives inside the control-flow BODY (in which
    // case the stmt is a scoping barrier) or merely inside the cond /
    // init (which does not re-execute across iterations and thus is not
    // a scoping barrier for this analysis).
    const Stmt* prev_stmt = call;
    DynTypedNode node = DynTypedNode::create(*call);
    // Cap the walk at a generous bound to defend against pathological
    // shapes; real source trees are nowhere near this deep.
    for (int hops = 0; hops < 512; ++hops) {
        const auto parents = ctx.getParents(node);
        if (parents.empty()) return MutationKind::LocalMutation;
        node = parents[0];

        // CompoundStmt branch: if this is the scope that contains the
        // declaration, we reached it WITHOUT passing through a barrier —
        // the mutation is local.
        if (const auto* cs = node.get<CompoundStmt>()) {
            if (decl_scope && cs == decl_scope) {
                return MutationKind::LocalMutation;
            }
            // Any other CompoundStmt is just a nested braced scope; keep
            // walking. (The Phase A–C matchers already parked the op on
            // the correct QScope via enclosing_scope; our job here is
            // purely the outer-vs-local classification.)
        }

        // ForStmt / WhileStmt: mutation is outer iff we entered via the
        // body (i.e. `prev_stmt` is the body Stmt). A mutation sitting
        // in the `init` or `cond` position runs once, not per-iteration,
        // so it does not trigger the reverse-loop-synthesis issue.
        //
        // Phase S S-B refinement: ForStmt is the one barrier shape that
        // Phase S S-A's `loop_reversal` module can reverse; every other
        // outer-mutation barrier (while/if/WHEN) stays on the legacy
        // PH-3 path. We therefore distinguish the two subclasses here so
        // the callback can choose between `needs_loop_reversal = true`
        // (reversible routine + for-loop barrier) and `skip_uncompute
        // = true` (every other outer-mutation shape).
        if (const auto* fs = node.get<ForStmt>()) {
            if (fs->getBody() == prev_stmt) {
                return MutationKind::OuterMutationInForLoop;
            }
        } else if (const auto* ws = node.get<WhileStmt>()) {
            if (ws->getBody() == prev_stmt) {
                return MutationKind::OuterMutationOther;
            }
        } else if (const auto* is = node.get<IfStmt>()) {
            // User-written if AND WHEN-expanded if both qualify. We
            // detect WHEN via the macro-spelling helper used across
            // every WHEN-aware matcher.
            const bool entered_via_body =
                (is->getThen() == prev_stmt) || (is->getElse() == prev_stmt);
            if (entered_via_body) {
                // Always treat as a scoping barrier regardless of
                // whether this is a user if or a WHEN-expanded if.
                // Both cases require a manual adjoint.
                (void)detail::is_expansion_of_macro(is->getIfLoc(), sm, lang,
                                                    "WHEN");
                return MutationKind::OuterMutationOther;
            }
        }

        // Update prev_stmt for the next iteration. We only track Stmt*
        // in prev_stmt — Decl parents do not qualify as "bodies" for
        // any control-flow stmt, so leaving prev_stmt alone on Decl
        // ancestors keeps the walk honest.
        if (const auto* as_stmt = node.get<Stmt>()) {
            prev_stmt = as_stmt;
        }
    }
    // Safety net — if the walk runs away we default to "local" so the
    // guard only fires with real evidence of outer mutation.
    return MutationKind::LocalMutation;
}

// Find the nearest enclosing CompoundStmt of a VarDecl (the CompoundStmt
// that lexically contains the declaration's source location). A walk up
// the parent chain: every local VarDecl has a containing CompoundStmt.
// Returns nullptr for declarations that don't nest inside any
// CompoundStmt (globals / file-scope / function parameters) — the
// caller treats a null decl_scope as "variable lives outside any
// user-written block", which means every for/while/if-body mutation
// of it is automatically outer.
static const CompoundStmt* enclosing_compound_of_decl(const VarDecl* vd,
                                                     ASTContext& ctx) {
    if (!vd) return nullptr;
    DynTypedNode node = DynTypedNode::create(*vd);
    for (int hops = 0; hops < 512; ++hops) {
        const auto parents = ctx.getParents(node);
        if (parents.empty()) return nullptr;
        node = parents[0];
        if (const auto* cs = node.get<CompoundStmt>()) {
            return cs;
        }
    }
    return nullptr;
}

// Phase S S-B helper: find the innermost enclosing `FunctionDecl` for a
// statement / expression node. Walks the parent chain via ASTContext::
// getParents until it lands on a FunctionDecl. Returns nullptr if the
// walk exits the TU without encountering one (e.g. top-level decls the
// matcher should not have matched in the first place — defensive).
//
// Exists as a standalone helper so the Phase S S-B opt-in check in the
// callback body is a single line: `is_reversible(enclosing_function(call, ctx))`.
static const FunctionDecl* enclosing_function(const Stmt* stmt,
                                              ASTContext& ctx) {
    if (!stmt) return nullptr;
    DynTypedNode node = DynTypedNode::create(*stmt);
    for (int hops = 0; hops < 512; ++hops) {
        const auto parents = ctx.getParents(node);
        if (parents.empty()) return nullptr;
        node = parents[0];
        if (const auto* fd = node.get<FunctionDecl>()) {
            return fd;
        }
    }
    return nullptr;
}

// Find the QOperation in `unit.scopes` that corresponds to the matched
// call expression. The Phase A–C matchers key their QOperation on
// `stmt_range = call->getSourceRange()`, so we locate the op by
// comparing the begin loc of the stmt_range against the call's begin
// loc. Returns nullptr if no matching op exists (the Phase A–C matcher
// didn't fire for this call, e.g. an unsupported shape — nothing to
// flag in that case).
static QOperation* find_op_for_call(QUnit& unit, const CXXOperatorCallExpr* call) {
    if (!call) return nullptr;
    const auto call_begin = call->getBeginLoc().getRawEncoding();
    for (auto& scope : unit.scopes) {
        for (auto& op : scope.ops) {
            if (op.stmt_range.getBegin().getRawEncoding() == call_begin) {
                return &op;
            }
        }
    }
    return nullptr;
}

// Emit the PH-3 diagnostic via the shared DiagContext. The format
// string is locked down in `DiagContext::report_outer_var_mutation`:
//   STURM: %0 '%1' (declared at line %2) is modified inside a
//   for/while/if/WHEN body — automatic uncomputation would require
//   reverse-loop synthesis. Provide a manual adjoint (P9) or
//   restructure.
//
// Pre-PM3-2 this function wrote a `<file>:<line>:<col>: error: ...`
// line directly to stderr via fprintf; PM3-2 routes the same
// information through `clang::DiagnosticsEngine` so the output is
// formatted by the parent CompilerInstance's `TextDiagnosticPrinter`
// and the severity is a proper `Warning` (not a hand-rolled `error:`
// prefix) — see the issue description for the end-to-end rationale.
static void emit_diagnostic(const SourceManager& sm,
                            const VarDecl* vd,
                            const CXXOperatorCallExpr* call,
                            DiagContext& diag) {
    if (!vd || !call) return;
    const SourceLocation call_loc = call->getBeginLoc();
    const SourceLocation decl_loc = vd->getLocation();

    // Resolve the file location (the spelling loc, in case we are in
    // a macro expansion — the user cares about where they wrote the
    // code, not where the macro expanded). The PM2-8 source_map
    // diagnostic test asserts `<memory-buffer>` never appears in
    // stderr; funnelling through `getFileLoc` is what guarantees the
    // reported loc resolves to the user's filename rather than the
    // plugin's nested MemoryBuffer pseudo-path.
    const SourceLocation file_call_loc = sm.getFileLoc(call_loc);
    const PresumedLoc decl_pl = sm.getPresumedLoc(sm.getFileLoc(decl_loc));

    unsigned decl_line = 0;
    if (decl_pl.isValid()) {
        decl_line = decl_pl.getLine();
    }

    const std::string name = vd->getNameAsString();

    // `kind` is a human-readable label embedded in the `%0` slot of
    // the format. The matcher does not currently discriminate qbool
    // vs qint_t at the diagnostic site (the Phase A/B/C upstream
    // matchers already enforced the element-type guards by the time
    // we are flagging the op), so we hand over the joint label
    // verbatim — matches the pre-PM3-2 fprintf output.
    constexpr std::string_view kKind = "qbool/qint";

    diag.report_outer_var_mutation(
        file_call_loc, kKind, std::string_view(name), decl_line);
}

// Shared callback body for every mutation operator-kind. The bound
// nodes are always (call, lhs) — the compound-assign call expression
// and its LHS DeclRefExpr.
class OuterVarGuardCallback : public MatchFinder::MatchCallback {
public:
    OuterVarGuardCallback(QUnit* unit, DiagContext* diag)
        : unit_(unit), diag_(diag) {}

    void run(const MatchFinder::MatchResult& r) override {
        const auto* call = r.Nodes.getNodeAs<CXXOperatorCallExpr>("call");
        const auto* lhs  = r.Nodes.getNodeAs<DeclRefExpr>("lhs");
        if (!call || !lhs || !r.Context || !diag_) return;

        const VarDecl* vd = var_decl_of(lhs);
        if (!vd) return;

        ASTContext& ctx = *r.Context;
        const SourceManager& sm = ctx.getSourceManager();
        const LangOptions& lang = ctx.getLangOpts();

        const CompoundStmt* decl_scope = enclosing_compound_of_decl(vd, ctx);

        const MutationKind kind =
            classify_mutation(call, decl_scope, ctx, sm, lang);
        if (!is_outer_mutation(kind)) return;

        // Outer mutation: only act if the Phase A/B/C matcher already
        // pushed a corresponding QOperation onto `unit.scopes`. That
        // filter (keyed on `stmt_range.getBegin()`) doubles as a type
        // guard: the Phase A/B/C matchers enforce the qbool/qint LHS
        // type checks, so any call that does not carry one of the
        // recognised kinds (e.g. an integer `^=` from a template in
        // the standard library) never produces an op to flag — and
        // therefore must not emit a spurious diagnostic either.
        //
        // If the call's shape matches our syntactic pattern but no
        // upstream op was produced for it, we silently bail. The
        // user-visible contract (per the PH-3 issue description) is
        // that the diagnostic refers to a `qbool/qint` named
        // identifier — without a Phase A/B/C op we have no proof the
        // LHS is of a quantum type.
        QOperation* op = find_op_for_call(*unit_, call);
        if (!op) return;

        // Phase S S-B (sturm-ha2k.3): for-loop outer mutations inside
        // a `[[sturm::reversible]]` routine are handed to Phase S's
        // `loop_reversal` module instead of being elided + diagnosed.
        // Both guards must hold — non-for barriers stay on the PH-3
        // path so the P-C validation pass retains sole ownership of
        // the "reject non-for control-flow in a reversible routine"
        // diagnostic family.
        if (kind == MutationKind::OuterMutationInForLoop &&
            is_reversible(enclosing_function(call, ctx))) {
            op->needs_loop_reversal = true;
            ++g_loop_reversal_handoff_count;
            return;
        }

        // Legacy PH-3 path — preserved bit-for-bit for every other
        // shape. Outside a reversible routine, or inside one with a
        // non-for barrier, the original skip_uncompute + diagnostic
        // behaviour runs unchanged.
        emit_diagnostic(sm, vd, call, *diag_);
        op->skip_uncompute = true;
        ++g_outer_var_guard_detection_count;
    }

private:
    QUnit* unit_;
    DiagContext* diag_;
};

std::vector<std::unique_ptr<OuterVarGuardCallback>>&
outer_var_guard_callback_pool() {
    static std::vector<std::unique_ptr<OuterVarGuardCallback>> pool;
    return pool;
}

// Build the matcher pattern for a mutation operator-name. The shape is a
// CXXOperatorCallExpr with exactly two arguments and a bound LHS
// DeclRefExpr. Unlike the Phase A–C matchers we deliberately do NOT
// constrain the LHS type here — PH-3's concern is the scoping pattern,
// not the element type. The Phase A–C matchers already enforce the
// element-type guards; if no Phase A/B/C op exists for this call, the
// `find_op_for_call` lookup returns null and we still emit the
// diagnostic (the user's source still has the problem).
template <typename OperatorName>
auto make_mutation_pattern(OperatorName op_name) {
    return cxxOperatorCallExpr(
        hasOverloadedOperatorName(op_name),
        argumentCountIs(2),
        hasArgument(0, ignoringImplicit(declRefExpr().bind("lhs")))
    ).bind("call");
}

template <typename OperatorName>
void add_pattern(MatchFinder& finder, OuterVarGuardCallback* cb,
                 OperatorName op_name) {
    finder.addMatcher(make_mutation_pattern(op_name), cb);
}

} // namespace

void register_outer_var_guard_matcher(
    clang::ast_matchers::MatchFinder& finder, QUnit& unit,
    DiagContext& diag) {
    // One callback instance handles every mutation operator kind. The
    // pattern per operator is registered separately because MatchFinder
    // does not natively take an "anyOf over overloaded operator names"
    // matcher at the call-expr level.
    auto& pool = outer_var_guard_callback_pool();
    pool.push_back(std::make_unique<OuterVarGuardCallback>(&unit, &diag));
    OuterVarGuardCallback* cb = pool.back().get();

    // Phase A / PA-3 + PA-4.
    add_pattern(finder, cb, "^=");
    // Phase B / PB-1..PB-4 AND Phase C / PC-1..PC-5 — the operator
    // names coincide, so one pattern per name is enough (the LHS
    // type guard the Phase B/C matchers use is irrelevant to this
    // classifier).
    add_pattern(finder, cb, "+=");
    add_pattern(finder, cb, "-=");
    add_pattern(finder, cb, "*=");
    add_pattern(finder, cb, "/=");
    add_pattern(finder, cb, "%=");
}

int outer_var_guard_detection_count_for_test() {
    return g_outer_var_guard_detection_count;
}

void reset_outer_var_guard_detection_count_for_test() {
    g_outer_var_guard_detection_count = 0;
}

// Phase S S-B (sturm-ha2k.3): test-only read / reset for the
// loop-reversal handoff counter. The tests that pin the S-B contract
// assert BOTH that a reversible routine's in-for mutation bumps this
// counter AND that it leaves the PH-3 legacy counter at zero, so the
// two surfaces must be independently inspectable.
int loop_reversal_handoff_count_for_test() {
    return g_loop_reversal_handoff_count;
}

void reset_loop_reversal_handoff_count_for_test() {
    g_loop_reversal_handoff_count = 0;
}

} // namespace sturm::transpile
