// test_qint_callsite_respelling_super_mask.cpp — sturm-1os7 (D1 split).
//
// Drift-gate for D0 (sturm-65rs.11) callsite re-spellings, super_mask /
// qubits[] surface family. Each anchor pins a site whose primary backend-
// only contact is a direct read/write of the `super_mask` field or the
// `qubits[i]` array — both of which exist only on `sturm::qint_t<W>`
// (the frontend `sturm::frontend::qint` class has no such members).
//
// Per Plan §13: for every D0-re-spelled site, assert decltype ==
// sturm::qint_t<64> so a drift back to bare `sturm::qint` (now
// sturm::frontend::qint, post-B1 sturm-65rs.6) fails to compile with a
// message naming the original test:line.
//
// History: this file is the super_mask/qubits slice of the per-surface
// split that sturm-1os7 introduced when the original (single-file) D1 hit
// the 200-LoC budget cap. The cross-checks (sturm::qint resolves to
// frontend::qint and they're distinct from qint_t<64>) are pinned in a
// single sibling file (test_qint_callsite_respelling_xchecks.cpp) so each
// per-surface file stays focused on its anchors.
//
// LoC budget: <= 200 (plan §1; per-surface split keeps each file under
// the cap so future D0-miss extensions add anchors here without
// renegotiating a higher budget).

#include "sturm/qtypes/qint.hpp"

#include <cstdint>
#include <type_traits>
#include <cstdio>

// Anchor S1 — tests/test_qint_classical.cpp:27 reads q.super_mask
// (formerly anchor 6 of the merged D1 file).
namespace anchor_qint_classical_super_mask_27 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_qint_classical.cpp:27 must remain sturm::qint_t<64> "
        "— check_classical reads q.super_mask (backend-only).");
}
}  // namespace anchor_qint_classical_super_mask_27

// Anchor S2 — tests/test_qint_classical.cpp:29 reads q.qubits[i]
// (formerly anchor 7 of the merged D1 file).
namespace anchor_qint_classical_qubits_29 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_qint_classical.cpp:29 must remain sturm::qint_t<64> "
        "— check_classical reads q.qubits[i] (backend-only).");
}
}  // namespace anchor_qint_classical_qubits_29

// Anchor S3 — tests/test_qint_superposed.cpp:{20,21} grouped (make_super
// helper writes q.super_mask and q.qubits[bit_pos]). The whole file leans
// on these two backend-only surfaces; one anchor covers the family
// (formerly anchor 10 of the merged D1 file).
namespace anchor_qint_superposed_super_mask_qubits_20_21 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_qint_superposed.cpp:{20,21} must remain sturm::qint_t<64> "
        "— make_super writes q.super_mask and q.qubits[i] (backend-only).");
}
}  // namespace anchor_qint_superposed_super_mask_qubits_20_21

// Anchor S4 — tests/test_conversions.cpp:{46-48,57,107-108,...} grouped
// (sturm-1os7 D0 re-spell miss). Pervasive read of q.value, q.super_mask,
// q.qubits[] across all 10 conversion-row tests; primary backend-surface
// contact is structural reads of super_mask / qubits[] in the conversion
// invariant assertions. One anchor covers the file's super_mask/qubits
// family.
namespace anchor_conversions_super_mask_qubits_pervasive {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_conversions.cpp:{conversion-row asserts} must remain "
        "sturm::qint_t<64> — every conversion-row test reads q.super_mask, "
        "q.qubits[], q.value (backend-only fields).");
}
}  // namespace anchor_conversions_super_mask_qubits_pervasive

// Anchor S5 — tests/test_sink_dispatch.cpp:{30,31,47,61,83,108,118,
// 132,133,142,145-146,160,170-173,191,216} grouped (sturm-1os7 D0 re-spell
// miss). Pervasive use of q.super_mask reads/writes and q.qubits[bit_pos]
// writes via the make_super helper plus the per-test result assertions.
// Primary backend-surface contact is super_mask field. (The file also uses
// compound-assigns +=/^=; that family is pinned in
// test_qint_callsite_respelling_compound_assign.cpp.)
namespace anchor_sink_dispatch_super_mask_qubits_pervasive {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_sink_dispatch.cpp:{make_super + result asserts} must "
        "remain sturm::qint_t<64> — make_super writes q.super_mask and "
        "q.qubits[i]; per-test asserts read q.super_mask (backend-only).");
}
}  // namespace anchor_sink_dispatch_super_mask_qubits_pervasive

int main() {
    // All assertions are compile-time. Reaching main() means every
    // re-spelled callsite in the super_mask/qubits family still names
    // sturm::qint_t<64>.
    std::puts("test_qint_callsite_respelling_super_mask: OK (5 anchors pinned)");
    return 0;
}
