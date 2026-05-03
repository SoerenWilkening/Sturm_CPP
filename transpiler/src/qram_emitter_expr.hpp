// qram_emitter_expr.hpp — sturm-u9ge.9 (Beat H4, post-v1) rewrite emitter
// for the expression-position QRAM-read shape.
//
// Plan §11 (post-v1 backlog) / Beat H4; PRD §9 row 4. Companion to
// `matcher_qram_subscript_expr.{hpp,cpp}` (sturm-u9ge.9 / Beat H4
// matcher). The matcher publishes a `QramSubscriptExprHit` per matched
// subscript site embedded in a larger initializer expression; this
// emitter performs three coordinated rewrites per hit:
//
//   1. **Pre-call extract.** Insert a fresh ancilla decl + QRAM_read call
//      *before* the user's `qint c = ...;` line:
//
//         sturm::qint_t<W> __qram_h4_<N>;
//         ::sturm::QRAM_read(a, [n,] i, __qram_h4_<N>);
//
//   2. **In-place subscript replacement.** Replace the `a[i]` subscript
//      expression inside the user's initializer with the ancilla name
//      `__qram_h4_<N>`. This is a Rewriter source-range replacement on
//      the subscript node alone — the rest of the initializer
//      expression (the `+ d`, the parens, the cast, etc.) is preserved
//      verbatim.
//
//   3. **Post-call uncompute.** Insert the matching adjoint *after*
//      the user's `;`:
//
//         ::sturm::__QRAM_read_adj(a, [n,] i, __qram_h4_<N>);
//
//      The adjoint placement is line-local (immediately after the
//      user's line) rather than scope-exit. The ancilla is fresh
//      and not referenced after the user's expression completes, so
//      uncomputing right away is correct AND cheaper than dragging
//      the ancilla to the function epilogue. This matches the
//      "ancilla extraction + uncompute" wording in PRD §9 row 4.
//
// LHS type rewrite
// ----------------
// The user's `qint c` LHS is also rewritten to `sturm::qint_t<W> c` so
// the whole sequence type-checks: the initializer's subscript becomes a
// `qint_t<W>` ancilla, the `+ d` produces a `qint_t<W>` (assuming `d`
// converts), and the resulting `qint_t<W>` initialises `c`. When
// multiple subscripts share a single VarDecl (a multi-subscript
// initializer like `qint c = a[i] + a[j];` after each `a[*]` becomes
// an ancilla), the LHS rewrite happens exactly once per VarDecl —
// the emitter dedupes by `target_var`.
//
// Pointer-arm length recovery (PRD §11.1.6)
// -----------------------------------------
// Identical posture to the v1 D2 emitter
// (`qram_emitter::emit_qram_forward_text`): the pointer overload's
// `n` argument comes from the matcher's `length_text` field; empty
// `length_text` on a pointer hit surfaces a
// `qram-pointer-length-missing` placeholder comment in place of `n`.
//
// Negative shape: empty hit vector ⇒ no-op
// ---------------------------------------
// `emit_qram_expr_rewrites(rw, {})` performs zero Rewriter mutations,
// matching every other rewrite emitter in the project.
//
// LoC budget: <= 300 (plan §1 budget for H4 follow-up).

#ifndef STURM_TRANSPILE_QRAM_EMITTER_EXPR_HPP
#define STURM_TRANSPILE_QRAM_EMITTER_EXPR_HPP

#include "matcher_qram_subscript_expr.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace clang {
class Rewriter;
} // namespace clang

namespace sturm::transpile {

/// One forward-emission text record. Empty `extract_text` ⇒ caller
/// skips this hit (degenerate input — null target var, missing
/// container/index expr, etc.). The three text slots correspond to
/// the three rewrite sites:
///   - `extract_text`: the `qint_t<W> __qram_h4_<N>; QRAM_read(...);`
///     block planted before the user's line.
///   - `replace_text`: the bare ancilla name `__qram_h4_<N>` planted
///     in place of the matched `a[i]` subscript.
///   - `adjoint_text`: the `__QRAM_read_adj(...);` call planted
///     immediately after the user's line.
///
/// `kind` echoes the originating `QramSubscriptExprHit::kind`.
struct QramExprEmission {
    std::string extract_text;
    std::string replace_text;
    std::string adjoint_text;
    QramContainerKind kind = QramContainerKind::StdArray;
};

/// Pure-string forward emission. Empty `target_name` / `container_text`
/// / `index_text` / `ancilla_name` ⇒ empty result (degenerate input,
/// skip). Used directly by unit tests; the AST-driven path delegates
/// here after recovering names from a `QramSubscriptExprHit`.
///
/// `W` follows the same convention as the D2 emitter
/// (`qram_emitter::emit_qram_forward_text`): when > 0, the emitted
/// declaration is `sturm::qint_t<W>`; when 0, falls back to the
/// legacy unqualified `qint` typename used by hermetic-stub fixtures.
///
/// `length_text` is consumed only for `Pointer` kind. Empty
/// `length_text` on a `Pointer` hit ⇒ placeholder comment per
/// PRD §11.1.6.
QramExprEmission emit_qram_expr_text(QramContainerKind kind,
                                     std::string_view ancilla_name,
                                     std::string_view container_text,
                                     std::string_view index_text,
                                     std::string_view length_text,
                                     unsigned W);

/// Apply per-hit forward + adjoint rewrites against `rw`. For each
/// non-degenerate hit:
///
///   1. Insert `sturm::qint_t<W> __qram_h4_<N>; ::sturm::QRAM_read(
///      a, [n,] i, __qram_h4_<N>);\n  ` immediately *before* the
///      user's VarDecl `qint c = ...;` line.
///   2. Replace the matched subscript source-range (`a[i]`) with the
///      bare ancilla name `__qram_h4_<N>`.
///   3. Insert `\n  ::sturm::__QRAM_read_adj(a, [n,] i, __qram_h4_<N>);`
///      immediately *after* the user's `;`.
///
/// When the same VarDecl carries multiple subscript hits (a
/// multi-subscript initializer), each hit gets its own ancilla, and
/// all extracts go before the line in matcher-traversal order; all
/// adjoints go after the line in reverse order (LIFO uncompute).
///
/// The LHS type rewrite (`qint c` -> `sturm::qint_t<W> c`) is applied
/// exactly once per `target_var` — the emitter dedupes by VarDecl
/// pointer.
///
/// `rw` must outlive the call. The function is a no-op when `hits`
/// is empty.
void emit_qram_expr_rewrites(clang::Rewriter& rw,
                             const std::vector<QramSubscriptExprHit>& hits);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_QRAM_EMITTER_EXPR_HPP
