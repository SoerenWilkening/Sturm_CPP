// matcher_lossy_op.hpp — LO-2a (sturm-9254): AST matcher for lossy
// compound-assigns on qint_t. See docs/plan_lossy_compound_reversibility.md
// §4 and PRD §7.
//
// Anchors on `CXXOperatorCallExpr` (NOT `CompoundAssignOperator`): qint_t
// overloads `operator*=` etc. as members, so overloaded compound-assigns
// lower to CXXOperatorCallExpr. Builtin `int *= int` lowers to
// CompoundAssignOperator and is rejected structurally.
//
// Records one `LossyOpHit` per match into a caller-owned vector. LO-2b
// consumes hits to emit the forward compute-swap pair; LO-2c uses
// `enclosing_block` to anchor the LIFO scope-exit cleanup at the block's
// close brace. Main-outer-scope suppression (PRD §4.3) is LO-2d's
// concern, not this matcher's.

#ifndef STURM_TRANSPILE_MATCHER_LOSSY_OP_HPP
#define STURM_TRANSPILE_MATCHER_LOSSY_OP_HPP

#include "clang/ASTMatchers/ASTMatchFinder.h"

#include <string>
#include <vector>

namespace clang {
class CompoundStmt;
class CXXOperatorCallExpr;
class DeclRefExpr;
} // namespace clang

namespace sturm::transpile {

/// Which of the five PRD §2.2 lossy compound-assigns fired. See PRD §2.4
/// dispatch table for the DSL primitive each maps to. Order mirrors
/// plan §4 (MUL/DIV/MOD first, then AND/OR).
enum class LossyOpKind { MulAssign, DivAssign, ModAssign, AndAssign, OrAssign };

/// One matched lossy compound-assignment. Non-owning pointers reference
/// AST nodes valid only for the MatchFinder's ASTContext lifetime.
/// `enclosing_block` is the nearest enclosing CompoundStmt — LO-2c's
/// anchor for the scope-exit cleanup. Null-enclosed hits are skipped.
struct LossyOpHit {
    LossyOpKind opcode;
    std::string lhs_name;
    std::string rhs_name;
    const clang::CXXOperatorCallExpr* call = nullptr;
    const clang::DeclRefExpr* lhs_ref = nullptr;
    const clang::DeclRefExpr* rhs_ref = nullptr;
    const clang::CompoundStmt* enclosing_block = nullptr;
};

/// Register five AST matchers (one per lossy operator) against `finder`,
/// directing every match into `hits`. `hits` must outlive the finder's
/// run. Call at most once per hits vector.
void register_lossy_op_matcher(clang::ast_matchers::MatchFinder& finder,
                               std::vector<LossyOpHit>& hits);

} // namespace sturm::transpile

#endif // STURM_TRANSPILE_MATCHER_LOSSY_OP_HPP
