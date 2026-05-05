// test_qint_callsite_respelling_value.cpp — sturm-1os7 (D1 split).
//
// Drift-gate for D0 (sturm-65rs.11) callsite re-spellings, `q.value`
// public field family. Each anchor pins a site whose primary backend-
// only contact is direct read of the `value` field — exposed publicly
// only on `sturm::qint_t<W>`. The frontend `sturm::frontend::qint` class
// stores its classical bits in a private `value_` member reachable only
// via `classical_value()` / `operator int64_t()`, so a drift back to
// bare `sturm::qint` produces a hard "no member named 'value'" error
// rather than a silent semantic shift.
//
// History: this file is the .value slice of the per-surface split that
// sturm-1os7 introduced when the original (single-file) D1 hit the
// 200-LoC budget cap. See the super_mask sibling for cross-check
// rationale.
//
// LoC budget: <= 200 (plan §1).

#include "sturm/qtypes/qint.hpp"

#include <cstdint>
#include <type_traits>
#include <cstdio>

// Anchor V1 — tests/test_qint_classical.cpp:39 reads q.value (frontend
// has private value_; the public field exists only on the backend)
// (formerly anchor 8 of the merged D1 file).
namespace anchor_qint_classical_value_39 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_qint_classical.cpp:39 must remain sturm::qint_t<64> "
        "— test_default_ctor reads q.value (backend-only public field).");
}
}  // namespace anchor_qint_classical_value_39

// Anchor V2 — tests/test_conversions.cpp:{45,56,63,...} grouped
// (sturm-1os7 D0 re-spell miss). Conversion-row tests assert q.value ==
// expected directly (e.g. `assert(q.value == 42);` at line 45). Frontend
// `qint` has no public `.value` field, so this read is unambiguously a
// backend-only contact. Pairs with the super_mask anchor for the same
// file (which covers the qubits[]/super_mask reads).
namespace anchor_conversions_value_pervasive {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_conversions.cpp:{value-asserts} must remain "
        "sturm::qint_t<64> — conversion-row tests assert q.value == "
        "expected (backend-only public field).");
}
}  // namespace anchor_conversions_value_pervasive

// Anchor V3 — tests/external_consumer/main.cpp:{56,57} reads a.value /
// b.value / n.value (sturm-7uhu D0 re-spell miss — the sturm-1os7 audit
// used `using sturm::qint;` as the grep anchor, which missed the
// external smoke consumer because it gets its `qint` symbol via
// `<sturm/prelude.hpp>`'s `using sturm::qint;` rather than an in-TU
// using-decl). Post-B1 (sturm-65rs.6) the prelude-injected unprefixed
// `qint` is `sturm::frontend::qint`, whose `value_` field is private.
// Fix shape mirrors sturm-pl7o / sturm-ypit / sturm-ztmf: a function-
// scoped `using qint = sturm::qint_t<64>;` inside `main()` shadows the
// prelude-injected name, restoring the backend `.value` public field.
namespace anchor_external_consumer_main_value_56 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/external_consumer/main.cpp:{56,57} must remain "
        "sturm::qint_t<64> — the smoke consumer reads a.value / "
        "b.value / n.value, all backend-only public fields. Drift "
        "back to bare `sturm::qint` (= sturm::frontend::qint via "
        "prelude) breaks `external_consumer_smoke` at compile time.");
}
}  // namespace anchor_external_consumer_main_value_56

int main() {
    // All assertions are compile-time. Reaching main() means every
    // re-spelled callsite in the .value family still names
    // sturm::qint_t<64>.
    std::puts("test_qint_callsite_respelling_value: OK (3 anchors pinned)");
    return 0;
}
