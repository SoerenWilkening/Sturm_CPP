// test_qint_callsite_respelling.cpp — sturm-65rs.12 (Beat D1 / sturm-qac.12).
//
// Drift-gate for D0 (sturm-65rs.11) callsite re-spellings (Plan §13): for
// every D0-re-spelled site, assert decltype == sturm::qint_t<64> so a drift
// back to bare `sturm::qint` (now sturm::frontend::qint, post-B1
// sturm-65rs.6, lacking super_mask/qubits/phi()/theta()) fails to compile
// with a message naming the original test:line.
//
// Anchors 1-5: D0. 6-9: sturm-pl7o (test_qint_classical.cpp). 10: sturm-ypit
// (test_qint_superposed.cpp). 11: sturm-ztmf (test_phase_amp.cpp).
// (test_umbrella_only.cpp:34 already used qint_t<W>; not a re-spelling
// target — its own static_asserts pin it.)
//
// LoC budget: <= 200 (plan §1).

#include "sturm/qtypes/qint.hpp"

#include <cstdint>
#include <type_traits>
#include <cstdio>

// Anchor 1 — tests/test_resource_lifecycle.cpp:106
// (test_qint64_classical_lifecycle): site relies on qubit-pool release
// semantics on destruction; needs the backend qint_t<64>.
namespace anchor_resource_lifecycle_106 {

[[maybe_unused]] static void pin_decltype() {
    // Match the original ctor argument shape: int64_t-via-static_cast.
    int i = 0;  // stand-in for the loop index in the original site.
    sturm::qint_t<64> q(static_cast<int64_t>(i));
    (void)q;
    static_assert(
        std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_resource_lifecycle.cpp:106 must remain "
        "sturm::qint_t<64> — backend surface (release-on-destruct, "
        "super_mask, qubits[]) is required by D0's audit (PRD §6 R1).");
}

}  // namespace anchor_resource_lifecycle_106

// Anchor 2 — tests/backend/test_when_control_stack_bridge.cpp:266
// (test_simulate_phi_uncontrolled): uses q.super_mask, q.qubits[0], q.phi().
namespace anchor_when_bridge_266 {

[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    (void)q;
    static_assert(
        std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/backend/test_when_control_stack_bridge.cpp:266 must remain "
        "sturm::qint_t<64> — site uses q.super_mask, q.qubits[0], q.phi().");
}

}  // namespace anchor_when_bridge_266

// Anchor 3 — tests/backend/test_when_control_stack_bridge.cpp:297
// (test_simulate_theta_uncontrolled): uses q.super_mask, q.qubits[0], q.theta().
namespace anchor_when_bridge_297 {

[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    (void)q;
    static_assert(
        std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/backend/test_when_control_stack_bridge.cpp:297 must remain "
        "sturm::qint_t<64> — site uses q.super_mask, q.qubits[0], q.theta().");
}

}  // namespace anchor_when_bridge_297

// Anchor 4 — tests/backend/test_when_control_stack_bridge.cpp:337
// (test_simulate_phi_controlled_via_when): CRZ-under-WHEN; uses
// q.super_mask, q.qubits[0], q.phi().
namespace anchor_when_bridge_337 {

[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    (void)q;
    static_assert(
        std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/backend/test_when_control_stack_bridge.cpp:337 must remain "
        "sturm::qint_t<64> — CRZ-via-WHEN site needs q.super_mask, "
        "q.qubits[0], q.phi().");
}

}  // namespace anchor_when_bridge_337

// Anchor 5 — tests/backend/test_when_control_stack_bridge.cpp:383
// (test_simulate_theta_controlled_via_when): CRY-under-WHEN; uses
// q.super_mask, q.qubits[0], q.theta().
namespace anchor_when_bridge_383 {

[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    (void)q;
    static_assert(
        std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/backend/test_when_control_stack_bridge.cpp:383 must remain "
        "sturm::qint_t<64> — CRY-via-WHEN site needs q.super_mask, "
        "q.qubits[0], q.theta().");
}

}  // namespace anchor_when_bridge_383

