// test_qram_read_dsl_simulate.cpp -- sturm-2w6h.4 (Beat B2).
//
// Pins lib_qram_read_qrom_dsl statevector behaviour for the QRAM
// backend gate-emission epic (plan `docs/plan_qram_backend.md` §5
// Beat B2, goal G1).
//
// Coverage:
//   For (N, W) = (4, 4) with `a = {0xA, 0x5, 0xF, 0x0}`:
//     T1 Initialise b = |0…0⟩^W. For every classical i ∈ {0, 1, 2, 3},
//        call lib_qram_read_qrom_dsl, assert P(b == a[i]) ≈ 1.0 via
//        OrkanBridge. Reset between iterations.
//     T2 With i in (|0⟩ + |1⟩)/√2 on the LSB only (apply H to i.bit(0))
//        and other bits |0⟩, run the body and assert P(b = a[0]) ≈ 0.5
//        and P(b = a[1]) ≈ 0.5.
//     T3 i.super_mask is unchanged across each call.
//
// Qubit budget (PRD/plan §3.1): W=4 (i) + W=4 (b) + 1 (eq_k) +
// (W − 2) = 2 c_n_AND ancillas = 11. Well under orkan's 17 cap.
//
// LoC budget: <= 300 (plan §1, §5 / B2).

#define STURM_BACKEND_ENABLED 1
#include "sturm/detail/lib/qram_read_dsl.hpp"
#include "sturm/qtypes/qbool.hpp"
#include "sturm/qtypes/qint.hpp"
#include "sturm/core/context.hpp"
#include "sturm/core/core.h"
#include "sturm/core/counter_sink.hpp"
#include "sturm/core/qubit_pool.hpp"
#include "sturm/backend/orkan_bridge.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdint>

