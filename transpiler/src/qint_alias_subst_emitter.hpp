// qint_alias_subst_emitter.hpp -- sturm-65rs.9 (Beat C2) rewrite emitter.
//
// Plan §10, PRD §4.3. Companion to `matcher_qint_alias_subst.{hpp,cpp}`
// (sturm-65rs.8 / Beat C1). The matcher publishes a typed
// `QintAliasSubstMatch` per matched site (VarDecl, ParmVarDecl,
// FieldDecl, function return type, CXXFunctionalCastExpr); this
// emitter consumes the vector and, per match, replaces the captured
// `TypeLoc` source range with `sturm::qint_t<W>`.
//
// Width selection (PRD §4.3, plan §10):
//
//   - VarDecl  → routes through the existing
//                `infer_width(VarDecl, InferContext)` (sturm-u9ge.11 /
//                Beat B1). Rule 1 (annotation) and rule 2 (RHS-driven
//                subscript inference) fall through on bare integer-literal
//                initializers, leaving rule 3 (`kDefaultWidth = 32`) as
//                the universal terminator.
//   - ParmVarDecl, FieldDecl, FunctionDecl-return, FunctionalCast →
//                width = `kDefaultWidth = 32` directly. Per-Parm/Field
//                width inference is OUT OF SCOPE per PRD §3 non-goal /
//                follow-up `sturm-65rs.17`.
//
// The rendered text is produced by the shared
// `render_qint_typename(W)` helper from `render_qint_typename.hpp`
// (sturm-65rs.7 / Beat C0) — one definition site for every emitter
// family.
//
// Negative shape: empty match vector ⇒ no-op
// ------------------------------------------
// `emit_qint_alias_subst_rewrites(rw, {})` performs zero Rewriter
// mutations, matching every other rewrite emitter in the project.
//
// Defensive posture
// -----------------
// A match with an invalid `type_range` or a null pointer in the field
// matching its `kind` yields zero Rewriter mutations for that match —
// same defensive posture every other emitter takes on malformed input.
//
// LoC budget: <= 300 (plan §1, §10 / C2).

#ifndef STURM_TRANSPILE_QINT_ALIAS_SUBST_EMITTER_HPP
#define STURM_TRANSPILE_QINT_ALIAS_SUBST_EMITTER_HPP

#include "matcher_qint_alias_subst.hpp"

#include "sturm/transpile/qir.hpp"

#include <string>
#include <unordered_set>
#include <vector>

namespace clang {
class LangOptions;
class Rewriter;
class SourceManager;
class VarDecl;
} // namespace clang

namespace sturm::transpile {

/// sturm-65rs.10 (Beat C3) overlap guard. The C3 consumer drains the
/// QRAM-subscript C1 emitter FIRST and records every VarDecl* it
/// rewrote into this struct's `qram_var_decls` set; the alias-subst
/// emitter then consults the set on its VarDecl arm and skips any
/// match whose `m.vd` is already claimed. Pins PRD R2's
/// single-VarDecl-single-rewrite invariant against the
/// `qint b = a[i];` overlap shape (both the QRAM matcher and the
/// alias-subst VarDecl matcher fire on that VarDecl; without this
/// guard the type-spelling of `b` would be rewritten twice).
///
/// Owned and populated by `transpile_consumer.cpp`'s drain block; the
/// emitter only reads it. Empty default ⇒ no overlap, every VarDecl
/// match is honoured (= the unit-test path used by C2's existing
/// `test_qint_alias_subst_emitter` exercises the no-claim arm).
struct QintAliasSubstClaimedDecls {
    std::unordered_set<const clang::VarDecl*> qram_var_decls;
};

/// Pure-string forward emission. Produces the rewritten type spelling
/// `sturm::qint_t<W>` for any `W > 0`. When `W == 0` the legacy bare
/// `qint` typename is emitted (used by hermetic-stub fixtures —
/// matches the `render_qint_typename(0)` arm).
///
/// Used directly by unit tests so the rendered shape is locked in
/// independent of the AST plumbing.
std::string emit_qint_alias_subst_text(unsigned W);

/// Apply per-match rewrites against `rw`. For each well-formed match:
///
///   1. Compute `W` per the table in the file-level header comment
///      (VarDecl: `infer_width`; everything else: `kDefaultWidth`).
///   2. Replace the captured `type_range` with the rendered text from
///      `emit_qint_alias_subst_text(W)`.
///
/// `rw` must outlive the call. The function is a no-op when `matches`
/// is empty. Defensive: a match whose `type_range` is invalid or whose
/// kind-discriminated pointer is null is silently skipped.
///
/// The `claimed` overload skips any VarDecl match whose `m.vd` is in
/// `claimed.qram_var_decls`. Pins PRD R2 / sturm-65rs.10: the C3
/// consumer drains the QRAM-subscript C1 emitter first, records every
/// rewritten VarDecl* in `claimed`, and forwards it here so the
/// alias-subst emitter does not double-rewrite a type spelling the
/// QRAM emitter already covered (the QRAM rewrite is line-level —
/// type+name+initializer — so a second rewrite of just the type is
/// both redundant and corrupting). Default-constructed `claimed` ⇒
/// no overlap (unit-test path).
void emit_qint_alias_subst_rewrites(
    clang::Rewriter& rw,
    const std::vector<QintAliasSubstMatch>& matches);

void emit_qint_alias_subst_rewrites(
    clang::Rewriter& rw,
    const std::vector<QintAliasSubstMatch>& matches,
    const QintAliasSubstClaimedDecls& claimed);

/// sturm-ddgo: produce `QReplacement` records without touching a
/// Rewriter. Mirrors `emit_qint_alias_subst_rewrites`'s per-match
/// logic but flows through the consumer's `QUnit::replacements`
/// channel (the C3 consumer wiring will call this entrypoint). The
/// emitter does not plant any adjoint, so no `UncomputeInsertion`
/// records are produced.
///
/// `sm` and `lang` are accepted for parity with the other
/// `_replacements` entrypoints in this directory; the C2 emitter does
/// not currently consult them, but the signature is stable so the C3
/// consumer can drain emitters uniformly.
void emit_qint_alias_subst_replacements(
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const std::vector<QintAliasSubstMatch>& matches,
    std::vector<QReplacement>& replacements);

/// sturm-65rs.10 (Beat C3): same per-match logic as the no-`claimed`
/// overload, but skips any VarDecl match whose `m.vd` is in
/// `claimed.qram_var_decls`. Used by `transpile_consumer.cpp` after
/// `emit_qram_replacements` populates the claimed-decls set.
void emit_qint_alias_subst_replacements(
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const std::vector<QintAliasSubstMatch>& matches,
    const QintAliasSubstClaimedDecls& claimed,
    std::vector<QReplacement>& replacements);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_QINT_ALIAS_SUBST_EMITTER_HPP
