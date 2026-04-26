// modular_rewrite_emitter.hpp — sturm-r5pn.4 (Phase 0.4): emitter SHELL
// for the upcoming Phase 5 modular-arithmetic rewrites.
//
// Plan §2.1 / §7. Companion to `matcher_modular_op.{hpp,cpp}`. The
// matcher produces a `ModularOpHit` per matched site; this emitter
// turns each hit into a forward call to one of the new
// `lib_{add,mul,pow}_mod_dsl` primitives plus the matching scope-exit
// adjoint. Phase 5 fills in the body following the LO-2b shape
// (`lossy_rewrite_emitter.hpp`).
//
// Forward emission shape Phase 5 will produce (see plan §7.2):
//
//     // (a + b) % n
//     sturm::qint_t<W> r{};
//     ::sturm::lib_add_mod_dsl(a, b, n, r);
//     // (scope exit, injected by the LO-2c-shaped sibling emitter)
//     sturm::invert<&::sturm::lib_add_mod_dsl<W>>()(a, b, n, r);
//
// With the emitter currently a SHELL, every entry-point function below
// returns an empty `ModularEmission` so the caller's
// `if (em.text.empty()) continue;` short-circuit fires unconditionally.
// Identity snapshot fixtures (modular_{add,mul}_op,
// modular_pow_op_{default,flag}) thus round-trip unchanged regardless
// of `STURM_MODULAR_POW`.
//
// Pure-string testability: Phase 5's text-only entry point delegates
// here without spinning up an ASTContext, mirroring
// `emit_lossy_forward_text` so the unit tests in
// `transpiler/tests/test_*emitter.cpp` can byte-compare emissions.

#ifndef STURM_TRANSPILE_MODULAR_REWRITE_EMITTER_HPP
#define STURM_TRANSPILE_MODULAR_REWRITE_EMITTER_HPP

#include "matcher_modular_op.hpp"

#include <string>
#include <string_view>

namespace sturm::transpile {

class FreshNameAllocator;

/// One forward emission record. Matches the shape of `LossyEmission`:
/// empty `text` ⇒ caller skips this hit. Phase 5 populates `text` with
/// the §7.2 forward-pair triplet (allocate result, call primitive,
/// optional swap) and `cleanup_anchor_name` with the result-variable
/// name the LO-2c-shaped scope-exit emitter uses to mint the matching
/// adjoint at the enclosing block's close brace.
///
/// `kind` echoes the originating `ModularOpHit::kind` so the consumer
/// drain loop can dispatch the cleanup arm without re-walking the AST
/// (parallel to `LossyEmission::opcode`).
struct ModularEmission {
    std::string text;
    std::string cleanup_anchor_name;
    ModularOpKind kind = ModularOpKind::AddMod;
};

/// Pure-string forward emission. Empty `result` / `a` / `b` (or `n`
/// for the gated PowMod arm) ⇒ empty result (degenerate input, skip).
/// Phase 5 wires this overload from `transpile_consumer.cpp` after
/// recovering names off a `ModularOpHit`. SHELL: returns `{}`.
///
/// `result_width` (mirrors `LossyEmission`'s sturm-czfi sibling): when
/// > 0, the emitted result-allocation is `sturm::qint_t<result_width>`;
/// when 0, falls back to the legacy unqualified `qint` typename used
/// by hermetic-stub fixtures.
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
/// SHELL: returns `{}`.
ModularEmission emit_modular_forward(const ModularOpHit& hit,
                                     FreshNameAllocator& alloc);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MODULAR_REWRITE_EMITTER_HPP
