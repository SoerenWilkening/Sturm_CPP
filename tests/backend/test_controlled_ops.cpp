// test_controlled_ops.cpp — M23: Hand-written controlled op variants (c_*).
//
// Tests per op, under SIMULATE mode:
//   - ctrl=|0⟩:               target state is unchanged.
//   - ctrl=|1⟩:               result equals the uncontrolled op applied to target.
//   - ctrl=(|0⟩+|1⟩)/√2:     result matches expected superposition amplitudes.
//
// Ops tested:
//   c_quantum_not  — X per target bit → CX(ctrl, bit)
//   c_quantum_xor  — CX(a_bit, tgt_bit) per bit → CCX(ctrl, a_bit, tgt_bit)
//   c_quantum_add  — stub: verifies gate emission via APPEND mode, not wavefunction.
//
// Harness: plain assert + main (no gtest).

#include "sturm/ops/c_quantum_not.hpp"
#include "sturm/ops/c_quantum_xor.hpp"
#include "sturm/ops/c_quantum_add.hpp"

#include "sturm/backend/exec_simulate.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/core/gate_kind.h"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <vector>

// ── Helpers ───────────────────────────────────────────────────────────────────

static constexpr double kTol = 1e-10;

using cx = std::complex<double>;

static void assert_close(cx got, cx expected, const char* label) {
    double err = std::abs(got - expected);
    if (err > kTol) {
        std::printf("FAIL %s: got (%g,%g) expected (%g,%g) err=%g\n",
                    label,
                    got.real(), got.imag(),
                    expected.real(), expected.imag(), err);
        assert(false);
    }
}

static cx amp(const orkan::state_t& s, uint64_t idx) {
    return orkan::amplitude(s, idx);
}

// ── Scoped SIMULATE context helper ───────────────────────────────────────────

struct ScopedSimCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    sturm::OrkanBridge*      bridge;

    ScopedSimCtx() {
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, 17u);
        assert(ctx && "sturm_backend_create failed");
        // Install Orkan bridge so execute_gate can reach the statevector.
        // execute_gate.cpp casts orkan_state_ptr to OrkanBridge*.
        bridge = new sturm::OrkanBridge();
        bridge->allocate(sturm::kMaxQubits);
        ctx->orkan_state_ptr = bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedSimCtx() {
        sturm_set_thread_context(prev);
        ctx->orkan_state_ptr = nullptr;
        delete bridge;
        sturm_backend_destroy(ctx);
    }

    // Reset statevector to |0…0⟩.
    void reset() {
        bridge->reset_zero();
    }

    // Apply a gate directly (bypass execute_gate) so we can set up test states.
    void apply_1q(sturm_gate_kind_t kind, uint32_t q, double p = 0.0) {
        sturm::exec_simulate_1q(bridge->state(), kind, q, p);
    }
    void apply_multiq(sturm_gate_kind_t kind, uint32_t q0, uint32_t q1,
                      uint32_t q2 = 0u, double p = 0.0) {
        sturm::exec_simulate_multiq(bridge->state(), kind, q0, q1, q2, p);
    }

    cx amp_at(uint64_t idx) const {
        return orkan::amplitude(bridge->state(), idx);
    }
};

// ── Scoped APPEND context helper ──────────────────────────────────────────────

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND, 17u);
        assert(ctx && "sturm_backend_create failed");
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedAppendCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }

    sturm::GateIR& ir() { return ctx->ir; }
};

// =============================================================================
// c_quantum_not tests
// =============================================================================
//
// c_quantum_not(ctrl_qubit, target_qubits, n):
//   For each bit in target_qubits, emits CX(ctrl, bit).
//   This is the controlled-NOT: if ctrl=|1⟩, every target bit is flipped.
//
// Register layout for 2-qubit test:
//   qubit 0 = ctrl
//   qubit 1 = target bit 0
//   (one-bit register so index space is 2^2 = 4)
//
// Basis states:
//   |ctrl=0, tgt=0⟩ = idx 0  (binary: 00)
//   |ctrl=0, tgt=1⟩ = idx 2  (binary: 10, qubit1 is bit 1)
//   |ctrl=1, tgt=0⟩ = idx 1  (binary: 01, qubit0 is bit 0)
//   |ctrl=1, tgt=1⟩ = idx 3  (binary: 11)

