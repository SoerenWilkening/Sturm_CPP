// matcher_modular_op.hpp — sturm-r5pn.4 (Phase 0.4): AST matcher SHELL
// for the upcoming Phase 5 modular-arithmetic rewrites.
//
// Plan §2.1 / §7. This file is a SCAFFOLD: the matcher class compiles
// and registers cleanly against `ast_matchers::MatchFinder`, but
// `register_modular_op_matcher` adds NO patterns to the finder. Phase 5
// (sturm-r5pn epic, P5 transpiler-modular-rewrite) replaces the empty
// body with the three pattern families listed in plan §7.2:
//
//   1. `(qint OP qint) % qint`  for OP ∈ {+, *}        (always on)
//   2. peephole-collapsed compound `r = a + b; r %= n;` (always on)
//   3. `pow(a, x) % n`                                  (gated on
//                                                         STURM_MODULAR_POW)
//
// Why land an empty shell now? Phase 5 lands its TDD beats incrementally
// (5.1 add, 5.2 mul, 5.3 compound-collapse, 5.4 pow-default,
// 5.5 pow-flag, 5.6 int-exp, 5.7 idempotence, 5.8 non-pattern); each
// beat needs the matcher TU to already exist so the per-pattern callback
// can be appended without disturbing the register-call surface. The
// shell also pins the ordering relative to existing matchers (see the
// wiring comment in `transpile_consumer.cpp`) — Phase 5 must NOT
// re-shuffle the registration order to introduce a new module.
//
// The companion identity snapshot fixtures
// (`tests/transpiler/fixtures/modular_{add,mul,pow}_op*.cpp`) round-trip
// through the transpiler unchanged because this matcher emits no hits.
// Phase 5 will overwrite each fixture's `.expected.cpp` with the
// post-rewrite golden, at which point the same snapshot tests become
// the Phase-5 regressions.
//
// Hit-vector contract: identical shape to `LossyOpHit` (one struct per
// matched site, AST-bound non-owning pointers valid for the
// MatchFinder's ASTContext lifetime). The struct fields below cover the
// three pattern families in plan §7.2 with one enum discriminant per
// rewrite kind. Phase 5's emitter (`modular_rewrite_emitter.hpp`)
// consumes the vector after `matchAST`.
//
// LO-2 / LO-2e coexistence note: this matcher fires on AST shapes that
// are structurally disjoint from the lossy / nested-lossy matchers
// (`(a OP b) % n` is a binary-operator expression nested inside another
// binary-operator expression — neither pattern binds an outer
// `CXXOperatorCallExpr` compound-assign). No suppression bookkeeping is
// needed; both matchers can coexist in the same pass.

#ifndef STURM_TRANSPILE_MATCHER_MODULAR_OP_HPP
#define STURM_TRANSPILE_MATCHER_MODULAR_OP_HPP

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <string>
#include <vector>

namespace clang {
class BinaryOperator;
class CallExpr;
class CompoundStmt;
class DeclRefExpr;
class VarDecl;
} // namespace clang

namespace sturm::transpile {

/// Which modular-arithmetic rewrite family fired. Phase 5 dispatches
/// the emitter on this enum; the shell never produces a hit so the
/// values are reserved but unused for now.
///
///   - `AddMod`  : `(a + b) % n`              → `lib_add_mod_dsl(a, b, n, r)`
///   - `MulMod`  : `(a * b) % n`              → `lib_mul_mod_dsl(a, b, n, r)`
///   - `PowMod`  : `pow(a, x) % n`            → `lib_pow_mod_dsl(a, x, n, r)`
///                  (gated on STURM_MODULAR_POW; OFF leaves the AST alone)
enum class ModularOpKind { AddMod, MulMod, PowMod };

/// One matched modular-arithmetic site. Non-owning pointers reference
/// AST nodes valid only for the MatchFinder's ASTContext lifetime.
/// `enclosing_block` anchors the rewrite scope (parallel to
/// `LossyOpHit::enclosing_block`). Phase 5 populates the operand-name
/// strings off the bound DeclRefExpr nodes in its callback; the shell
/// version below leaves them empty because no callback runs.
///
/// Field-shape rationale: mirrors `LossyOpHit` so the consumer drain
/// loop in `transpile_consumer.cpp` can follow the same code shape (a
/// hits vector populated during `matchAST`, drained after the post-walk
/// backstops). Keeping the per-hit struct flat — no variant, no
/// optional — keeps Phase 5's emitter switch a single readable
/// function.
struct ModularOpHit {
    ModularOpKind kind = ModularOpKind::AddMod;
    /// Result variable name (the LHS of `qint_t<W> r = ...;` or the LHS
    /// of the compound-assign in the peephole-collapsed form).
    std::string result_name;
    /// Operand-name slots — Phase 5 fills these from bound DeclRefExpr
    /// nodes (`a`, `b`, `n`) or from the `pow` call's argument
    /// expressions for the PowMod kind.
    std::string a_name;
    std::string b_name;
    std::string n_name;
    /// `W` in `qint_t<W>` resolved off the result type, mirroring
    /// `LossyOpHit::lhs_width`. 0 ⇒ unknown / dependent (Phase 5
    /// emitters fall back to the bare `qint` typename).
    int result_width = 0;
    /// AST anchors — populated by Phase 5's per-pattern callback. The
    /// shell leaves them null; downstream consumers must null-check
    /// before dereferencing.
    const clang::BinaryOperator* mod_expr = nullptr;
    const clang::BinaryOperator* inner_op_expr = nullptr;
    const clang::CallExpr*       pow_call    = nullptr;
    const clang::VarDecl*        result_var  = nullptr;
    const clang::CompoundStmt*   enclosing_block = nullptr;
};

/// Register the modular-op AST matchers against `finder`, directing
/// every match into `hits`. SHELL: the current implementation registers
/// NO patterns — `hits` will remain empty after `matchAST`.
///
/// `hits` must outlive the finder's run. Call at most once per hits
/// vector. The function exists so `transpile_consumer.cpp` can wire it
/// in at its final ordering position now (Phase 5 only swaps the body,
/// not the call surface).
void register_modular_op_matcher(clang::ast_matchers::MatchFinder& finder,
                                 std::vector<ModularOpHit>& hits);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_MODULAR_OP_HPP
