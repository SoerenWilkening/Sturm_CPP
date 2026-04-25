// lossy_nested_rewrite.hpp — LO-2e (sturm-rry6): depth-first rewrite for
// nested lossy ops. PRD §11 + plan §4 LO-2e. LO-2a's matcher requires
// arg(1) to be a bare DRE; this module covers the disjoint outer-lossy /
// inner-bare-binary-op shape.

#ifndef STURM_TRANSPILE_LOSSY_NESTED_REWRITE_HPP
#define STURM_TRANSPILE_LOSSY_NESTED_REWRITE_HPP

#include "matcher_lossy_op.hpp"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include <string>
#include <vector>

namespace clang { class CompoundStmt; class CXXOperatorCallExpr; }

namespace sturm::transpile {

class FreshNameAllocator;

/// One nested-lossy match (e.g. `a *= (b & c)`). Kinds reuse LossyOpKind
/// (bare ↔ assign sharing the kind→tag/dsl table).
struct NestedLossyHit {
    LossyOpKind outer_kind;
    LossyOpKind inner_kind;
    std::string lhs_name, inner_lhs_name, inner_rhs_name;
    const clang::CXXOperatorCallExpr* outer_call = nullptr;
    const clang::CompoundStmt* enclosing_block = nullptr;
};

/// Inner-first forward + LIFO outer-first cleanup.
struct NestedLossyEmission {
    std::string forward_text, cleanup_text;
    std::string inner_tmp_name, outer_tmp_name;
};

void register_nested_lossy_matcher(clang::ast_matchers::MatchFinder& finder,
                                   std::vector<NestedLossyHit>& hits);

NestedLossyEmission emit_nested_lossy(const NestedLossyHit& hit,
                                      FreshNameAllocator& alloc);

} // namespace sturm::transpile

#endif
