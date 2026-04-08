// test_execute_gate.cpp — M13: Layer B — execute_gate (TDD, written before implementation).
//
// Tests:
//   1. COUNT_ONLY: N calls → gate_count == N; SIMULATE: same; APPEND: same.
//   2. APPEND: N calls records exactly N entries in ctx.ir.
//   3. SIMULATE: correct final state after H then X on qubit 0 (|0>→|+>→...)
//      Specifically: H|0> = (|0>+|1>)/√2; then X on same qubit → (|1>+|0>)/√2
//      i.e. same superposition, order swapped → amplitudes equal to 1/√2 everywhere.
//   4. CRx/CRy/CRz: under APPEND, records as the original single gate kind (not decomposed).
//      Under SIMULATE, produces the correct output (decomposed, verified vs reference).
//
// The C ABI entry point sturm_execute_gate is also called directly to verify
// that it correctly forwards to the C++ helper and increments ctx.gate_count.
//
// Harness: plain assert + main (no gtest).

#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/gate_kind.h"
#include "sturm/backend/ir.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/backend/exec_simulate.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>

static constexpr double kTol = 1e-10;
using cx = std::complex<double>;

// ── Helpers ───────────────────────────────────────────────────────────────────

static void assert_close(cx got, cx expected, const char* label) {
    double err = std::abs(got - expected);
    if (err > kTol) {
        std::fprintf(stderr, "FAIL %s: got (%g,%g) expected (%g,%g) err=%g\n",
                    label,
                    got.real(), got.imag(),
                    expected.real(), expected.imag(),
                    err);
        assert(false);
    }
}

// ── Fixture: scoped context installer ────────────────────────────────────────

struct ScopedCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedCtx(sturm_mode_t mode) {
        ctx  = sturm_backend_create(mode, 17u);
        assert(ctx);
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
};

// ── Test 1: gate_count == N after N calls in COUNT_ONLY mode ─────────────────

static void test_count_only_counter() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    assert(sturm_gate_count(sc.ctx) == 0u);

    const uint32_t q[1] = {0u};
    for (int i = 0; i < 100; ++i) {
        sturm_execute_gate(STURM_GATE_X, q, 1u, 0.0);
    }
    assert(sturm_gate_count(sc.ctx) == 100u);
    std::printf("  COUNT_ONLY counter: PASS\n");
}

// ── Test 2: gate_count == N after N calls in APPEND mode ─────────────────────

static void test_append_counter() {
    ScopedCtx sc{STURM_MODE_APPEND};
    assert(sturm_gate_count(sc.ctx) == 0u);

    const uint32_t q[2] = {0u, 1u};
    const int N = 50;
    for (int i = 0; i < N; ++i) {
        sturm_execute_gate(STURM_GATE_CX, q, 2u, 0.0);
    }
    assert(sturm_gate_count(sc.ctx) == (uint64_t)N);
    std::printf("  APPEND counter: PASS\n");
}

// ── Test 3: APPEND records exactly N entries in ctx.ir ───────────────────────

static void test_append_records_n_entries() {
    ScopedCtx sc{STURM_MODE_APPEND};

    const uint32_t q[1] = {0u};
    const int N = 77;
    for (int i = 0; i < N; ++i) {
        sturm_execute_gate(STURM_GATE_H, q, 1u, 0.0);
    }
    assert(sc.ctx->ir.size() == (size_t)N);
    // Verify each entry has the correct kind and qubit
    for (size_t i = 0; i < (size_t)N; ++i) {
        const sturm::GateRecord& r = sc.ctx->ir.at(i);
        assert(r.kind      == STURM_GATE_H);
        assert(r.qubits[0] == 0u);
        assert(r.n         == 1u);
    }
    std::printf("  APPEND records N entries: PASS\n");
}

// ── Test 4: gate_count == N after N calls in SIMULATE mode ───────────────────

static void test_simulate_counter() {
    // We need an OrkanBridge to back the context's simulate mode.
    // The context must have a bridge allocated. Create one manually.
    sturm::OrkanBridge bridge;
    bridge.allocate(4u);

    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_SIMULATE, 17u);
    assert(ctx);
    // Wire the bridge into the context.
    ctx->orkan_state_ptr = &bridge;

    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    const uint32_t q[1] = {0u};
    const int N = 30;
    for (int i = 0; i < N; ++i) {
        // X twice = identity, keeps state clean
        sturm_execute_gate(STURM_GATE_X, q, 1u, 0.0);
        sturm_execute_gate(STURM_GATE_X, q, 1u, 0.0);
    }
    assert(sturm_gate_count(ctx) == (uint64_t)(N * 2));

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    std::printf("  SIMULATE counter: PASS\n");
}

