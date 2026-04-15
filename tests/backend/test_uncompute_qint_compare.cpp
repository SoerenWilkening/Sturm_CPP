// test_uncompute_qint_compare.cpp — sturm-999i Phase D: representative unit
// test for the 6 `uncompute_*_qint(qbool&, const qint_t<W>&, const qint_t<W>&)`
// free functions added to include/sturm/uncompute/uncompute_api.hpp.
//
// The issue acceptance criterion is one representative case: calling
// `uncompute_eq_qint(r, a, b)` after the forward `r = (a == b)` must XOR the
// result qubit `r` back to |0> (the DSL `lib_eq_dsl` is self-uncomputing under
// the Bennett discipline, so invoking it twice flips `r` twice).
//
// Approach: SIMULATE mode, known basis state for a and b, run the forward
// qint_t::operator== (populates r), then immediately call uncompute_eq_qint
// in the same scope (a and b still live, byte-identical). Read the result
// qubit from the statevector and assert P(r = |0>) = 1. The `a` and `b`
// registers must be untouched.
//
// Qubit layout mirrors tests/backend/test_qint_compare_simulate.cpp so the
// ancilla budget of lib_eq_dsl (≤ W extra qubits) fits inside the 17-qubit
// cap without Orkan growth.
//
// Harness: plain assert + printf (no gtest).

#define STURM_BACKEND_ENABLED 1

#include "sturm/uncompute/uncompute_api.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

static constexpr double kTol = 1e-9;

// ── SimCtx: SIMULATE mode with OrkanBridge ────────────────────────────────────

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;

    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 128u) {
        bridge.allocate(n_qubits);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx && "sturm_backend_create failed");
        ctx->orkan_state_ptr = &bridge;
        prev = sturm_get_thread_context();
        sturm_set_thread_context(ctx);
    }
    ~SimCtx() {
        sturm_set_thread_context(prev);
        sturm_backend_destroy(ctx);
    }
    orkan::state_t& sv() { return bridge.state(); }
};

// ── Read an n-qubit contiguous register from the statevector ─────────────────
static uint32_t read_reg(orkan::state_t& sv, uint32_t base_q, uint32_t n,
                          uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t val = 0u;
            for (uint32_t k = 0; k < n; ++k) {
                val |= (static_cast<uint32_t>((s >> (base_q + k)) & 1u) << k);
            }
            return val;
        }
    }
    return 0u;
}

// ── Sum probability that qubit `q` is |0> across all basis states ────────────
static double probability_zero(orkan::state_t& sv, uint32_t qubit) {
    uint64_t dim = uint64_t{1} << sv.n_qubits;
    uint64_t mask = uint64_t{1} << qubit;
    double p0 = 0.0;
    for (uint64_t s = 0; s < dim; ++s) {
        if ((s & mask) == 0u) {
            p0 += std::norm(orkan::amplitude(sv, s));
        }
    }
    return p0;
}

// ═══════════════════════════════════════════════════════════════════════════════
// test_uncompute_eq_qint_roundtrip
//
// Forward: r = (a == b) with a=4, b=4 → r=|1>.
// Inverse: uncompute_eq_qint(r, a, b) must XOR r back to |0> while leaving
// a and b byte-identical in the statevector.
// ═══════════════════════════════════════════════════════════════════════════════

static void test_uncompute_eq_qint_roundtrip() {
    static constexpr std::size_t W       = 3u;
    static constexpr uint32_t    A_BASE  = 0u;
    static constexpr uint32_t    B_BASE  = W;          // 3
    static constexpr uint32_t    N_REG   = 2u * W;     // 6 register qubits
    static constexpr uint32_t    N_ORKAN = 17u;

    sturm::QubitPool::instance().reset_for_testing();

    // Pre-reserve 2*W register qubits so the result qubit lands at index N_REG.
    int reserved[N_REG];
    for (uint32_t i = 0; i < N_REG; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();
    assert(reserved[0] == 0 && "pool must start at 0");

    SimCtx sc{N_ORKAN, 128u};

    // Initialise: a = b = 4 (binary 100) → equality should hold.
    const uint32_t a_val = 4u;
    const uint32_t b_val = 4u;
    for (uint32_t i = 0; i < W; ++i) {
        if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), A_BASE + i);
        if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), B_BASE + i);
    }

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(A_BASE + i);
            b.qubits[i] = static_cast<int>(B_BASE + i);
        }
        a.value      = static_cast<int64_t>(a_val);
        a.super_mask = (1u << W) - 1u;
        b.value      = static_cast<int64_t>(b_val);
        b.super_mask = (1u << W) - 1u;

        // Forward comparison: r picks up a freshly-allocated result qubit and
        // lib_eq_dsl flips it once (a == b is true → r = |1>).
        sturm::qbool r = (a == b);

        const int result_q = r.qubits[0];
        assert(result_q >= 0
               && "forward ==: result qubit must be allocated from the pool");

        // Confirm the forward op did set r to |1>.
        const double p0_fwd = probability_zero(sc.sv(),
                                               static_cast<uint32_t>(result_q));
        assert(std::abs(p0_fwd - 0.0) < kTol
               && "forward a==b (4==4): r must be |1>");

        // Now invoke the Phase D inverse: a second lib_eq_dsl XORs r back.
        sturm::uncompute_eq_qint<W>(r, a, b);

        // Ancilla qubit held by r must be back to |0> with probability 1.
        const double p0_inv = probability_zero(sc.sv(),
                                               static_cast<uint32_t>(result_q));
        assert(std::abs(p0_inv - 1.0) < kTol
               && "uncompute_eq_qint must return the ancilla to |0>");

        // a and b registers must be byte-identical to the pre-forward state.
        const uint32_t a_sv = read_reg(sc.sv(), A_BASE, W, N_ORKAN);
        const uint32_t b_sv = read_reg(sc.sv(), B_BASE, W, N_ORKAN);
        assert(a_sv == a_val
               && "uncompute_eq_qint must not touch a's register");
        assert(b_sv == b_val
               && "uncompute_eq_qint must not touch b's register");

        // Suppress the COMPARE destructor stamp so the qbool's RAII path does
        // not double-emit a compare circuit on teardown (matches the pattern
        // in tests/backend/test_qint_compare_simulate.cpp).
        r.super_mask = 0;

        // Prevent double-release of the reserved register qubits.
        a.qubits.fill(-1);
        b.qubits.fill(-1);
    } // r destructs: pool release runs; compare tag suppressed.

    // Release the pre-reserved register qubits.
    for (uint32_t i = 0; i < N_REG; ++i)
        sturm::QubitPool::instance().release(reserved[i]);

    std::printf("  PASS: uncompute_eq_qint returns r to |0> after forward ==\n");
}

// ── main ────────────────────────────────────────────────────────────────────
int main() {
    std::printf("sturm-999i Phase D: uncompute_*_qint (comparison) tests:\n");
    test_uncompute_eq_qint_roundtrip();
    std::printf("All uncompute qint-compare tests passed.\n");
    return 0;
}
