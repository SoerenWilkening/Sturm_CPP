// qram_emitter_assign.hpp -- sturm-u9ge.6 (Beat H1, post-v1) rewrite emitter
// for the existing-target QRAM-read shape.
//
// Plan §11 (post-v1 backlog) / Beat H1; PRD §9 row 1. Companion to
// `matcher_qram_subscript_assign.{hpp,cpp}` (sturm-u9ge.6 / Beat H1
// matcher). The matcher publishes a `QramSubscriptAssignHit` per matched
// `b = a[i];` site; this emitter performs a two-line rewrite per hit:
//
//     b = a[i];
// becomes
//     ::sturm::__QRAM_target_uncompute(b);
//     ::sturm::QRAM_read(a, [n,] i, b);
//
// PRD §9 row 1 wording: "needs uncompute of old `b` before QRAM
// writes". The forward QRAM_read is the standard PRD §11.1.5 line; the
// pre-call uncompute is the new piece this beat introduces. The
// runtime helper `__QRAM_target_uncompute(qint_t<W>&)` is intentionally
// emitted by name only here -- the runtime body lives outside this
// beat's scope (filed-not-implemented, post-v1; the user / runtime
// wiring is responsible for the actual reset-to-|0> semantics). The
// emitter's contract is that the planted text is a syntactically valid
// C++ statement that the user can either provide a body for or replace
// with their own uncompute scheme.
//
// LHS spelling preserved
// ----------------------
// The matcher records `target_expr` (the LHS of the assignment) as an
// AST `Expr*`; the emitter recovers the source text verbatim so
// member-access spellings (`obj.tbl_dst = a[i];` becomes
// `::sturm::__QRAM_target_uncompute(obj.tbl_dst); ::sturm::QRAM_read(...);`)
// continue to compile.
//
// Pointer-arm length recovery (PRD §11.1.6)
// -----------------------------------------
// Identical posture to the v1 D2 emitter: pointer overload's `n` arg
// comes from the matcher's `length_text` field; empty `length_text`
// on a `Pointer` hit surfaces a `qram-pointer-length-missing`
// placeholder comment in place of `n`.
//
// Negative shape: empty hit vector => no-op
// -----------------------------------------
// `emit_qram_assign_rewrites(rw, {})` performs zero Rewriter
// mutations, matching every other rewrite emitter in the project.
//
// LoC budget: <= 300 (plan §1 budget for H1 follow-up).

#ifndef STURM_TRANSPILE_QRAM_EMITTER_ASSIGN_HPP
#define STURM_TRANSPILE_QRAM_EMITTER_ASSIGN_HPP

#include "matcher_qram_subscript_assign.hpp"

#include "sturm/transpile/qir.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace clang {
class LangOptions;
class Rewriter;
class SourceManager;
} // namespace clang

namespace sturm::transpile {

/// One forward-emission text record. Empty `text` => caller skips this
/// hit (degenerate input -- null target expr, missing container/index
/// expr, etc.). The `text` slot carries the two-statement sequence
/// (uncompute of old target + forward QRAM_read into target) the
/// emitter plants in place of the original `b = a[i];` op-call's
/// statement extent.
///
/// `kind` echoes the originating `QramSubscriptAssignHit::kind`.
struct QramAssignEmission {
    std::string text;
    QramContainerKind kind = QramContainerKind::StdArray;
};

/// Pure-string forward emission. Empty `target_text` / `container_text`
/// / `index_text` => empty result (degenerate input, skip). Used
/// directly by unit tests; the AST-driven path delegates here after
/// recovering names from a `QramSubscriptAssignHit`.
///
/// `W` follows the same convention as the D2 emitter
/// (`qram_emitter::emit_qram_forward_text`): when > 0 the emitted
/// text is namespace-qualified `::sturm::QRAM_read` /
/// `::sturm::__QRAM_target_uncompute`; when 0 the (legacy) bare
/// `QRAM_read` / `__QRAM_target_uncompute` names are emitted, used by
/// hermetic-stub fixtures. The `W` value also drives the
/// uncompute-helper template-argument-list rendering.
///
/// `length_text` is consumed only for `Pointer` kind. Empty
/// `length_text` on a `Pointer` hit => placeholder comment per
/// PRD §11.1.6.
QramAssignEmission emit_qram_assign_text(QramContainerKind kind,
                                         std::string_view target_text,
                                         std::string_view container_text,
                                         std::string_view index_text,
                                         std::string_view length_text,
                                         unsigned W);

/// Apply per-hit forward rewrites against `rw`. For each non-degenerate
/// hit:
///
///   1. Replace the original `b = a[i]` op-call's source range with a
///      two-statement sequence:
///
///         ::sturm::__QRAM_target_uncompute(b);
///         ::sturm::QRAM_read(a, [n,] i, b);
///
///      The replacement covers exactly the assignment expression
///      (NOT the trailing `;`); the caller's existing `;` remains in
///      place after the rewrite, so the final buffer reads
///      `<uncompute_call>;<forward_call>;`. Trailing comments and
///      column alignment are preserved by the Rewriter's source-range
///      replacement.
///
/// `rw` must outlive the call. The function is a no-op when `hits`
/// is empty.
void emit_qram_assign_rewrites(
    clang::Rewriter& rw,
    const std::vector<QramSubscriptAssignHit>& hits);

/// sturm-ddgo: same per-hit logic as `emit_qram_assign_rewrites` but
/// appends `QReplacement` records to `replacements` instead of
/// mutating a Rewriter. H1 has no adjoint plant, so no
/// `UncomputeInsertion` is produced.
void emit_qram_assign_replacements(
    const clang::SourceManager& sm,
    const clang::LangOptions& lang,
    const std::vector<QramSubscriptAssignHit>& hits,
    std::vector<QReplacement>& replacements);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_QRAM_EMITTER_ASSIGN_HPP