// ctrl=|0⟩, tgt=|0⟩  → c_quantum_not does nothing → state still |ctrl=0,tgt=0⟩
static void test_c_not_ctrl0_unchanged() {
    ScopedSimCtx sc;
    sc.reset();
    // State is |00⟩ (ctrl=0, tgt=0)
    // Apply c_quantum_not(ctrl=qubit0, target=qubit1)
    uint32_t ctrl = 0u;
    uint32_t tgt[1] = {1u};
    sturm::c_quantum_not(*sturm_get_thread_context(), ctrl, tgt, 1u);
    // ctrl=0: CX never fires. State unchanged.
    assert_close(sc.amp_at(0), {1.0, 0.0}, "c_not ctrl0 amp[00]");
    assert_close(sc.amp_at(1), {0.0, 0.0}, "c_not ctrl0 amp[01]");
    assert_close(sc.amp_at(2), {0.0, 0.0}, "c_not ctrl0 amp[10]");
    assert_close(sc.amp_at(3), {0.0, 0.0}, "c_not ctrl0 amp[11]");
    std::printf("PASS test_c_not_ctrl0_unchanged\n");
}

// ctrl=|1⟩, tgt=|0⟩  → c_quantum_not flips tgt → |ctrl=1,tgt=1⟩
static void test_c_not_ctrl1_flips() {
    ScopedSimCtx sc;
    sc.reset();
    // Prepare ctrl=|1⟩ by applying X to qubit 0.
    sc.apply_1q(STURM_GATE_X, 0u);
    // State is now |ctrl=1, tgt=0⟩ = idx 1 (bit0=1, bit1=0 → 0b01=1)
    assert_close(sc.amp_at(1), {1.0, 0.0}, "c_not setup ctrl1");

    uint32_t ctrl = 0u;
    uint32_t tgt[1] = {1u};
    sturm::c_quantum_not(*sturm_get_thread_context(), ctrl, tgt, 1u);
    // CX(ctrl=0, tgt=1): |01⟩ → |11⟩ (flip qubit1).
    // idx of |ctrl=1,tgt=1⟩ = 0b11 = 3.
    assert_close(sc.amp_at(0), {0.0, 0.0}, "c_not ctrl1 amp[00]");
    assert_close(sc.amp_at(1), {0.0, 0.0}, "c_not ctrl1 amp[01]");
    assert_close(sc.amp_at(2), {0.0, 0.0}, "c_not ctrl1 amp[10]");
    assert_close(sc.amp_at(3), {1.0, 0.0}, "c_not ctrl1 amp[11]");
    std::printf("PASS test_c_not_ctrl1_flips\n");
}

// ctrl=H|0⟩=(|0⟩+|1⟩)/√2, tgt=|0⟩ → superposition: (|ctrl=0,tgt=0⟩+|ctrl=1,tgt=1⟩)/√2
static void test_c_not_ctrl_super() {
    ScopedSimCtx sc;
    sc.reset();
    // Apply H to qubit 0 (ctrl becomes (|0⟩+|1⟩)/√2).
    sc.apply_1q(STURM_GATE_H, 0u);
    // State: (|00⟩ + |01⟩)/√2  (qubit0=ctrl is LSB, so |01⟩=idx1 means ctrl=1,tgt=0)
    double s = 1.0 / std::sqrt(2.0);
    assert_close(sc.amp_at(0), {s, 0.0}, "c_not super setup amp[00]");
    assert_close(sc.amp_at(1), {s, 0.0}, "c_not super setup amp[01]");

    uint32_t ctrl = 0u;
    uint32_t tgt[1] = {1u};
    sturm::c_quantum_not(*sturm_get_thread_context(), ctrl, tgt, 1u);
    // CX(0,1): |00⟩→|00⟩, |01⟩→|11⟩
    // Result: (|00⟩ + |11⟩)/√2
    assert_close(sc.amp_at(0), {s,   0.0}, "c_not super amp[00]");
    assert_close(sc.amp_at(1), {0.0, 0.0}, "c_not super amp[01]");
    assert_close(sc.amp_at(2), {0.0, 0.0}, "c_not super amp[10]");
    assert_close(sc.amp_at(3), {s,   0.0}, "c_not super amp[11]");
    std::printf("PASS test_c_not_ctrl_super\n");
}