namespace {

constexpr double kTol = 1e-6;
constexpr std::size_t W = 4u;
constexpr std::size_t N = 4u;

// SIMULATE-mode context helper.
struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_q, uint32_t max_q = 128u) {
        bridge.allocate(n_q);
        ctx = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
        assert(ctx);
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

// Read the value packed in a register's qubits, summing probability
// across all basis states whose `qubits` agree with the target value.
// Returns total probability that those bits encode `target_val`.
static double prob_register_eq(orkan::state_t& sv, uint32_t n_total,
                               const int* qidx, std::size_t n_bits,
                               std::uint64_t target_val) {
    const std::uint64_t dim = std::uint64_t{1} << n_total;
    double p = 0.0;
    for (std::uint64_t s = 0; s < dim; ++s) {
        std::uint64_t got = 0u;
        for (std::size_t k = 0; k < n_bits; ++k) {
            if (qidx[k] >= 0) {
                got |= ((s >> qidx[k]) & 1u) << k;
            }
        }
        if (got == target_val) {
            const auto amp = orkan::amplitude(sv, s);
            p += std::norm(amp);
        }
    }
    return p;
}

// ── Build a QROM container with classical values. ───────────────────
static void make_container(sturm::qint_t<W>* a) {
    a[0] = sturm::qint_t<W>(0xA);
    a[1] = sturm::qint_t<W>(0x5);
    a[2] = sturm::qint_t<W>(0xF);
    a[3] = sturm::qint_t<W>(0x0);
}

// ── T1: classical i ∈ {0,1,2,3}, assert P(b == a[i]) ≈ 1.0. ────────
static void test_classical_index_sweep() {
    constexpr std::uint64_t a_vals[N] = { 0xAu, 0x5u, 0xFu, 0x0u };

    for (std::size_t iv = 0; iv < N; ++iv) {
        sturm::QubitPool::instance().reset_for_testing();

        // Pre-allocate qubits in a deterministic layout:
        //   q[0..3]  = i (W=4)
        //   q[4..7]  = b (W=4)
        //   q[8]     = eq_k
        //   q[9..10] = c_n_AND ancillas (W-2=2 for K=2 NC sandwich)
        // Pool starts handing indices from 0; we seed the indices for i and b
        // so the OrkanBridge map is known. eq_k and ancillas allocate at runtime
        // from index 8 upward.
        constexpr uint32_t n_orkan = 11u;
        int reserved[8];
        for (uint32_t r = 0; r < 8u; ++r) {
            reserved[r] = sturm::QubitPool::instance().allocate();
        }
        assert(reserved[0] == 0 && "pool deterministic layout");

        SimCtx sc{n_orkan, /*max_q=*/128u};

        // Encode classical i = iv into q[0..3] via X gates.
        for (std::size_t j = 0; j < W; ++j) {
            if ((iv >> j) & 1u) orkan::apply_x(sc.sv(), static_cast<uint32_t>(j));
        }

        sturm::qint_t<W> i_reg;
        sturm::qint_t<W> b_reg;
        for (std::size_t j = 0; j < W; ++j) {
            i_reg.qubits[j] = reserved[j];
            i_reg.super_mask |= (1ULL << j);
            b_reg.qubits[j] = reserved[W + j];
            b_reg.super_mask |= (1ULL << j);
        }
        i_reg.value = static_cast<int64_t>(iv);
        b_reg.value = 0;
        const auto i_mask_before = i_reg.super_mask;

        sturm::qint_t<W> a[N];
        make_container(a);

        sturm::lib_qram_read_qrom_dsl<W>(a, N, i_reg, b_reg);

        // T1: P(b == a[iv]) ≈ 1.0
        const double p = prob_register_eq(sc.sv(), n_orkan,
                                          b_reg.qubits.data(), W, a_vals[iv]);
        if (std::abs(p - 1.0) > kTol) {
            std::fprintf(stderr,
                "T1: iv=%zu P(b==0x%llX)=%g (expected 1.0)\n",
                iv, static_cast<unsigned long long>(a_vals[iv]), p);
        }
        assert(std::abs(p - 1.0) < kTol
               && "T1: classical i selects QROM cell deterministically");

        // T3: i.super_mask unchanged.
        assert(i_reg.super_mask == i_mask_before
               && "T3: i.super_mask unchanged across the call");

        // Cleanup — clear qubits so destructors do not double-release.
        i_reg.qubits.fill(-1);
        b_reg.qubits.fill(-1);
        i_reg.super_mask = 0;
        b_reg.super_mask = 0;
        for (uint32_t r = 0; r < 8u; ++r) {
            sturm::QubitPool::instance().release(reserved[r]);
        }
    }
}

// ── T2: superposed i = (|0⟩ + |1⟩)/√2 on LSB; expect equal split. ──
static void test_superposed_index_split() {
    constexpr std::uint64_t a_vals[N] = { 0xAu, 0x5u, 0xFu, 0x0u };

    sturm::QubitPool::instance().reset_for_testing();

    constexpr uint32_t n_orkan = 11u;
    int reserved[8];
    for (uint32_t r = 0; r < 8u; ++r) {
        reserved[r] = sturm::QubitPool::instance().allocate();
    }
    assert(reserved[0] == 0 && "pool deterministic layout");

    SimCtx sc{n_orkan, /*max_q=*/128u};

    // Apply H to i.bit(0) only — produces (|0⟩ + |1⟩)/√2.
    orkan::apply_h(sc.sv(), static_cast<uint32_t>(reserved[0]));

    sturm::qint_t<W> i_reg;
    sturm::qint_t<W> b_reg;
    for (std::size_t j = 0; j < W; ++j) {
        i_reg.qubits[j] = reserved[j];
        i_reg.super_mask |= (1ULL << j);
        b_reg.qubits[j] = reserved[W + j];
        b_reg.super_mask |= (1ULL << j);
    }
    // Track classical: i ∈ {0, 1}. The classical value tracker can be 0
    // (matches |0⟩ branch); the superposition's existence comes from
    // super_mask + the H gate already applied above.
    i_reg.value = 0;
    b_reg.value = 0;
    const auto i_mask_before = i_reg.super_mask;

    sturm::qint_t<W> a[N];
    make_container(a);

    sturm::lib_qram_read_qrom_dsl<W>(a, N, i_reg, b_reg);

    // T2: P(b = a[0]) ≈ 0.5 and P(b = a[1]) ≈ 0.5.
    const double p_a0 = prob_register_eq(sc.sv(), n_orkan,
                                         b_reg.qubits.data(), W, a_vals[0]);
    const double p_a1 = prob_register_eq(sc.sv(), n_orkan,
                                         b_reg.qubits.data(), W, a_vals[1]);
    if (std::abs(p_a0 - 0.5) > kTol) {
        std::fprintf(stderr, "T2: P(b == a[0]=0x%llX) = %g (expected 0.5)\n",
                     static_cast<unsigned long long>(a_vals[0]), p_a0);
    }
    if (std::abs(p_a1 - 0.5) > kTol) {
        std::fprintf(stderr, "T2: P(b == a[1]=0x%llX) = %g (expected 0.5)\n",
                     static_cast<unsigned long long>(a_vals[1]), p_a1);
    }
    assert(std::abs(p_a0 - 0.5) < kTol
           && "T2: superposed i yields P(b = a[0]) ≈ 0.5");
    assert(std::abs(p_a1 - 0.5) < kTol
           && "T2: superposed i yields P(b = a[1]) ≈ 0.5");

    // T3: i.super_mask unchanged.
    assert(i_reg.super_mask == i_mask_before
           && "T3: i.super_mask unchanged across the call");

    i_reg.qubits.fill(-1);
    b_reg.qubits.fill(-1);
    i_reg.super_mask = 0;
    b_reg.super_mask = 0;
    for (uint32_t r = 0; r < 8u; ++r) {
        sturm::QubitPool::instance().release(reserved[r]);
    }
}

}  // namespace

int main() {
    std::puts("sturm-2w6h.4 Beat B2: lib_qram_read_qrom_dsl simulate test:");
    test_classical_index_sweep();
    std::puts("  PASS: T1 classical i sweep (every iv selects a[iv])");
    test_superposed_index_split();
    std::puts("  PASS: T2 superposed i (|0>+|1>)/sqrt(2) splits evenly");
    std::puts("  PASS: T3 i.super_mask unchanged");
    std::puts("test_qram_read_dsl_simulate: OK");
    return 0;
}
