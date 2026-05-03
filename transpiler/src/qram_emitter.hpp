// qram_emitter.hpp — sturm-u9ge.15 (Beat D2) rewrite emitter for the
// QRAM-via-array-subscript epic.
//
// Plan §7 / D2; PRD §8 / M4. Companion to `matcher_qram_subscript.{hpp,cpp}`
// (C1, sturm-u9ge.12). The matcher publishes a `QramSubscriptHit` per
// matched site; this emitter turns each hit into a single statement-level
// rewrite via the existing Clang `Rewriter` infrastructure used by
// `lossy_rewrite_emitter.cpp` and `modular_rewrite_emitter.cpp`.
//
// Per hit (PRD §8 + §11.1.5 — D0a per-shape emitted call line table):
//
//     // user wrote:
//     qint b = a[i];
//     // emitter produces (StdArray / CArray):
//     sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);
//     // emitter produces (Pointer, with recovered length n):
//     sturm::qint_t<W> b; ::sturm::QRAM_read(a, n, i, b);
//
// The emitter reads the original `qint b = a[i];` declaration source
// range (VarDecl begin loc through the trailing `;`) and replaces it
// with a two-line sequence: a fresh `sturm::qint_t<W> b;` declaration
// followed by the runtime `QRAM_read` call. Trailing comments after
// the user's `;` and column alignment of the surrounding lines are
// preserved verbatim — the Rewriter's source-range replacement leaves
// everything outside the `[begin, semi]` range untouched.
//
// Adjoint placement (PRD §11.4 / D0d.5)
// -------------------------------------
// When the QRAM read sits inside a `[[sturm::reversible]]` forward
// routine, D2 plants the matching uncompute call
//
//     sturm::invert<&::sturm::QRAM_read<...>>()(a, i, b);
//
// just before the enclosing reversible scope's close brace, mirroring
// how `adjoint_emitter.cpp` places `uncompute_*` calls today. When the
// enclosing function is not reversible, no adjoint is emitted — the
// caller's responsibility to manage `b`'s lifetime continues unchanged.
//
// Pointer-arm length recovery (PRD §11.1.6)
// -----------------------------------------
// The pointer overload's `n` length argument comes from the matcher's
// `length_text` field (populated by C1's
// `recover_pointer_length_text` heuristic — sibling integral-typed
// `ParmVarDecl` immediately following the container parameter). When
// `length_text` is empty (pointer hit with no recoverable length),
// the emitter renders a `qram-pointer-length-missing` placeholder
// comment so the rewritten file is unambiguous about the missing
// argument; v1 may legitimately ship with such hits diagnosed and
// not silently dropped.
//
// Negative shape: empty hit vector ⇒ no-op
// ---------------------------------------
// `emit_qram_rewrites(rw, {})` performs zero rewrites and zero
// rewriter mutations — the function is a no-op on an empty input,
// matching the posture every other rewrite emitter takes (the
// consumer drain loop's `for (const auto& hit : hits)` early-outs
// when `hits` is empty).
//
// LoC budget: <= 300 (plan §7 / D2).

#ifndef STURM_TRANSPILE_QRAM_EMITTER_HPP
#define STURM_TRANSPILE_QRAM_EMITTER_HPP

#include "matcher_qram_subscript.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace clang {
class Rewriter;
} // namespace clang

namespace sturm::transpile {

/// One forward emission record. Mirrors `LossyEmission` /
/// `ModularEmission`: empty `text` ⇒ caller skips this hit (degenerate
/// input — null target var, missing index/container expr, etc.). The
/// emitter populates `text` with the two-line rewrite
/// `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);\n` (or the
/// pointer-arm shape with an extra `n` argument).
///
/// `kind` echoes the originating `QramSubscriptHit::kind` so the
/// caller can dispatch any post-emit cleanup arm without re-walking
/// the AST (parallel to `LossyEmission::opcode` /
/// `ModularEmission::kind`).
struct QramEmission {
    std::string text;
    QramContainerKind kind = QramContainerKind::StdArray;
};

/// Pure-string forward emission. Empty `target_name` / `container_text` /
/// `index_text` ⇒ empty result (degenerate input, skip). Used directly
/// by unit tests; the AST-driven path delegates here after recovering
/// names from a `QramSubscriptHit`.
///
/// `W` (mirrors `LossyEmission` / `ModularEmission`'s sturm-czfi
/// pattern): when > 0, the emitted declaration is
/// `sturm::qint_t<W>`; when 0, falls back to the legacy unqualified
/// `qint` typename used by hermetic-stub fixtures.
///
/// `length_text` is consumed only for `Pointer` kind. Empty
/// `length_text` on a `Pointer` hit ⇒ placeholder comment per
/// PRD §11.1.6.
QramEmission emit_qram_forward_text(QramContainerKind kind,
                                    std::string_view target_name,
                                    std::string_view container_text,
                                    std::string_view index_text,
                                    std::string_view length_text,
                                    unsigned W);

/// Apply per-hit forward + adjoint rewrites against `rw`. For each
/// non-degenerate hit:
///
///   1. Replace the original `qint b = a[i];` declaration source range
///      (VarDecl begin loc → trailing `;` inclusive) with a two-line
///      `sturm::qint_t<W> b; ::sturm::QRAM_read(a, i, b);` sequence
///      (PRD §8). Trailing comments and column alignment are
///      preserved by the Rewriter's source-range replacement.
///
///   2. If the enclosing function is `[[sturm::reversible]]`, insert
///      a matching `sturm::invert<&::sturm::QRAM_read<...>>()(a, i, b);`
///      call before the reversible scope's close brace per D0d.5.
///
/// `rw` must outlive the call. The function is a no-op when `hits`
/// is empty.
void emit_qram_rewrites(clang::Rewriter& rw,
                        const std::vector<QramSubscriptHit>& hits);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_QRAM_EMITTER_HPP
