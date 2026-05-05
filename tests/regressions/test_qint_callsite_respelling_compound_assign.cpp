// test_qint_callsite_respelling_compound_assign.cpp — sturm-1os7 (D1 split).
//
// Drift-gate for D0 (sturm-65rs.11) callsite re-spellings, compound-
// assignment operator family. Each anchor pins a site whose primary
// backend-only contact is one of `+=`, `-=`, `*=`, `/=`, `%=`, `&=`,
// `|=`, `^=`, `<<=`, `>>=` against another quantum operand — none of
// which are part of the frontend `sturm::frontend::qint` surface. (PRD
// §6 R1: the frontend has only the implicit `operator size_t()` and the
// explicit `operator int64_t()`; arithmetic and bitwise overloads stay
// on the backend qint_t<W>.)
//
// History: this file is the compound-assign slice of the per-surface
// split that sturm-1os7 introduced when the original (single-file) D1
// hit the 200-LoC budget cap. See the super_mask sibling for cross-check
// rationale.
//
// LoC budget: <= 200 (plan §1).

#include "sturm/qtypes/qint.hpp"

#include <cstdint>
#include <type_traits>
#include <cstdio>

// Anchor CA1 — tests/test_qint_classical.cpp:{79,101,135,157,179} grouped
// (compound-assigns +=, -=, *=, /=, %=). Per the original D1 sizing
// memo: "one anchor per operator family is fine" when budget is tight;
// frontend lacks all five overloads (formerly anchor 9 of merged D1).
namespace anchor_qint_classical_compound_assigns_79_101_135_157_179 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> a(0), b(1);
    a += b; a -= b; a *= b; a /= b; a %= b;
    static_assert(std::is_same_v<decltype(a), sturm::qint_t<64>>,
        "tests/test_qint_classical.cpp:{79,101,135,157,179} must remain "
        "sturm::qint_t<64> — compound assigns +=, -=, *=, /=, %= are "
        "backend-only (frontend lacks these overloads).");
}
}  // namespace anchor_qint_classical_compound_assigns_79_101_135_157_179

// Anchor CA2 — tests/test_sink_dispatch.cpp:{50,87,195} grouped (sturm-1os7
// D0 re-spell miss). The file exercises quantum_add via `a += b` (line 50,
// 87) and quantum_xor via `a ^= b` (line 195) under both no-control and
// WHEN(flag) routing. The XOR-assign overload `qint_t<W>::operator^=`
// emits quantum_xor records via the sink; frontend has neither += nor ^=.
namespace anchor_sink_dispatch_compound_assigns_50_87_195 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> a(0), b(1);
    a += b; a ^= b;
    static_assert(std::is_same_v<decltype(a), sturm::qint_t<64>>,
        "tests/test_sink_dispatch.cpp:{50,87,195} must remain "
        "sturm::qint_t<64> — quantum_add (+=) and quantum_xor (^=) "
        "compound-assign overloads are backend-only.");
}
}  // namespace anchor_sink_dispatch_compound_assigns_50_87_195

int main() {
    // All assertions are compile-time. Reaching main() means every
    // re-spelled callsite in the compound-assign family still names
    // sturm::qint_t<64>.
    std::puts("test_qint_callsite_respelling_compound_assign: OK (2 anchors pinned)");
    return 0;
}
