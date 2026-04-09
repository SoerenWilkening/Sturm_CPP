// test_garbage_uncompute.cpp — M5 (PRD v2): GarbageManager full round-trip.
//
// Tests:
//   - track() registers displaced register + uncompute function.
//   - flush() runs uncompute functions in reverse order, then frees ancillas.
//   - After flush() ancilla pool is clean (mgr.num_in_use() == 0).
//   - Multiple entries: LIFO order verified.
//   - Empty uncompute_fn (nullptr): track a trivially-zero register;
//     flush should still call free_ancilla correctly.
//   - pending_count() and total_pending_qubits() diagnostics.
//
// Scenario:
//   Setup: 6 qubits total.
//     q0 = "result" (stays with caller, not tracked).
//     q1 = displaced ancilla 1 — XOR'd with q0, then uncomputed.
//     q2 = displaced ancilla 2 — pristine |0⟩ (no uncompute needed).
//     q3..q5 = ancilla slot for manager.
//
//   The uncompute_fn for q1 applies XOR(q0, q1) again (self-inverse) to
//   restore q1 to |0⟩ before free_ancilla is called.
//
// Harness: plain assert + printf (no gtest).

#include "sturm/lib/garbage.hpp"
#include "sturm/lib/move.hpp"
#include "sturm/backend/state.hpp"
#include "sturm/backend/ancilla.hpp"
#include "sturm/backend/primitives.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>
#include <vector>

static constexpr double kTol = 1e-9;

static bool is_qubit_zero(const sturm::v2::SimState& s, uint32_t q) {
    uint64_t dim  = uint64_t{1} << s.num_qubits();
    uint64_t mask = uint64_t{1} << q;
    for (uint64_t i = 0; i < dim; ++i) {
        if ((i & mask) != 0u) {
            if (std::abs(s.amplitude(i)) > kTol) return false;
        }
    }
    return true;
}

// ── test_garbage_empty_uncompute_fn ───────────────────────────────────────────
//
// Track a qubit that is already |0⟩ with no uncompute_fn.
// flush() must free it without error.
static void test_garbage_empty_uncompute_fn() {
    sturm::v2::SimState s;
    s.allocate(4);
    s.load_basis(0u);  // all |0⟩

    // q0..q2 are "user" qubits; q3 is the ancilla the manager will issue.
    sturm::v2::AncillaManager mgr(s, 0u);  // all qubits available

    // Allocate q0 and immediately claim it as a displaced qubit (it's |0⟩).
    uint32_t q0 = mgr.allocate_ancilla();
    assert(q0 == 0u);

    sturm::v2::GarbageManager gc(s, mgr);

    // Track with no uncompute.
    uint32_t idxs[] = {q0};
    gc.track(idxs, 1u, nullptr);

    assert(gc.pending_count() == 1u);
    assert(gc.total_pending_qubits() == 1u);

    gc.flush();

    assert(gc.pending_count() == 0u);
    assert(mgr.num_in_use() == 0u);
    std::puts("  PASS: GarbageManager empty uncompute_fn (trivially |0>)");
}

// ── test_garbage_xor_round_trip ───────────────────────────────────────────────
//
// Full round-trip:
//   1. Allocate q0 and q1 as "result" and "displaced" ancillas.
//   2. Set q0=|1⟩ and XOR q0 into q1 (q1 becomes |1⟩ = entangled with q0).
//   3. Track q1 with an uncompute_fn = XOR(q0, q1) (self-inverse).
//   4. flush(): uncompute_fn fires, q1 → |0⟩; then free_ancilla(q1).
//   5. Verify mgr.num_in_use() == 1 (q0 still allocated), q1 is |0⟩.
static void test_garbage_xor_round_trip() {
    sturm::v2::SimState s;
    s.allocate(4);
    s.load_basis(0u);

    sturm::v2::AncillaManager mgr(s, 0u);

    uint32_t q0 = mgr.allocate_ancilla();  // q0
    uint32_t q1 = mgr.allocate_ancilla();  // q1

    // Prepare q0 = |1⟩
    sturm::v2::primitive_X(s, q0);

    // XOR q0 into q1: q1 = |1⟩ (entangled with q0)
    sturm::v2::primitive_XOR(s, q0, q1);

    // q1 is now |1⟩ — not clean.
    assert(!is_qubit_zero(s, q1) && "q1 should be |1> now");

    sturm::v2::GarbageManager gc(s, mgr);

    // Track q1 with its uncompute: XOR(q0, q1) again restores q1 → |0⟩.
    // Capture q0 and q1 by value; capture s by reference.
    uint32_t idxs[] = {q1};
    gc.track(idxs, 1u, [&s, q0, q1]() {
        sturm::v2::primitive_XOR(s, q0, q1);
    });

    assert(gc.pending_count() == 1u);
    assert(gc.total_pending_qubits() == 1u);
    assert(mgr.num_in_use() == 2u);  // q0 and q1 still allocated

    // Flush: uncompute_fn fires, q1 → |0⟩; then free_ancilla(q1).
    gc.flush();

    assert(gc.pending_count() == 0u);
    assert(mgr.num_in_use() == 1u);  // only q0 remains
    assert(is_qubit_zero(s, q1) && "q1 must be |0> after uncompute");
    std::puts("  PASS: GarbageManager XOR round-trip — ancilla clean after flush");
}