// ── Test 5: SIMULATE produces correct final state — H then X ─────────────────
//
// Start |0>.
// H|0> = (|0>+|1>)/√2  →  amplitude[0] = amplitude[1] = 1/√2.
// X(|0>+|1>)/√2 = (X|0>+X|1>)/√2 = (|1>+|0>)/√2
//   → amplitude[0] = amplitude[1] = 1/√2 (same magnitudes, identical state).

static void test_simulate_h_then_x() {
    sturm::OrkanBridge bridge;
    bridge.allocate(2u);

    sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_SIMULATE, 17u);
    assert(ctx);
    ctx->orkan_state_ptr = &bridge;

    sturm_backend_context_t* prev = sturm_get_thread_context();
    sturm_set_thread_context(ctx);

    const uint32_t q0[1] = {0u};
    sturm_execute_gate(STURM_GATE_H, q0, 1u, 0.0);  // H|0> = (|0>+|1>)/√2
    sturm_execute_gate(STURM_GATE_X, q0, 1u, 0.0);  // X on same qubit

    // After H: amp[0]=1/√2, amp[1]=1/√2  (qubit 1 is |0>)
    // So in 2-qubit register: amp[00]=1/√2, amp[10]=1/√2 (qubit0 varies)
    // After X on qubit 0: amp[01]=1/√2, amp[11]=1/√2
    // Basis: idx = qubit0_bit | (qubit1_bit << 1)
    // After H on q0: idx=0 (|00>) = 1/√2, idx=1 (|10>) = 1/√2 (WAIT - need to verify orkan qubit convention)
    // Use bridge to get actual amps
    double inv_sqrt2 = 1.0 / std::sqrt(2.0);

    // Verify gate_count = 2
    assert(sturm_gate_count(ctx) == 2u);

    // Check amplitudes: after H(q0) then X(q0):
    // |psi> = X(q0) * H(q0) |00>
    //       = X(q0) * (|00>+|10>)/sqrt(2)   (LSB convention: q0 is bit 0)
    //       = (|10>+|00>)/sqrt(2)
    // same state — so amp at idx 0 and idx 1 (qubit0=0,q1=0 and qubit0=1,q1=0)
    // idx: qubit0 = bit 0, qubit1 = bit 1
    //   idx 0 = |q1=0,q0=0> = |00>: amp = 1/√2
    //   idx 1 = |q1=0,q0=1> = |10>: amp = 1/√2
    //   idx 2 = |q1=1,q0=0> = |01>: amp = 0
    //   idx 3 = |q1=1,q0=1> = |11>: amp = 0

    cx a0 = orkan::amplitude(bridge.state(), 0);
    cx a1 = orkan::amplitude(bridge.state(), 1);
    cx a2 = orkan::amplitude(bridge.state(), 2);
    cx a3 = orkan::amplitude(bridge.state(), 3);

    assert_close(a0, cx{inv_sqrt2, 0.0}, "H+X: amp[0]");
    assert_close(a1, cx{inv_sqrt2, 0.0}, "H+X: amp[1]");
    assert_close(a2, cx{0.0, 0.0},       "H+X: amp[2]");
    assert_close(a3, cx{0.0, 0.0},       "H+X: amp[3]");

    sturm_set_thread_context(prev);
    sturm_backend_destroy(ctx);
    std::printf("  SIMULATE H+X final state: PASS\n");
}

// ── Test 6: CRx/CRy/CRz under APPEND — recorded as single entry (no decomp) ──

static void test_crot_append_single_entry() {
    const sturm_gate_kind_t crot_kinds[] = {
        STURM_GATE_CRX, STURM_GATE_CRY, STURM_GATE_CRZ
    };
    const char* names[] = {"CRx", "CRy", "CRz"};
    const uint32_t q[2] = {0u, 1u};
    const double theta  = 1.5707963267948966; // pi/2

    for (int k = 0; k < 3; ++k) {
        ScopedCtx sc{STURM_MODE_APPEND};
        sturm_execute_gate(crot_kinds[k], q, 2u, theta);

        // Must be exactly 1 entry — no decomposition in APPEND mode.
        assert(sc.ctx->ir.size() == 1u);
        assert(sc.ctx->ir.at(0).kind  == crot_kinds[k]);
        assert(sc.ctx->ir.at(0).n     == 2u);
        assert(sc.ctx->ir.at(0).param == theta);
        std::printf("  APPEND %s single entry: PASS\n", names[k]);
    }
}

