// modular_rewrite_emitter.hpp — Phase 5 (sturm-qzab) modular-arithmetic
// rewrite emitter.
//
// Plan §2.1 / §7. Companion to `matcher_modular_op.{hpp,cpp}`. The
// matcher produces a `ModularOpHit` per matched site; this emitter
// turns each hit into a single statement-level rewrite: the user's
// `qint_t<W> r = (a OP b) % n;` declaration is replaced with a call
// to the corresponding qint-level free function (`add_mod` / `mul_mod`
// / `pow_mod`).
//
// Beat 5.1 (sturm-qzab.1) implements the AddMod arm:
//
//     // user wrote:
//     qint_t<W> r = (a + b) % n;
//     // emitter produces:
//     sturm::qint_t<W> r = ::sturm::add_mod(a, b, n);
//
// Beat 5.2 (sturm-qzab.2) mirrors that for the MulMod arm:
//
//     // user wrote:
//     qint_t<W> r = (a * b) % n;
//     // emitter produces:
//     sturm::qint_t<W> r = ::sturm::mul_mod(a, b, n);
//
// Beats 5.4-5.6 fill in the PowMod arm following the same
// single-statement substitution shape. Unlike the LO-2 lossy rewrites,
// modular rewrites do NOT inject a scope-exit cleanup — `r` is a
// freshly-bound register whose lifetime is the user's intent.
//
// Why `::sturm::add_mod` and not `lib_add_mod_dsl` directly? PRD §2.1
// phrases the rewrite target as `lib_add_mod_dsl(a.bits(), b.bits(),
// n.bits(), W, r.bits())`, but `qint_t<W>` does not expose a `bits()`
// method — the lib-level primitive takes raw `Bit*` arrays. The
// public free function `sturm::add_mod` (PRD §3.1, plan §6.2) is the
// qint-friendly wrapper that allocates the fresh result register and
// dispatches to `lib_add_mod_dsl` internally. Emitting the wrapper
// is the same posture LO-2b takes when emitting `mul_oop` /
// `and_oop` instead of `lib_c_MUL_dsl` / `lib_c_AND_dsl` directly.
//
// Pure-string testability: the text-only entry point delegates here
// without spinning up an ASTContext, mirroring `emit_lossy_forward_text`
// so the unit tests in `transpiler/tests/test_modular_rewrite_emitter.cpp`
// can byte-compare emissions.

#ifndef STURM_TRANSPILE_MODULAR_REWRITE_EMITTER_HPP
#define STURM_TRANSPILE_MODULAR_REWRITE_EMITTER_HPP

#include "matcher_modular_op.hpp"

#include <string>
#include <string_view>

namespace sturm::transpile {

class FreshNameAllocator;

/// One forward emission record. Matches the shape of `LossyEmission`:
/// empty `text` ⇒ caller skips this hit. The AddMod arm (beat 5.1)
/// populates `text` with the single-statement rewrite
/// `sturm::qint_t<W> r = ::sturm::add_mod(a, b, n);\n`; the MulMod arm
/// (beat 5.2) mirrors that with `::sturm::mul_mod(...)`. Beats 5.4-5.6
/// follow the same shape for PowMod.
///
/// `cleanup_anchor_name` carries the result-variable name. AddMod /
/// MulMod do not inject a scope-exit cleanup, so the consumer drain
/// uses this slot only as a diagnostic breadcrumb. PowMod's deferred
/// adjoint-swap pattern (beat 5.6) may consume it.
///
/// `kind` echoes the originating `ModularOpHit::kind` so the consumer
/// drain loop can dispatch any cleanup arm without re-walking the AST
/// (parallel to `LossyEmission::opcode`).
struct ModularEmission {
    std::string text;
    std::string cleanup_anchor_name;
    ModularOpKind kind = ModularOpKind::AddMod;
};

/// Pure-string forward emission. Empty `result` / `a` / `b` (or `n`
/// for the gated PowMod arm) ⇒ empty result (degenerate input, skip)
/// — the consumer's `if (em.text.empty()) continue;` arm fires.
///
/// `result_width` (mirrors `LossyEmission`'s sturm-czfi sibling): when
/// > 0, the emitted result-allocation is `sturm::qint_t<result_width>`;
/// when 0, falls back to the legacy unqualified `qint` typename used
/// by hermetic-stub fixtures.
///
/// `alloc` is reserved for beats 5.4-5.6 (PowMod) which may need fresh
/// ancilla names; the AddMod / MulMod arms (beats 5.1 / 5.2) do not
/// consume a slot.
ModularEmission emit_modular_forward_text(ModularOpKind kind,
                                          std::string_view result,
                                          std::string_view a,
                                          std::string_view b,
                                          std::string_view n,
                                          int result_width,
                                          FreshNameAllocator& alloc);

/// AST-aware overload: pulls the operand names + width off `hit` and
/// delegates to the pure-string overload. Source-map (`#line`)
/// prefixing is the wiring layer's concern, not this emitter's.
ModularEmission emit_modular_forward(const ModularOpHit& hit,
                                     FreshNameAllocator& alloc);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MODULAR_REWRITE_EMITTER_HPP
