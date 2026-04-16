// matcher_output_class.hpp — Phase I PI-3 output-param liveness / ownership
// classifier.
//
// This header is NOT part of the public transpiler API. It lives under
// transpiler/src/ (next to matcher_common.hpp) and is transitively
// included by matcher_common.hpp so every matcher TU that `#include`s
// matcher_common.hpp also sees `detail::classify_output`. Splitting the
// classifier out keeps matcher_common.hpp from growing past its already-
// large surface while keeping the "one include for every matcher helper"
// contract the earlier phases rely on.
//
// Purpose
// -------
// When the Phase I PI-2 matcher (matcher_user_routine.cpp) picks up a
// call to a user-registered routine, every output parameter — each
// non-const qbool&/qint& reference slot — backs a named VarDecl the
// caller supplied. PI-3 classifies each of those VarDecls into one of
// four ownership classes so the uncompute pass can decide where (or
// whether) to plant the routine's adjoint:
//
//   - Intermediate        : VD is declared in the call's enclosing scope.
//                           Uncompute at the call scope's close brace
//                           (the default M8 anchor).
//   - IntermediateOuter   : VD is declared in an ancestor scope of the
//                           call, still inside the function. Uncompute
//                           at the declaring scope's close brace via
//                           `QOperation::insert_before_override`.
//   - Final               : VD is a function parameter OR declared at
//                           file / namespace scope. The value escapes
//                           — no uncompute, no diagnostic.
//   - SkipWithDiagnostic  : VD is declared outside a for / while / if /
//                           else / WHEN body but the call (the mutation)
//                           lives inside one. Mirror PH-3: set
//                           `skip_uncompute=true` and emit a stderr
//                           diagnostic.
//
// The parent-chain walk was originally implemented inside
// matcher_outer_var_guard.cpp (`classify_mutation`) to back PH-3; the
// PI-3 subphase extracts that logic into a shared helper so PI-2 and
// PH-3 can agree on a single source of truth. PH-3 is refactored to
// call this helper (see matcher_outer_var_guard.cpp) and its existing
// snapshot fixtures remain byte-identical — the classification output
// for the PH-3 shapes (compound-assign LHS in for / while / if / WHEN
// bodies) reduces cleanly to `SkipWithDiagnostic`.

#ifndef STURM_TRANSPILE_MATCHER_OUTPUT_CLASS_HPP
#define STURM_TRANSPILE_MATCHER_OUTPUT_CLASS_HPP

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/Stmt.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"

#include <cstdio>
#include <string>

