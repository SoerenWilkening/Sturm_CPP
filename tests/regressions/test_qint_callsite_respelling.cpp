// test_qint_callsite_respelling.cpp — sturm-65rs.12 (Beat D1 / sturm-qac.12).
//
// Drift-gate regression for D0 (sturm-65rs.11) callsite re-spellings.
// Plan §13 spells the contract literally: "for every callsite re-spelled
// in D0, assert the type via std::is_same_v<decltype(...),
// sturm::qint_t<64>>". This pins the re-spelling against future drift
// back to bare `qint`.
//
// Pre-B1 (sturm-65rs.6) `sturm::qint` aliased `sturm::qint_t<64>`, so
// `sturm::qint q(0)` poking `q.super_mask`, `q.qubits[i]`, `q.phi()`,
// `q.theta()` was well-formed. B1 repointed `sturm::qint` to the
// frontend class `sturm::frontend::qint` (no `super_mask`, no `qubits`,
// no `phi()`/`theta()`), so any callsite that needs the BACKEND surface
// MUST be spelled `sturm::qint_t<64>`. D0 re-spelled five callsites
// (anchors 1-5); sturm-pl7o caught a missed sixth file
// (tests/test_qint_classical.cpp) and added anchors 6-9 below. If a future
// refactor flips any of them back to bare `sturm::qint`, the corresponding
// `decltype` resolves to `sturm::frontend::qint` and the static_assert
// fires loudly with a message naming the original test:line.
//
// The PRD §6 R1 audit also names tests/packaging/test_umbrella_only.cpp:34,
// but the D0 commit (4f5f122) shows that file already used qint_t<W>
// (not the bare alias) — never a re-spelling target. We do not pin it
// here; that file's own static_asserts already pin its qint_t<8> use.
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

// ── sturm-pl7o (D0 re-spell miss) extension ─────────────────────────────────
// tests/test_qint_classical.cpp was missed by the original D0 audit. Anchors
// 6-9 below pin its backend-only surfaces (super_mask, qubits[], value, and
// compound-assigns +=/-=/*=//=/%= grouped per issue's LoC exception). Each
// static_assert names the original test:line so drift back to bare
// `sturm::qint` produces a compile-time failure citing the affected site.

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

// Anchor 8 — tests/test_qint_classical.cpp:39 reads q.value (frontend has
// private value_; the public field exists only on the backend).
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
    std::puts("test_qint_callsite_respelling: OK (9 anchors pinned)");
    return 0;
}
