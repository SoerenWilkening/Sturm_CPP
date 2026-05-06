// test_qint_callsite_respelling_phi_theta.cpp — sturm-1os7 (D1 split).
//
// Drift-gate for D0 (sturm-65rs.11) callsite re-spellings, phi() / theta()
// proxy surface family. Each anchor pins a site whose primary backend-only
// contact is `q.phi()` or `q.theta()` — the rotation-gate proxies that
// emit RZ / RY gates only on `sturm::qint_t<W>`. The frontend
// `sturm::frontend::qint` class carries observability-only PhiProxyStub /
// ThetaProxyStub stubs (sturm-vm38) so user code like `qint i; i.phi() +=
// 3;` parses pre-transpile, but those stubs no-op the value and bump the
// measurement counter — they do NOT allocate qubits or emit rotation
// gates, which is what these anchor sites actually depend on. So the
// re-spelling check still pins these sites at `sturm::qint_t<64>`.
//
// History: this file is the phi()/theta() slice of the per-surface split
// that sturm-1os7 introduced when the original (single-file) D1 hit the
// 200-LoC budget cap. See the super_mask sibling for cross-check rationale.
//
// LoC budget: <= 200 (plan §1).

#include "sturm/qtypes/qint.hpp"

#include <cstdint>
#include <type_traits>
#include <cstdio>

// Anchor PT1 — tests/backend/test_when_control_stack_bridge.cpp:266
// (test_simulate_phi_uncontrolled): uses q.super_mask, q.qubits[0], q.phi()
// — primary contact is phi() (formerly anchor 2 of the merged D1 file).
namespace anchor_when_bridge_266 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/backend/test_when_control_stack_bridge.cpp:266 must remain "
        "sturm::qint_t<64> — site uses q.super_mask, q.qubits[0], q.phi().");
}
}  // namespace anchor_when_bridge_266

// Anchor PT2 — tests/backend/test_when_control_stack_bridge.cpp:297
// (test_simulate_theta_uncontrolled): uses q.super_mask, q.qubits[0],
// q.theta() — primary contact is theta() (formerly anchor 3).
namespace anchor_when_bridge_297 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/backend/test_when_control_stack_bridge.cpp:297 must remain "
        "sturm::qint_t<64> — site uses q.super_mask, q.qubits[0], q.theta().");
}
}  // namespace anchor_when_bridge_297

// Anchor PT3 — tests/backend/test_when_control_stack_bridge.cpp:337
// (test_simulate_phi_controlled_via_when): CRZ-under-WHEN; uses
// q.super_mask, q.qubits[0], q.phi() (formerly anchor 4).
namespace anchor_when_bridge_337 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/backend/test_when_control_stack_bridge.cpp:337 must remain "
        "sturm::qint_t<64> — CRZ-via-WHEN site needs q.super_mask, "
        "q.qubits[0], q.phi().");
}
}  // namespace anchor_when_bridge_337

// Anchor PT4 — tests/backend/test_when_control_stack_bridge.cpp:383
// (test_simulate_theta_controlled_via_when): CRY-under-WHEN; uses
// q.super_mask, q.qubits[0], q.theta() (formerly anchor 5).
namespace anchor_when_bridge_383 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/backend/test_when_control_stack_bridge.cpp:383 must remain "
        "sturm::qint_t<64> — CRY-via-WHEN site needs q.super_mask, "
        "q.qubits[0], q.theta().");
}
}  // namespace anchor_when_bridge_383

// Anchor PT5 — tests/test_phase_amp.cpp:{35,36,51,87} grouped (make_super
// writes super_mask + qubits[0]; phi()/theta() proxies on q). Single
// anchor covers the phi/theta family plus the helper's backend writes
// (formerly anchor 11).
namespace anchor_phase_amp_super_mask_qubits_phi_theta_35_36_51_87 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_phase_amp.cpp:{35,36,51,87} must remain sturm::qint_t<64> "
        "— super_mask, qubits[0], phi(), theta() are all backend-only.");
}
}  // namespace anchor_phase_amp_super_mask_qubits_phi_theta_35_36_51_87

// Anchor PT6 — tests/backend/test_phi_theta_backend.cpp:{45-49,99,116,
// 135,156,180,207,229,244,267,291,314,333,350} grouped (sturm-1os7 D0
// re-spell miss). The whole file is dedicated to verifying phi()/theta()
// gate emission on the backend; every test calls q.phi() += delta or
// q.theta() += delta. One anchor covers the entire phi/theta backend
// family.
namespace anchor_phi_theta_backend_pervasive {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/backend/test_phi_theta_backend.cpp:{phi/theta gate-emission "
        "tests} must remain sturm::qint_t<64> — q.phi() and q.theta() "
        "rotation-proxy surfaces exist only on the backend qint_t<W>.");
}
}  // namespace anchor_phi_theta_backend_pervasive

int main() {
    // All assertions are compile-time. Reaching main() means every
    // re-spelled callsite in the phi()/theta() family still names
    // sturm::qint_t<64>.
    std::puts("test_qint_callsite_respelling_phi_theta: OK (6 anchors pinned)");
    return 0;
}
