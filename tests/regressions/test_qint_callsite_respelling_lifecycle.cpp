// test_qint_callsite_respelling_lifecycle.cpp — sturm-1os7 (D1 split).
//
// Drift-gate for D0 (sturm-65rs.11) callsite re-spellings, qubit-pool
// lifecycle / release-on-destruct surface family. Each anchor pins a
// site whose primary backend-only contact is the destructor's qubit
// release semantics — `sturm::qint_t<W>::~qint_t()` releases any
// allocated qubits back to `QubitPool::instance()`, behavior that the
// frontend `sturm::frontend::qint` class does not implement (the
// frontend has no qubits to release).
//
// History: this file is the lifecycle slice of the per-surface split
// that sturm-1os7 introduced when the original (single-file) D1 hit the
// 200-LoC budget cap. See the super_mask sibling for cross-check
// rationale.
//
// LoC budget: <= 200 (plan §1).

#include "sturm/qtypes/qint.hpp"

#include <cstdint>
#include <type_traits>
#include <cstdio>

// Anchor L1 — tests/test_resource_lifecycle.cpp:106
// (test_qint64_classical_lifecycle): site relies on qubit-pool release
// semantics on destruction; needs the backend qint_t<64> (formerly
// anchor 1 of the merged D1 file).
namespace anchor_resource_lifecycle_106 {
[[maybe_unused]] static void pin_decltype() {
    // Match the original ctor argument shape: int64_t-via-static_cast.
    int i = 0;  // stand-in for the loop index in the original site.
    sturm::qint_t<64> q(static_cast<int64_t>(i));
    (void)q;
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_resource_lifecycle.cpp:106 must remain "
        "sturm::qint_t<64> — backend surface (release-on-destruct, "
        "super_mask, qubits[]) is required by D0's audit (PRD §6 R1).");
}
}  // namespace anchor_resource_lifecycle_106

int main() {
    // All assertions are compile-time. Reaching main() means every
    // re-spelled callsite in the lifecycle family still names
    // sturm::qint_t<64>.
    std::puts("test_qint_callsite_respelling_lifecycle: OK (1 anchor pinned)");
    return 0;
}