// ── Test 7: CRx/CRy/CRz under SIMULATE — decomposed (verified vs reference) ──
//
// Reuse the same reference unitaries used by test_exec_simulate_crot.cpp.
// Here we call sturm_execute_gate (the C ABI) instead of exec_simulate_crot
// directly — verifying the full dispatch chain.

static void apply_crx_ref(cx* out, const cx* in, double theta) {
    double c = std::cos(theta / 2.0), s = std::sin(theta / 2.0);
    out[0] = in[0]; out[2] = in[2];
    out[1] = cx{c, 0.0} * in[1] + cx{0.0, -s} * in[3];
    out[3] = cx{0.0, -s} * in[1] + cx{c, 0.0} * in[3];
}

static void apply_cry_ref(cx* out, const cx* in, double theta) {
    double c = std::cos(theta / 2.0), s = std::sin(theta / 2.0);
    out[0] = in[0]; out[2] = in[2];
    out[1] = cx{c,  0.0} * in[1] + cx{-s, 0.0} * in[3];
    out[3] = cx{s,  0.0} * in[1] + cx{c,  0.0} * in[3];
}

static void apply_crz_ref(cx* out, const cx* in, double theta) {
    out[0] = in[0]; out[2] = in[2];
    out[1] = std::exp(cx{0.0, -theta / 2.0}) * in[1];
    out[3] = std::exp(cx{0.0, +theta / 2.0}) * in[3];
}

static void test_crot_simulate_via_execute_gate(
        sturm_gate_kind_t kind,
        void (*ref_fn)(cx*, const cx*, double),
        const char* name) {
    const double angles[] = {M_PI / 8.0, M_PI / 4.0, M_PI / 2.0, M_PI};
    const uint32_t ctrl = 0u, tgt = 1u;

    for (double theta : angles) {
        for (uint64_t input_idx = 0; input_idx < 4; ++input_idx) {
            // Build reference input vector
            cx in_amp[4] = {};
            in_amp[input_idx] = cx{1.0, 0.0};

            // Compute reference output
            cx expected[4];
            ref_fn(expected, in_amp, theta);

            // Run via C ABI execute_gate
            sturm::OrkanBridge bridge;
            bridge.allocate(2u);

            sturm_backend_context_t* ctx = sturm_backend_create(STURM_MODE_SIMULATE, 17u);
            assert(ctx);
            ctx->orkan_state_ptr = &bridge;

            sturm_backend_context_t* prev = sturm_get_thread_context();
            sturm_set_thread_context(ctx);

            // Set initial state
            if ((input_idx >> 0) & 1u) orkan::apply_x(bridge.state(), ctrl);
            if ((input_idx >> 1) & 1u) orkan::apply_x(bridge.state(), tgt);

            const uint32_t q[2] = {ctrl, tgt};
            sturm_execute_gate(kind, q, 2u, theta);

            // Compare amplitudes
            char label[128];
            for (uint64_t idx = 0; idx < 4; ++idx) {
                std::snprintf(label, sizeof(label),
                              "SIM %s theta=%.4f input=%llu amp[%llu]",
                              name, theta,
                              (unsigned long long)input_idx,
                              (unsigned long long)idx);
                assert_close(orkan::amplitude(bridge.state(), idx), expected[idx], label);
            }

            sturm_set_thread_context(prev);
            sturm_backend_destroy(ctx);
        }
    }
    std::printf("  SIMULATE %s decomposed: PASS\n", name);
}

static void test_crot_simulate() {
    test_crot_simulate_via_execute_gate(STURM_GATE_CRX, apply_crx_ref, "CRx");
    test_crot_simulate_via_execute_gate(STURM_GATE_CRY, apply_cry_ref, "CRy");
    test_crot_simulate_via_execute_gate(STURM_GATE_CRZ, apply_crz_ref, "CRz");
}

// ── Test 8: COUNT_ONLY does not touch ctx.ir ──────────────────────────────────

static void test_count_only_no_ir() {
    ScopedCtx sc{STURM_MODE_COUNT_ONLY};
    const uint32_t q[1] = {0u};
    for (int i = 0; i < 10; ++i) {
        sturm_execute_gate(STURM_GATE_X, q, 1u, 0.0);
    }
    assert(sc.ctx->ir.size() == 0u && "COUNT_ONLY must not write to ir");
    std::printf("  COUNT_ONLY no IR: PASS\n");
}

// ── Runner ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("M13 execute_gate tests:\n");
    test_count_only_counter();
    test_append_counter();
    test_append_records_n_entries();
    test_simulate_counter();
    test_simulate_h_then_x();
    test_crot_append_single_entry();
    test_crot_simulate();
    test_count_only_no_ir();
    std::printf("All M13 execute_gate tests passed.\n");
    return 0;
}
