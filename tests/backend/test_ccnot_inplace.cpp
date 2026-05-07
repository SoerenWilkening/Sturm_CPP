// test_ccnot_inplace.cpp — Phase J PJ-1b (sturm-8cxd): forward helper
// ccnot_inplace(qbool& x, const qbool& a, const qbool& b) coverage.
//
// The fusion peephole emitted by PJ-1d rewrites the adjacent pair
//   qbool __t = a & b;  x ^= __t;
// into a single call
//   ccnot_inplace(x, a, b);
// saving one ancilla and collapsing the forward + uncompute CCX pair
// into exactly one CCX against the active sink. The helper is
// self-adjoint: invoking `ccnot_inplace(x, a, b)` a second time undoes
// the first invocation (CCX is its own inverse), so the same symbol
// serves as both forward emission (QOpKind::CCNOT_INPLACE render) and
// uncompute (PJ-1c's uncompute_pass case).
//
// Acceptance checks:
//   1. Declared in sturm::, visible through the public umbrella header.
//   2. Emits exactly one STURM_GATE_CCX record into the gate stream with
//      qubit arguments (a, b, x) — matching primitive_AND(ctx, a, b, x).
//   3. Self-adjoint: a second call cancels the first on the live
//      statevector (round-trip identity on x), with a and b unchanged.
//   4. Quantum-control truth table: for every (a_bit, b_bit, x_initial)
//      basis input, ccnot_inplace flips x iff a AND b == 1.
//
// Harness: plain assert + printf (no gtest).

#define STURM_BACKEND_ENABLED 1

#include "sturm/uncompute/uncompute_api.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/backend/ir.hpp"
#include "sturm/backend/orkan_bridge.hpp"
#include "sturm/qtypes/qbool.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

// ── Scoped APPEND context fixture ────────────────────────────────────────────

struct ScopedAppendCtx {
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    ScopedAppendCtx() {
        ctx  = sturm_backend_create(STURM_MODE_APPEND);
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

// ── Scoped SIMULATE context fixture ──────────────────────────────────────────

struct ScopedSimulateCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit ScopedSimulateCtx(uint32_t n_qubits, uint32_t max_q = 17u) {
        bridge.allocate(n_qubits);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE);
        assert(ctx && "sturm_backend_create failed");
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~ScopedSimulateCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    orkan::state_t& sv() { return bridge.state(); }
};

// ── Helpers ──────────────────────────────────────────────────────────────────

static sturm::qbool make_quantum_qbool(int idx) {
    sturm::qbool q = sturm::qbool::make_non_owning(idx);
    q.super_mask   = 1ULL;
    q.value        = 0;
    return q;
}

static uint32_t read_bit(orkan::state_t& sv, uint32_t qubit) {
    uint64_t dim = uint64_t{1} << sv.n_qubits;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            return static_cast<uint32_t>((s >> qubit) & 1u);
        }
    }
    return 0u;
}

// ── APPEND-mode: exactly one CCX(a, b, x) ────────────────────────────────────
//
// The forward helper must emit precisely one STURM_GATE_CCX with qubit
// arguments in the order primitive_AND uses: (ctrl0=a, ctrl1=b, target=x).
static void emits_one_ccx_with_a_b_x_ordering() {
    ScopedAppendCtx sc;
    sturm::qbool a = make_quantum_qbool(10);
    sturm::qbool b = make_quantum_qbool(11);
    sturm::qbool x = make_quantum_qbool(12);

    const std::size_t ir_before = sc.ir().size();
    sturm::ccnot_inplace(x, a, b);
    const std::size_t ir_after = sc.ir().size();

    assert(ir_after - ir_before == 1u
           && "ccnot_inplace must emit exactly one gate");
    const sturm::GateRecord& g = sc.ir().at(ir_before);
    assert(g.kind == STURM_GATE_CCX
           && "ccnot_inplace must emit a single CCX");
    assert(g.qubits[0] == 10u && g.qubits[1] == 11u && g.qubits[2] == 12u
           && "CCX ordering must be (a, b, x) matching primitive_AND");
    std::printf("  emits_one_ccx_with_a_b_x_ordering: PASS\n");
}