namespace sturm::transpile::detail {

// The four output-param ownership classes. See the file-level comment
// for the semantics of each. `Intermediate` is the default / "uncompute
// normally at scope close" case; every other class adjusts where or
// whether the uncompute lands.
enum class OutputClass {
    Intermediate,
    IntermediateOuter,
    Final,
    SkipWithDiagnostic,
};

// Find the enclosing CompoundStmt of a Decl (walking the parent chain
// through ASTContext::getParents). Returns nullptr for declarations
// that do not nest inside any CompoundStmt — globals, file-scope
// declarations, and function parameters are the canonical null-returning
// cases. The walk is capped at a generous 512 hops to guard against
// pathological parent chains; real source trees are nowhere near that
// deep.
//
// Extracted from `enclosing_compound_of_decl` in
// matcher_outer_var_guard.cpp by PI-3 so PH-3 and the new PI-3
// classifier share identical walk semantics.
inline const clang::CompoundStmt*
enclosing_compound_of_decl(const clang::Decl* decl, clang::ASTContext& ctx) {
    if (!decl) return nullptr;
    clang::DynTypedNode node = clang::DynTypedNode::create(*decl);
    for (int hops = 0; hops < 512; ++hops) {
        const auto parents = ctx.getParents(node);
        if (parents.empty()) return nullptr;
        node = parents[0];
        if (const auto* cs = node.get<clang::CompoundStmt>()) {
            return cs;
        }
    }
    return nullptr;
}

// Classify whether the call at `call` lives inside a for / while / if /
// else / WHEN body that was entered BEFORE the walk reached the
// CompoundStmt `decl_scope`. Returns true if a control-flow barrier
// was encountered first (SkipWithDiagnostic condition), false if
// `decl_scope` was reached first (no barrier — the mutation is safe
// to uncompute at a well-defined anchor).
//
// Semantics mirror PH-3's `classify_mutation`:
//   - Walk up the parent chain of `call`.
//   - Track `prev_stmt` to disambiguate "body of a for/while/if" vs
//     "cond / init" — a mutation in the init-stmt of a for runs once
//     and is NOT a barrier.
//   - Any IfStmt / ForStmt / WhileStmt entered via its body / then /
//     else counts as a barrier. WHEN-expanded IfStmts behave
//     identically (the WHEN macro expands to nested `if`s).
//   - Stop when we reach the declaring CompoundStmt (decl_scope).
//
// A null `decl_scope` means the VarDecl has no enclosing CompoundStmt
// (file scope, parameter, ...). Callers treat that case via the
// OutputClass::Final path before reaching this helper, so the walk
// here is never invoked with a null decl_scope in practice — but if it
// does, we conservatively return true once the walk empties out
// without finding the declaring scope.
inline bool walk_sees_barrier_before_decl_scope(
    const clang::Expr* call,
    const clang::CompoundStmt* decl_scope,
    clang::ASTContext& ctx) {
    if (!call) return false;
    const clang::Stmt* prev_stmt = call;
    clang::DynTypedNode node = clang::DynTypedNode::create(*call);
    for (int hops = 0; hops < 512; ++hops) {
        const auto parents = ctx.getParents(node);
        if (parents.empty()) return false;
        node = parents[0];

        // Reached the declaring scope without seeing a barrier → not
        // a skip condition.
        if (const auto* cs = node.get<clang::CompoundStmt>()) {
            if (decl_scope && cs == decl_scope) {
                return false;
            }
            // Otherwise the CompoundStmt is a nested braced block; keep
            // walking. No-op for prev_stmt bookkeeping — prev_stmt is
            // updated below.
        }

        if (const auto* fs = node.get<clang::ForStmt>()) {
            if (fs->getBody() == prev_stmt) return true;
        } else if (const auto* ws = node.get<clang::WhileStmt>()) {
            if (ws->getBody() == prev_stmt) return true;
        } else if (const auto* is = node.get<clang::IfStmt>()) {
            const bool entered_via_body =
                (is->getThen() == prev_stmt) || (is->getElse() == prev_stmt);
            if (entered_via_body) return true;
        }

        if (const auto* as_stmt = node.get<clang::Stmt>()) {
            prev_stmt = as_stmt;
        }
    }
    return false;
}

// Classify an output VarDecl `vd` relative to a call `call` whose
// enclosing CompoundStmt is `call_scope`. The supplied `sm` / `lang`
// are accepted for signature uniformity with the other matcher
// helpers (some future refinements — e.g. file-scope checks across
// translation units — will need them); today the classifier only
// consults `ctx`.
//
// See the file-level comment for the full rule set. Summary:
//   - Parameter / file-scope VD             → Final
//   - VD's scope == call_scope              → Intermediate
//   - VD's scope is an ancestor of call, no
//     control-flow barrier between them     → IntermediateOuter
//   - VD's scope is an ancestor of call AND
//     call is inside a for/while/if/WHEN
//     body relative to VD                    → SkipWithDiagnostic
inline OutputClass classify_output(
    const clang::VarDecl* vd,
    const clang::Expr* call,
    const clang::CompoundStmt* call_scope,
    clang::ASTContext& ctx,
    const clang::SourceManager& /*sm*/,
    const clang::LangOptions& /*lang*/) {
    if (!vd || !call) {
        // Defensive: no classification possible. Treat as Final so the
        // PI-2 matcher neither skips nor plants a bogus uncompute for an
        // unclassifiable output.
        return OutputClass::Final;
    }

    // Function parameters always escape through the call site's return
    // path. Per P9 the caller of this routine owns the uncompute policy
    // for its own parameters — we must NOT plant any inverse here.
    if (clang::isa<clang::ParmVarDecl>(vd)) {
        return OutputClass::Final;
    }

    // File / namespace-scope VarDecls live outside every function body,
    // so their "declaring scope" is not a CompoundStmt. They escape
    // every call site and must not be auto-uncomputed.
    const clang::CompoundStmt* decl_scope =
        enclosing_compound_of_decl(vd, ctx);
    if (!decl_scope) {
        return OutputClass::Final;
    }

    // Same-scope case: VD is declared in the call's enclosing block.
    // The default M8 anchor (call scope close brace) is the right
    // place for the uncompute.
    if (call_scope && decl_scope == call_scope) {
        return OutputClass::Intermediate;
    }

    // VD is in some other scope. If call_scope is null (the call lives
    // in a PH-1 braceless body whose synthetic QScope is not a
    // CompoundStmt), the call is inside a for / while / if body by
    // definition — so if VD is not the same scope we must treat it as
    // SkipWithDiagnostic unconditionally.
    if (!call_scope) {
        return OutputClass::SkipWithDiagnostic;
    }

    // VD is in an ancestor scope (or a sibling, but for a well-formed
    // VD that reaches the call-site ref its scope must lexically
    // contain the call-site). Walk up the parent chain of the call and
    // see whether we cross a for / while / if barrier before reaching
    // decl_scope.
    const bool barrier =
        walk_sees_barrier_before_decl_scope(call, decl_scope, ctx);
    return barrier ? OutputClass::SkipWithDiagnostic
                   : OutputClass::IntermediateOuter;
}

// Convenience: look up the close-brace SourceLocation of the
// CompoundStmt that declares `vd`. Returns an invalid SourceLocation
// when `vd` has no enclosing CompoundStmt (file-scope / parameter —
// callers will already have classified those as Final).
//
// Used by matcher_user_routine.cpp to fill
// `QOperation::insert_before_override` for `IntermediateOuter`
// outputs so the M8 synthesis pass plants the inverse at the declaring
// scope's close brace, not at the call's enclosing-scope close brace.
inline clang::SourceLocation
declaring_scope_close_brace(const clang::VarDecl* vd,
                            clang::ASTContext& ctx) {
    const clang::CompoundStmt* decl_scope =
        enclosing_compound_of_decl(vd, ctx);
    if (!decl_scope) return {};
    return decl_scope->getRBracLoc();
}

// Emit the shared PH-3 / PI-3 "outer-mutation hazard" diagnostic to
// stderr. Format intentionally mirrors matcher_outer_var_guard.cpp's
// `emit_diagnostic` word-for-word so both PH-3 (compound-assign
// mutation) and PI-3 (routine-call output mutation) speak one voice
// to the user. Callers supply `site` — the AST node whose source loc
// the diagnostic points at (for PH-3: the CXXOperatorCallExpr; for
// PI-3: the CallExpr) — and `vd`, the mutated VarDecl.
//
// Extracted from matcher_outer_var_guard.cpp by PI-3 so the PI-2
// matcher's SkipWithDiagnostic path can call the same helper. PH-3's
// callback now delegates here as well, keeping the diagnostic text a
// single source of truth.
inline void emit_outer_mutation_diagnostic(const clang::SourceManager& sm,
                                           const clang::VarDecl* vd,
                                           const clang::Expr* site) {
    if (!vd || !site) return;
    const clang::SourceLocation site_loc = site->getBeginLoc();
    const clang::SourceLocation decl_loc = vd->getLocation();

    const clang::PresumedLoc site_pl = sm.getPresumedLoc(sm.getFileLoc(site_loc));
    const clang::PresumedLoc decl_pl = sm.getPresumedLoc(sm.getFileLoc(decl_loc));

    const char* file = "<unknown>";
    unsigned line = 0;
    unsigned col = 0;
    if (site_pl.isValid()) {
        file = site_pl.getFilename() ? site_pl.getFilename() : "<unknown>";
        line = site_pl.getLine();
        col  = site_pl.getColumn();
    }
    unsigned decl_line = 0;
    if (decl_pl.isValid()) {
        decl_line = decl_pl.getLine();
    }

    const std::string name = vd->getNameAsString();

    std::fprintf(stderr,
                 "%s:%u:%u: error: STURM: qbool/qint '%s' (declared at "
                 "%u) is modified inside a for/while/if/WHEN body — "
                 "automatic uncomputation would require reverse-loop "
                 "synthesis. Provide a manual adjoint (P9) or "
                 "restructure.\n",
                 file, line, col, name.c_str(), decl_line);
}

} // namespace sturm::transpile::detail

#endif // STURM_TRANSPILE_MATCHER_OUTPUT_CLASS_HPP
