// qint_alias_subst_emitter.cpp -- sturm-65rs.9 (Beat C2) implementation.
//
// See `qint_alias_subst_emitter.hpp` for the contract; PRD §4.3 for
// the rewrite shape; plan §10 for the rule-3 rationale on
// non-VarDecl anchors.
//
// Per-match dispatch
// ------------------
// Each `QintAliasSubstMatch` carries the AST node (one of five
// pointer slots, exactly one non-null per match — see C1's bind
// contract) plus the captured `TypeLoc` source range. The emitter:
//
//   1. Resolves the width `W`:
//        - `kind == VarDecl` → `infer_width(*vd, ctx)` (rules 1-3).
//        - all other kinds → `ctx.default_width` (= `kDefaultWidth`,
//          PRD §3 non-goal: per-Parm/Field width inference is out of
//          scope; follow-up `sturm-65rs.17` will lift this).
//   2. Renders the replacement text via `render_qint_typename(W)`
//      from `render_qint_typename.hpp` (sturm-65rs.7 / Beat C0). One
//      definition site for every emitter family.
//   3. Calls `Rewriter::ReplaceText(type_range, …)` which leaves
//      everything outside the captured TypeLoc range untouched —
//      variable name, `=`, initializer, trailing `;`, comments — all
//      preserved verbatim.
//
// Defensive posture
// -----------------
// A match with an invalid `type_range` or a null kind-pointer yields
// zero Rewriter mutations for that match — same shape every emitter
// in this directory takes on malformed input.

#include "qint_alias_subst_emitter.hpp"

#include "render_qint_typename.hpp"
#include "width_inference.hpp"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include <string>
#include <vector>

namespace sturm::transpile {

namespace {

using clang::Rewriter;
using clang::SourceRange;

// Resolve the backend width `W` for a match. For VarDecls we route
// through the existing `infer_width(VarDecl, InferContext)` so the
// three-rule pipeline (annotation / RHS-driven / global default) has
// a single source of truth across the QRAM and qint-alias matchers.
// For every other anchor (ParmVarDecl, FieldDecl, FunctionDecl
// return, FunctionalCast) we fall through directly to
// `ctx.default_width` — per PRD §3 non-goal, per-Parm/Field width
// inference is out of scope; follow-up `sturm-65rs.17` will lift this.
//
// The `InferContext` is constructed without a `DiagnosticsEngine`
// (null `diag` slot) so the emitter does not surface
// `qram-width-mismatch` / `qram-width-annotation-reserved`
// diagnostics on the alias-subst path. Those diagnostics are the
// QRAM-subscript matcher's concern (it owns the InferContext with a
// live `DiagnosticsEngine`); on this path the width inference is a
// pure dispatch and silent fallthrough to rule 3 is the correct
// posture.
unsigned width_for_match(const QintAliasSubstMatch& m) {
    InferContext ctx;
    if (m.kind == QintAliasSubstKind::VarDecl && m.vd != nullptr) {
        return infer_width(*m.vd, ctx);
    }
    return ctx.default_width;
}

// Defensive precheck: the kind-discriminated pointer must be non-null
// and the captured `type_range` must be valid. C1 already gates on
// the latter at publish time, but we re-check here so the emitter
// stays robust against future matcher changes.
bool is_match_well_formed(const QintAliasSubstMatch& m) {
    if (m.type_range.isInvalid()) return false;
    switch (m.kind) {
    case QintAliasSubstKind::VarDecl:        return m.vd   != nullptr;
    case QintAliasSubstKind::ParmVarDecl:    return m.pmd  != nullptr;
    case QintAliasSubstKind::FieldDecl:      return m.fd   != nullptr;
    case QintAliasSubstKind::FunctionDecl:   return m.fn   != nullptr;
    case QintAliasSubstKind::FunctionalCast: return m.cast != nullptr;
    }
    return false;
}

} // anonymous namespace

// ── Public surface: pure-string emission ───────────────────────────────────

std::string emit_qint_alias_subst_text(unsigned W) {
    // Single source of truth: the shared `render_qint_typename` helper
    // from `render_qint_typename.hpp` (sturm-65rs.7 / Beat C0). When
    // `W == 0` the legacy bare `qint` typename is emitted; for any
    // `W > 0` the canonical `sturm::qint_t<W>` shape is rendered.
    return render_qint_typename(W);
}

// ── Public surface: AST-driven rewrites ───────────────────────────────────

void emit_qint_alias_subst_rewrites(
    Rewriter& rw,
    const std::vector<QintAliasSubstMatch>& matches) {
    if (matches.empty()) return;

    for (const auto& m : matches) {
        if (!is_match_well_formed(m)) continue;
        const unsigned W = width_for_match(m);
        const std::string text = emit_qint_alias_subst_text(W);
        if (text.empty()) continue;

        // ReplaceText takes a token-end SourceRange — the same shape
        // every other rewrite emitter in this directory consumes. The
        // captured `type_range` spans the type spelling only (e.g.
        // `sturm::frontend::qint` from `sturm::frontend::qint x;` —
        // NOT including the variable name, the `=`, nor the trailing
        // `;`); the rest of the line is preserved verbatim.
        (void)rw.ReplaceText(m.type_range, text);
    }
}

// ── Public surface: replacement-record emission (sturm-ddgo path) ─────────

void emit_qint_alias_subst_replacements(
    const clang::SourceManager& /*sm*/,
    const clang::LangOptions& /*lang*/,
    const std::vector<QintAliasSubstMatch>& matches,
    std::vector<QReplacement>& replacements) {
    if (matches.empty()) return;

    for (const auto& m : matches) {
        if (!is_match_well_formed(m)) continue;
        const unsigned W = width_for_match(m);
        const std::string text = emit_qint_alias_subst_text(W);
        if (text.empty()) continue;

        QReplacement rep;
        rep.range       = m.type_range;
        rep.replacement = text;
        replacements.push_back(std::move(rep));
    }
}

} // namespace sturm::transpile