// ── Self-adjoint: two calls cancel (round-trip identity on x) ────────────────
//
// Same symbol serves forward and uncompute. Running ccnot_inplace twice
// on the live statevector must restore x to its initial state for every
// basis input on (a, b) with probability 1, and leave a, b unchanged.
static void self_adjoint_roundtrip_identity_on_x() {
    const uint32_t qa = 0u, qb = 1u, qx = 2u;
    for (uint32_t a_bit = 0; a_bit < 2; ++a_bit) {
        for (uint32_t b_bit = 0; b_bit < 2; ++b_bit) {
            for (uint32_t x_initial = 0; x_initial < 2; ++x_initial) {
                ScopedSimulateCtx sc{3u};
                if (a_bit)     orkan::apply_x(sc.sv(), qa);
                if (b_bit)     orkan::apply_x(sc.sv(), qb);
                if (x_initial) orkan::apply_x(sc.sv(), qx);

                sturm::qbool a = make_quantum_qbool(static_cast<int>(qa));
                sturm::qbool b = make_quantum_qbool(static_cast<int>(qb));
                sturm::qbool x = make_quantum_qbool(static_cast<int>(qx));

                // Forward: x flips iff (a_bit AND b_bit) == 1.
                sturm::ccnot_inplace(x, a, b);
                const uint32_t expected_after_forward =
                    x_initial ^ (a_bit & b_bit);
                assert(read_bit(sc.sv(), qx) == expected_after_forward
                       && "ccnot_inplace must flip x iff (a AND b) == 1");

                // Self-adjoint: same symbol undoes the forward flip.
                sturm::ccnot_inplace(x, a, b);
                assert(read_bit(sc.sv(), qx) == x_initial
                       && "second ccnot_inplace must restore x");
                assert(read_bit(sc.sv(), qa) == a_bit
                       && "a unchanged after round-trip");
                assert(read_bit(sc.sv(), qb) == b_bit
                       && "b unchanged after round-trip");
            }
        }
    }
    std::printf("  self_adjoint_roundtrip_identity_on_x: PASS\n");
}

// ── Single-call truth table under SIMULATE ───────────────────────────────────
//
// Exercises the CCX semantics of the single forward emission: x_new =
// x_initial XOR (a_bit AND b_bit). Covers all 8 basis inputs.
static void single_call_truth_table() {
    const uint32_t qa = 0u, qb = 1u, qx = 2u;
    for (uint32_t a_bit = 0; a_bit < 2; ++a_bit) {
        for (uint32_t b_bit = 0; b_bit < 2; ++b_bit) {
            for (uint32_t x_initial = 0; x_initial < 2; ++x_initial) {
                ScopedSimulateCtx sc{3u};
                if (a_bit)     orkan::apply_x(sc.sv(), qa);
                if (b_bit)     orkan::apply_x(sc.sv(), qb);
                if (x_initial) orkan::apply_x(sc.sv(), qx);

                sturm::qbool a = make_quantum_qbool(static_cast<int>(qa));
                sturm::qbool b = make_quantum_qbool(static_cast<int>(qb));
                sturm::qbool x = make_quantum_qbool(static_cast<int>(qx));

                sturm::ccnot_inplace(x, a, b);

                const uint32_t expected_x = x_initial ^ (a_bit & b_bit);
                assert(read_bit(sc.sv(), qx) == expected_x
                       && "ccnot_inplace truth: x = x XOR (a AND b)");
                assert(read_bit(sc.sv(), qa) == a_bit
                       && "ccnot_inplace truth: a unchanged");
                assert(read_bit(sc.sv(), qb) == b_bit
                       && "ccnot_inplace truth: b unchanged");
            }
        }
    }
    std::printf("  single_call_truth_table: PASS\n");
}

// ── Umbrella include visibility ──────────────────────────────────────────────
//
// The transpiler emits `ccnot_inplace(x, a, b);` into user-facing output;
// that call site must resolve via the public umbrella include the way
// every other uncompute_api helper does.
#include "sturm/sturm.hpp"
static void umbrella_include_visibility() {
    ScopedAppendCtx sc;
    sturm::qbool a = make_quantum_qbool(100);
    sturm::qbool b = make_quantum_qbool(101);
    sturm::qbool x = make_quantum_qbool(102);
    sturm::ccnot_inplace(x, a, b);  // must compile via the umbrella include.
    (void)sc;
    std::printf("  umbrella_include_visibility: PASS\n");
}

// ── main ────────────────────────────────────────────────────────────────────

int main() {
    std::printf("Phase J PJ-1b ccnot_inplace forward helper tests:\n");
    emits_one_ccx_with_a_b_x_ordering();
    single_call_truth_table();
    self_adjoint_roundtrip_identity_on_x();
    umbrella_include_visibility();
    std::printf("All ccnot_inplace tests passed.\n");
    return 0;
}