// =============================================================================
// c_quantum_xor tests
// =============================================================================
//
// c_quantum_xor(ctrl_qubit, a_qubits, b_qubits, n):
//   Controlled XOR: for each bit i, if ctrl=|1⟩ then b[i] ^= a[i].
//   Implemented as CCX(ctrl, a[i], b[i]) per bit.
//
// Register layout for 1-bit test:
//   qubit 0 = ctrl
//   qubit 1 = a[0]  (input operand)
//   qubit 2 = b[0]  (result/output)
//   (3 qubits → 2^3 = 8 basis states)
//
// Basis index: bit0=ctrl, bit1=a, bit2=b → idx = ctrl + 2*a + 4*b.

// ctrl=|0⟩: b unchanged regardless of a.
static void test_c_xor_ctrl0_unchanged() {
    ScopedSimCtx sc;
    sc.reset();
    // Set a=|1⟩ so that if ctrl fires b would change.
    sc.apply_1q(STURM_GATE_X, 1u); // a=1
    // State: |ctrl=0, a=1, b=0⟩ = idx 0b010 = 2
    assert_close(sc.amp_at(2), {1.0, 0.0}, "c_xor ctrl0 setup");

    uint32_t ctrl = 0u;
    uint32_t a[1] = {1u};
    uint32_t b[1] = {2u};
    sturm::c_quantum_xor(*sturm_get_thread_context(), ctrl, a, b, 1u);
    // ctrl=0: CCX never fires. b stays 0.
    // State still |ctrl=0, a=1, b=0⟩ = idx 2
    assert_close(sc.amp_at(2), {1.0, 0.0}, "c_xor ctrl0 b_unchanged");
    assert_close(sc.amp_at(6), {0.0, 0.0}, "c_xor ctrl0 b_not_flipped");
    std::printf("PASS test_c_xor_ctrl0_unchanged\n");
}

// ctrl=|1⟩, a=|1⟩, b=|0⟩ → b becomes |1⟩ (XOR of 1 and 0).
static void test_c_xor_ctrl1_applies_xor() {
    ScopedSimCtx sc;
    sc.reset();
    sc.apply_1q(STURM_GATE_X, 0u); // ctrl=1
    sc.apply_1q(STURM_GATE_X, 1u); // a=1
    // State: |ctrl=1, a=1, b=0⟩ = idx 0b011 = 3

    uint32_t ctrl = 0u;
    uint32_t a[1] = {1u};
    uint32_t b[1] = {2u};
    sturm::c_quantum_xor(*sturm_get_thread_context(), ctrl, a, b, 1u);
    // CCX(ctrl=0, a=1, b=2): ctrl=1 and a=1 → b flipped → b=1
    // State: |ctrl=1, a=1, b=1⟩ = idx 0b111 = 7
    assert_close(sc.amp_at(7), {1.0, 0.0}, "c_xor ctrl1 result");
    assert_close(sc.amp_at(3), {0.0, 0.0}, "c_xor ctrl1 original");
    std::printf("PASS test_c_xor_ctrl1_applies_xor\n");
}

// ctrl=(|0⟩+|1⟩)/√2, a=|1⟩, b=|0⟩ →
//   superposition: (|ctrl=0,a=1,b=0⟩ + |ctrl=1,a=1,b=1⟩)/√2
static void test_c_xor_ctrl_super() {
    ScopedSimCtx sc;
    sc.reset();
    sc.apply_1q(STURM_GATE_H, 0u); // ctrl = (|0⟩+|1⟩)/√2
    sc.apply_1q(STURM_GATE_X, 1u); // a = |1⟩

    // State before: (|ctrl=0,a=1,b=0⟩ + |ctrl=1,a=1,b=0⟩)/√2
    //               = (|010⟩ + |011⟩)/√2  (idx 2 and 3)
    double s = 1.0 / std::sqrt(2.0);
    assert_close(sc.amp_at(2), {s, 0.0}, "c_xor super setup 2");
    assert_close(sc.amp_at(3), {s, 0.0}, "c_xor super setup 3");

    uint32_t ctrl = 0u;
    uint32_t a[1] = {1u};
    uint32_t b[1] = {2u};
    sturm::c_quantum_xor(*sturm_get_thread_context(), ctrl, a, b, 1u);
    // CCX(0,1,2): |010⟩ stays (ctrl=0), |011⟩ → |111⟩ (ctrl=1, a=1 → flip b)
    // Result: (|010⟩ + |111⟩)/√2  (idx 2 and 7)
    assert_close(sc.amp_at(2), {s,   0.0}, "c_xor super amp[010]");
    assert_close(sc.amp_at(3), {0.0, 0.0}, "c_xor super amp[011]");
    assert_close(sc.amp_at(7), {s,   0.0}, "c_xor super amp[111]");
    std::printf("PASS test_c_xor_ctrl_super\n");
}