// ── test_garbage_lifo_order ────────────────────────────────────────────────────
//
// Verify LIFO uncompute order: register two entries, track the order in which
// their uncompute_fn's are called, confirm it is reverse-registration order.
static void test_garbage_lifo_order() {
    sturm::v2::SimState s;
    s.allocate(4);
    s.load_basis(0u);

    sturm::v2::AncillaManager mgr(s, 0u);

    uint32_t q0 = mgr.allocate_ancilla();  // ancilla 0
    uint32_t q1 = mgr.allocate_ancilla();  // ancilla 1

    sturm::v2::GarbageManager gc(s, mgr);

    // Both qubits are |0⟩ initially, so we don't need real uncompute logic
    // — just track call order.
    std::vector<int> order;

    uint32_t idxs0[] = {q0};
    uint32_t idxs1[] = {q1};
    gc.track(idxs0, 1u, [&order]() { order.push_back(0); });
    gc.track(idxs1, 1u, [&order]() { order.push_back(1); });

    assert(gc.pending_count() == 2u);
    assert(gc.total_pending_qubits() == 2u);

    // Both q0 and q1 are |0⟩ so free_ancilla won't throw even though the
    // "uncompute" functions only record the call order.
    gc.flush();

    // LIFO order: entry 1 (registered second) should be flushed first.
    assert(order.size() == 2u);
    assert(order[0] == 1 && "LIFO: second entry must uncompute first");
    assert(order[1] == 0 && "LIFO: first entry must uncompute last");
    assert(gc.pending_count() == 0u);
    assert(mgr.num_in_use() == 0u);
    std::puts("  PASS: GarbageManager LIFO uncompute order");
}

// ── test_garbage_multi_qubit_entry ────────────────────────────────────────────
//
// Track a 2-qubit displaced register with an uncompute_fn.
// After flush(), both qubits must be |0⟩ and mgr clean.
static void test_garbage_multi_qubit_entry() {
    sturm::v2::SimState s;
    s.allocate(6);
    s.load_basis(0u);

    sturm::v2::AncillaManager mgr(s, 0u);

    uint32_t q0 = mgr.allocate_ancilla();  // "result" qubit
    uint32_t q1 = mgr.allocate_ancilla();  // displaced reg bit 0
    uint32_t q2 = mgr.allocate_ancilla();  // displaced reg bit 1

    // Set q0 = |1⟩
    sturm::v2::primitive_X(s, q0);

    // XOR q0 into q1 and q2 so they carry entanglement.
    sturm::v2::primitive_XOR(s, q0, q1);
    sturm::v2::primitive_XOR(s, q0, q2);

    assert(!is_qubit_zero(s, q1));
    assert(!is_qubit_zero(s, q2));

    sturm::v2::GarbageManager gc(s, mgr);

    uint32_t idxs[] = {q1, q2};
    gc.track(idxs, 2u, [&s, q0, q1, q2]() {
        // Uncompute: XOR q0 back out of both displaced qubits.
        sturm::v2::primitive_XOR(s, q0, q1);
        sturm::v2::primitive_XOR(s, q0, q2);
    });

    assert(gc.total_pending_qubits() == 2u);

    gc.flush();

    assert(gc.pending_count() == 0u);
    assert(mgr.num_in_use() == 1u);  // q0 still allocated
    assert(is_qubit_zero(s, q1));
    assert(is_qubit_zero(s, q2));
    std::puts("  PASS: GarbageManager multi-qubit entry round-trip");
}

// ── test_garbage_diagnostics ──────────────────────────────────────────────────
//
// Verify pending_count() and total_pending_qubits() before and after flush.
static void test_garbage_diagnostics() {
    sturm::v2::SimState s;
    s.allocate(4);
    s.load_basis(0u);

    sturm::v2::AncillaManager mgr(s, 0u);

    uint32_t q0 = mgr.allocate_ancilla();
    uint32_t q1 = mgr.allocate_ancilla();
    uint32_t q2 = mgr.allocate_ancilla();

    sturm::v2::GarbageManager gc(s, mgr);

    uint32_t a1[] = {q0};
    uint32_t a2[] = {q1, q2};

    gc.track(a1, 1u, nullptr);
    assert(gc.pending_count() == 1u);
    assert(gc.total_pending_qubits() == 1u);

    gc.track(a2, 2u, nullptr);
    assert(gc.pending_count() == 2u);
    assert(gc.total_pending_qubits() == 3u);

    gc.flush();
    assert(gc.pending_count() == 0u);
    assert(gc.total_pending_qubits() == 0u);
    assert(mgr.num_in_use() == 0u);
    std::puts("  PASS: GarbageManager diagnostics correct before/after flush");
}

int main() {
    std::puts("=== test_garbage_uncompute ===");
    test_garbage_empty_uncompute_fn();
    test_garbage_xor_round_trip();
    test_garbage_lifo_order();
    test_garbage_multi_qubit_entry();
    test_garbage_diagnostics();
    std::puts("ALL PASS");
    return 0;
}
