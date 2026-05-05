// test_qint_callsite_respelling_xchecks.cpp — sturm-1os7 (D1 split).
//
// Cross-checks shared by every per-surface drift-gate file. Pulled out of
// the per-surface files so each focuses on its own anchors and so a
// future change to the cross-check invariants is a one-place edit.
//
// Both invariants below are load-bearing: if either fails, the
// per-surface anchors degenerate into tautologies and the whole D0
// drift-gate goes silent.
//
// LoC budget: <= 200 (plan §1).

#include "sturm/qtypes/qint.hpp"

#include <type_traits>
#include <cstdio>

// Cross-check 1: pin sturm::frontend::qint != sturm::qint_t<64>. If they
// ever unify, every anchor-static_assert in the per-surface files
// degenerates into a tautology and this drift-gate goes silent.
static_assert(
    !std::is_same_v<sturm::frontend::qint, sturm::qint_t<64>>,
    "sturm::frontend::qint must be a distinct type from sturm::qint_t<64> "
    "for the D0 drift-gate to be load-bearing. If they ever unify, "
    "rewrite this regression to pin a different post-B1 invariant.");

// Cross-check 2: confirm `sturm::qint` (post-B1 namespace alias) resolves
// to the frontend class. This is the invariant that turns a future drift
// `sturm::qint_t<64> q(...)` -> `sturm::qint q(...)` into a hard compile
// error at the per-anchor static_asserts in the sibling files.
static_assert(
    std::is_same_v<sturm::qint, sturm::frontend::qint>,
    "Post-B1 (sturm-65rs.6), bare sturm::qint must alias "
    "sturm::frontend::qint. If this assertion ever fails, the D0 "
    "drift-gate's loud-red mode is no longer load-bearing.");

int main() {
    std::puts("test_qint_callsite_respelling_xchecks: OK (2 cross-checks pinned)");
    return 0;
}
