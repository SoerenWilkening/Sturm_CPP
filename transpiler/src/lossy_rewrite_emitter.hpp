// lossy_rewrite_emitter.hpp — LO-2b (sturm-yxxa): forward
// allocate-compute-swap pair for every LossyOpHit produced by LO-2a.
// See docs/plan_lossy_compound_reversibility.md §4 row LO-2b and
// docs/prd_lossy_compound_reversibility.md §2.2 / §2.4.
//
// Per PRD §2.4 each lossy compound assignment desugars to a fresh
// ancilla + an OOP DSL call + a swap. `<N>` is a single global counter
// per FreshNameAllocator instance, monotonic across opcodes — matches
// the LO-0.3 fixture pattern (`__sturm_tmp_and_0` then
// `__sturm_tmp_mul_1`). LO-2c (sturm-hbwr) reads `swap_target_name` to
// build its scope-exit cleanup, so both emitters must share the same
// allocator instance to keep counter suffixes paired.
//
// FORWARD pair only — no scope-exit cleanup. Returned text has no
// `#line` directives; the wiring layer prefixes those when assembling
// QReplacement objects so unit tests can byte-compare emissions
// without spinning up a SourceManager.

#ifndef STURM_TRANSPILE_LOSSY_REWRITE_EMITTER_HPP
#define STURM_TRANSPILE_LOSSY_REWRITE_EMITTER_HPP

#include "matcher_lossy_op.hpp"

#include <string>
#include <string_view>

namespace sturm::transpile {

class FreshNameAllocator;

/// One forward emission record. Empty `text` + empty
/// `swap_target_name` signal a degenerate input the caller must skip.
/// `aux_tmp_name` is the partner divide ancilla for `/=` / `%=` and
/// empty for the three single-ancilla opcodes.
struct LossyEmission {
    std::string text;
    std::string swap_target_name;
    std::string aux_tmp_name;
    LossyOpKind opcode = LossyOpKind::MulAssign;
};

/// Pure-string forward emission. Empty `lhs` or `rhs` ⇒ empty result
/// (degenerate input, skip). Used directly by tests; the AST overload
/// below delegates here after recovering names from a LossyOpHit.
///
/// `lhs_width` (sturm-czfi): when > 0, the ancilla declaration is emitted
/// as `sturm::qint_t<lhs_width>` (resolves to a concrete type even when the
/// generated TU does not carry a `using qint = ...;` typedef — the case the
/// real example targets exercise). When 0, the legacy unqualified `qint`
/// typename is emitted (the hermetic-stub fixture path that pre-dates
/// sturm-czfi). The 4-arg overload below preserves the legacy call shape
/// by defaulting to 0.
LossyEmission emit_lossy_forward_text(LossyOpKind kind,
                                      std::string_view lhs,
                                      std::string_view rhs,
                                      int lhs_width,
                                      FreshNameAllocator& alloc);

LossyEmission emit_lossy_forward_text(LossyOpKind kind,
                                      std::string_view lhs,
                                      std::string_view rhs,
                                      FreshNameAllocator& alloc);

/// AST-aware overload: pulls `lhs_name` / `rhs_name` / `lhs_width` off `hit`
/// and delegates. Source-map (`#line`) prefixing is the wiring layer's
/// concern, not this emitter's.
LossyEmission emit_lossy_forward(const LossyOpHit& hit,
                                 FreshNameAllocator& alloc);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_LOSSY_REWRITE_EMITTER_HPP
