// lossy_scope_exit_emitter.hpp — LO-2c (sturm-hbwr): scope-exit cleanup
// for every LossyOpHit + LossyEmission pair. PRD §2.3 emits
// `swap(lhs, swap_target); sturm::invert<&::sturm::lib_<X>_dsl>()(...);`
// in LIFO at the enclosing CompoundStmt's close brace. LO-2b carries
// swap_target_name / aux_tmp_name. LO-2d (sturm-wva7) layers the
// PRD §4.3 main-outer-scope exception on top: when the enclosing
// CompoundStmt IS `main`'s outermost body, cleanup is suppressed (the
// program terminates at main's closing brace, so a reverse swap +
// invert would be dead code).

#ifndef STURM_TRANSPILE_LOSSY_SCOPE_EXIT_EMITTER_HPP
#define STURM_TRANSPILE_LOSSY_SCOPE_EXIT_EMITTER_HPP

#include "lossy_rewrite_emitter.hpp"
#include "matcher_lossy_op.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace clang {
class ASTContext;
class CompoundStmt;
} // namespace clang

namespace sturm::transpile {

/// One scope-exit cleanup record. Empty `text` ⇒ skip (LO-2b posture).
struct LossyCleanupEmission {
    std::string text;
    LossyOpKind opcode = LossyOpKind::MulAssign;
    const clang::CompoundStmt* enclosing_block = nullptr;
};

/// Concatenated cleanup keyed by enclosing block. Wiring layer plants
/// `text` verbatim before `enclosing_block`'s close brace.
struct BlockCleanup {
    const clang::CompoundStmt* enclosing_block = nullptr;
    std::string text;
};

/// Pure-string per-hit cleanup. Empty operand or empty swap target ⇒
/// empty result.
LossyCleanupEmission emit_lossy_cleanup_text(LossyOpKind kind,
                                             std::string_view lhs,
                                             std::string_view rhs,
                                             std::string_view swap_target,
                                             std::string_view aux_tmp);

/// AST-aware overload: combines hit (operand names + enclosing block)
/// with the matching forward emission (tmp names) into one cleanup.
LossyCleanupEmission emit_lossy_cleanup(const LossyOpHit& hit,
                                        const LossyEmission& forward);

/// Group hits by `enclosing_block`; emit each block's cleanup string in
/// REVERSE input order (LIFO). Block order in the output follows
/// first-hit-encounter order on the inputs. Mismatched vector sizes ⇒
/// empty result.
///
/// When `ctx` is non-null, every block whose `is_main_outer_block(ctx)`
/// returns true (PRD §4.3) is recorded with an EMPTY `text` — cleanup
/// is suppressed. The block entry stays in the output so callers can
/// see "we matched a hit here but did not emit cleanup". Passing
/// `ctx == nullptr` disables the check (legacy 2-arg form below).
std::vector<BlockCleanup>
group_cleanups_by_block(const std::vector<LossyOpHit>& hits,
                        const std::vector<LossyEmission>& forwards,
                        clang::ASTContext* ctx);

/// Legacy 2-arg overload: equivalent to passing `ctx == nullptr` (no
/// main-outer suppression). Retained so existing callers compile
/// without source change.
std::vector<BlockCleanup>
group_cleanups_by_block(const std::vector<LossyOpHit>& hits,
                        const std::vector<LossyEmission>& forwards);

/// PRD §4.3 predicate. Returns true iff `block` is the outermost
/// CompoundStmt of a FunctionDecl whose name is exactly `main` — i.e.
/// the immediate parent of `block` in the AST is such a FunctionDecl.
/// Lambda bodies (parent = closure's `operator()`), nested if/while
/// blocks (parent = IfStmt/WhileStmt), and bodies of any non-main
/// function all return false. `block == nullptr` returns false.
bool is_main_outer_block(const clang::CompoundStmt* block,
                         clang::ASTContext& ctx);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_LOSSY_SCOPE_EXIT_EMITTER_HPP