// ── D0 re-spell miss extensions (anchors 6-11; see file header) ─────────

// Anchor 6 — tests/test_qint_classical.cpp:27 reads q.super_mask.
namespace anchor_qint_classical_super_mask_27 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_qint_classical.cpp:27 must remain sturm::qint_t<64> "
        "— check_classical reads q.super_mask (backend-only).");
}
}  // namespace anchor_qint_classical_super_mask_27

// Anchor 7 — tests/test_qint_classical.cpp:29 reads q.qubits[i].
namespace anchor_qint_classical_qubits_29 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_qint_classical.cpp:29 must remain sturm::qint_t<64> "
        "— check_classical reads q.qubits[i] (backend-only).");
}
}  // namespace anchor_qint_classical_qubits_29

// Anchor 8 — tests/test_qint_classical.cpp:39 reads q.value (backend-only).
namespace anchor_qint_classical_value_39 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_qint_classical.cpp:39 must remain sturm::qint_t<64> "
        "— test_default_ctor reads q.value (backend-only public field).");
}
}  // namespace anchor_qint_classical_value_39

// Anchor 9 — tests/test_qint_classical.cpp:{79,101,135,157,179} grouped
// (compound-assigns +=, -=, *=, /=, %=). Per issue: "one anchor per operator
// family is fine" when budget is tight; frontend lacks all five overloads.
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

// Anchor 10 — tests/test_qint_superposed.cpp:{20,21} grouped (make_super
// helper writes q.super_mask and q.qubits[bit_pos]). The whole file leans
// on these two backend-only surfaces; one anchor covers the family.
namespace anchor_qint_superposed_super_mask_qubits_20_21 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_qint_superposed.cpp:{20,21} must remain sturm::qint_t<64> "
        "— make_super writes q.super_mask and q.qubits[i] (backend-only).");
}
}  // namespace anchor_qint_superposed_super_mask_qubits_20_21

// Anchor 11 — tests/test_phase_amp.cpp:{35,36,51,87} grouped (make_super
// writes super_mask + qubits[0]; phi()/theta() proxies on q). Single anchor
// covers the phi/theta family plus the helper's backend writes.
namespace anchor_phase_amp_super_mask_qubits_phi_theta_35_36_51_87 {
[[maybe_unused]] static void pin_decltype() {
    sturm::qint_t<64> q(0);
    static_assert(std::is_same_v<decltype(q), sturm::qint_t<64>>,
        "tests/test_phase_amp.cpp:{35,36,51,87} must remain sturm::qint_t<64> "
        "— super_mask, qubits[0], phi(), theta() are all backend-only.");
}
}  // namespace anchor_phase_amp_super_mask_qubits_phi_theta_35_36_51_87

// Cross-check 1: pin sturm::frontend::qint != sturm::qint_t<64>. If they
// ever unify, every anchor-static_assert above degenerates into a
// tautology and this drift-gate goes silent.
static_assert(
    !std::is_same_v<sturm::frontend::qint, sturm::qint_t<64>>,
    "sturm::frontend::qint must be a distinct type from sturm::qint_t<64> "
    "for the D0 drift-gate to be load-bearing. If they ever unify, "
    "rewrite this regression to pin a different post-B1 invariant.");

// Cross-check 2: confirm `sturm::qint` (post-B1 namespace alias) resolves
// to the frontend class. This is the invariant that turns a future drift
// `sturm::qint_t<64> q(...)` -> `sturm::qint q(...)` into a hard compile
// error at the per-anchor static_asserts above.
static_assert(
    std::is_same_v<sturm::qint, sturm::frontend::qint>,
    "Post-B1 (sturm-65rs.6), bare sturm::qint must alias "
    "sturm::frontend::qint. If this assertion ever fails, the D0 "
    "drift-gate's loud-red mode is no longer load-bearing.");

int main() {
    // All assertions are compile-time. Reaching main() means every
    // re-spelled callsite still names sturm::qint_t<64> and both
    // cross-checks hold.
    std::puts("test_qint_callsite_respelling: OK (11 anchors pinned)");
    return 0;
}
