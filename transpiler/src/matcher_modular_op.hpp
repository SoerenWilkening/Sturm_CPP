// matcher_modular_op.hpp — Phase 5 (sturm-qzab) modular-arithmetic AST
// matcher.
//
// Plan §2.1 / §7. Beat 5.1 (sturm-qzab.1) lands the AddMod arm:
// recognises the AST shape `qint_t<W> r = (a + b) % n;` and produces
// one `ModularOpHit` per match. Subsequent beats append additional
// `register_one<...>` calls inside `register_modular_op_matcher`:
//
//   1. `(qint + qint) % qint` → AddMod   (beat 5.1, this beat)
//   2. `(qint * qint) % qint` → MulMod   (beat 5.2)
//   3. peephole-collapsed `r = a + b; r %= n;` (beat 5.3)
//   4. `pow(qint, qint) % qint` → PowMod (beats 5.4–5.6, gated on
//                                          STURM_MODULAR_POW)
//
// Each beat's per-pattern callback is appended to a shared hits vector;
// the consumer drain in `transpile_consumer.cpp` dispatches on the
// `ModularOpKind` discriminant so a single drain loop handles all
// three families.
//
// Hit-vector contract: identical shape to `LossyOpHit` (one struct per
// matched site, AST-bound non-owning pointers valid for the
// MatchFinder's ASTContext lifetime). The struct fields below cover the
// three pattern families in plan §7.2 with one enum discriminant per
// rewrite kind. The companion emitter (`modular_rewrite_emitter.hpp`)
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
class CallExpr;
class CompoundStmt;
class CXXOperatorCallExpr;
class DeclRefExpr;
class VarDecl;
} // namespace clang

namespace sturm::transpile {

/// Which modular-arithmetic rewrite family fired. The consumer drain
/// dispatches on this enum so a single loop handles all three rewrite
/// families. Beat 5.1 produces only `AddMod` hits; later beats add
/// `MulMod` and `PowMod`.
///
///   - `AddMod`  : `(a + b) % n`   → `::sturm::add_mod(a, b, n)`
///   - `MulMod`  : `(a * b) % n`   → `::sturm::mul_mod(a, b, n)` (beat 5.2)
///   - `PowMod`  : `pow(a, x) % n` → `::sturm::pow_mod(a, x, n)` (beats 5.4–5.6,
///                  gated on STURM_MODULAR_POW; OFF leaves the AST alone)
enum class ModularOpKind { AddMod, MulMod, PowMod };

/// One matched modular-arithmetic site. Non-owning pointers reference
/// AST nodes valid only for the MatchFinder's ASTContext lifetime.
/// `enclosing_block` anchors the rewrite scope (parallel to
/// `LossyOpHit::enclosing_block`). Beat 5.1's AddMod callback populates
/// the operand-name strings off the bound DeclRefExpr nodes; later
/// beats follow the same shape for MulMod / PowMod.
///
/// Field-shape rationale: mirrors `LossyOpHit` so the consumer drain
/// loop in `transpile_consumer.cpp` can follow the same code shape (a
/// hits vector populated during `matchAST`, drained after the post-walk
/// backstops). Keeping the per-hit struct flat — no variant, no
/// optional — keeps the emitter switch a single readable function.
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
    /// AST anchors — populated by the per-pattern callback. Downstream
    /// consumers must null-check before dereferencing. The `qint_t<W>`
    /// user-defined `operator+` / `operator*` / `operator%` overloads
    /// land in the AST as `CXXOperatorCallExpr` (Clang normalizes both
    /// member and free operator overloads to that node class), so the
    /// AddMod / MulMod arms anchor on `CXXOperatorCallExpr`. The
    /// shell-stage typing of these slots as `BinaryOperator`
    /// (sturm-r5pn.4 Phase 0.4) was adjusted in Beat 5.1 once the AST
    /// shape produced by the actual `(qint + qint) % qint` fixture was
    /// confirmed via a Clang AST dump.
    const clang::CXXOperatorCallExpr* mod_expr = nullptr;
    const clang::CXXOperatorCallExpr* inner_op_expr = nullptr;
    const clang::CallExpr*       pow_call    = nullptr;
    const clang::VarDecl*        result_var  = nullptr;
    const clang::CompoundStmt*   enclosing_block = nullptr;
};

/// Register the modular-op AST matchers against `finder`, directing
/// every match into `hits`. Beat 5.1 registers the AddMod arm; later
/// beats append MulMod / PowMod patterns inside the same function.
/// `hits` must outlive the finder's run. Call at most once per hits
/// vector.
void register_modular_op_matcher(clang::ast_matchers::MatchFinder& finder,
                                 std::vector<ModularOpHit>& hits);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_MODULAR_OP_HPP