// =============================================================================
// c_quantum_add tests (APPEND mode — verifies gate emission, not wavefunction)
// =============================================================================
//
// c_quantum_add is a stub (TODO(backend)) that emits placeholder gates into the
// IR.  Tests verify:
//   - ctrl=classical 0: no gates emitted (the caller short-circuits).
//   - ctrl=classical 1: the op emits the same stub sequence as c_quantum_add_stub.
//   - The gate count under ctrl=1 is >= gate count of uncontrolled quantum_add.
//
// These tests use APPEND mode so they are pure IR-inspection tests (no Orkan).

static void test_c_add_ctrl0_no_gates() {
    ScopedAppendCtx sc;
    size_t before = sc.ir().size();

    // ctrl=0: per PRD §7 mixed dispatch, classical control 0 → gate skipped.
    // c_quantum_add itself checks: if ctrl_val == 0, emit nothing.
    uint32_t a[4] = {0u, 1u, 2u, 3u};
    uint32_t out[4] = {4u, 5u, 6u, 7u};
    // Call the c_* variant with ctrl_val=0 (classical zero).
    sturm::c_quantum_add_stub(*sturm_get_thread_context(),
                               /*ctrl_qubit=*/8u, /*ctrl_val=*/0,
                               a, out, 4u);
    size_t after = sc.ir().size();
    assert(after == before && "c_quantum_add ctrl=0 should emit no gates");
    std::printf("PASS test_c_add_ctrl0_no_gates\n");
}

static void test_c_add_ctrl1_emits_gates() {
    ScopedAppendCtx sc;
    size_t before = sc.ir().size();

    uint32_t a[4] = {0u, 1u, 2u, 3u};
    uint32_t out[4] = {4u, 5u, 6u, 7u};
    // Call the c_* variant with ctrl_val=1 (classical one → gates fire).
    sturm::c_quantum_add_stub(*sturm_get_thread_context(),
                               /*ctrl_qubit=*/8u, /*ctrl_val=*/1,
                               a, out, 4u);
    size_t after = sc.ir().size();
    assert(after > before && "c_quantum_add ctrl=1 must emit at least one gate");
    std::printf("PASS test_c_add_ctrl1_emits_gates\n");
}

static void test_c_add_ctrl1_more_gates_than_uncontrolled() {
    ScopedAppendCtx sc;

    // Uncontrolled: emit 1 CX per bit (add_stub emits CX per output bit).
    uint32_t a[2] = {0u, 1u};
    uint32_t out[2] = {2u, 3u};
    size_t before_unc = sc.ir().size();
    sturm::quantum_add_stub(*sturm_get_thread_context(), a, out, 2u);
    size_t unc_gates = sc.ir().size() - before_unc;

    // Controlled: same 2-bit op.
    size_t before_c = sc.ir().size();
    sturm::c_quantum_add_stub(*sturm_get_thread_context(),
                               /*ctrl_qubit=*/4u, /*ctrl_val=*/1,
                               a, out, 2u);
    size_t c_gates = sc.ir().size() - before_c;

    assert(c_gates >= unc_gates &&
           "controlled add must emit at least as many gates as uncontrolled");
    std::printf("PASS test_c_add_ctrl1_more_gates_than_uncontrolled "
                "(unc=%zu c=%zu)\n", unc_gates, c_gates);
}

// =============================================================================
// main
// =============================================================================

int main() {
    // c_quantum_not
    test_c_not_ctrl0_unchanged();
    test_c_not_ctrl1_flips();
    test_c_not_ctrl_super();

    // c_quantum_xor
    test_c_xor_ctrl0_unchanged();
    test_c_xor_ctrl1_applies_xor();
    test_c_xor_ctrl_super();

    // c_quantum_add (APPEND mode / IR inspection)
    test_c_add_ctrl0_no_gates();
    test_c_add_ctrl1_emits_gates();
    test_c_add_ctrl1_more_gates_than_uncontrolled();

    std::printf("All M23 controlled-op tests passed.\n");
    return 0;
}
