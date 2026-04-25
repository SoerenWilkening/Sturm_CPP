// test_divide_oop_adj.cpp -- sturm-nwwu: divide_oop / divide_oop_adj wrapper
// roundtrip tests.
//
// Contract: divide_oop(a, b, q, r) emits the non-restoring division network via
// lib_div_dsl when at least one operand carries allocated qubits; divide_oop_adj
// is the structural inverse — it runs __lib_div_dsl_adj on the post-swap-undo
// state where (q, r) satisfy the divide invariant (a == q*b + r) and returns
// q and r both to |0…0> while preserving a and b.
//
// Invariants pinned by this test:
//   (1) Classical short-circuit path: both qubits[0] < 0 -- divide_oop_adj is
//       bookkeeping-only; q.value, r.value, q.super_mask, r.super_mask zero.
//   (2) Gate path: divide_oop allocates fresh qubits for q and r; divide_oop_adj
//       calls __lib_div_dsl_adj which returns q and r registers to |0…0> in
//       the simulator while a and b are preserved.
//   (3) `sturm::invert<&sturm::detail::divide_oop<W>>()` resolves to
//       divide_oop_adj<W> at compile time via STURM_REGISTER_ADJOINT (must be
//       registered for the LO-2 emitter's `invert<...>()(...)` cleanup line).

#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint.hpp"
#include "sturm/qtypes/divide_oop.hpp"
#include "sturm/qtypes/lossy_oop.hpp"
#include "sturm/routines/invert.hpp"
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

struct SimCtx {
    sturm::OrkanBridge bridge;
    sturm_backend_context_t* ctx;
    sturm_backend_context_t* prev;
    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 128u) {
        bridge.allocate(n_qubits);
        ctx  = sturm_backend_create(STURM_MODE_SIMULATE, max_q);
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

static uint32_t read_reg_idxs(orkan::state_t& sv, const int* qidx,
                              uint32_t n, uint32_t n_total) {
    uint64_t dim = uint64_t{1} << n_total;
    for (uint64_t s = 0; s < dim; ++s) {
        if (std::norm(orkan::amplitude(sv, s)) > kTol) {
            uint32_t val = 0u;
            for (uint32_t k = 0; k < n; ++k) {
                if (qidx[k] >= 0) {
                    val |= (static_cast<uint32_t>((s >> qidx[k]) & 1u) << k);
                }
            }
            return val;
        }
    }
    return 0u;
}

template <std::size_t W>
static void run_divide_gate_path_roundtrip(uint32_t a_val, uint32_t b_val) {
    // Layout: a in [0..W), b in [W..2W), q + r allocated by divide_oop. Use a
    // fixed Orkan budget large enough for the W=2 case the test exercises plus
    // headroom (lib_div_dsl allocates n + (n-1) + 2 = 5 ancillas internally).
    static constexpr uint32_t n_orkan = 17u;
    const uint32_t a_base = 0u, b_base = W, n_reg = 2u * W;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();

    SimCtx sc{n_orkan, 128u};
    for (uint32_t i = 0; i < W; ++i) {
        if ((a_val >> i) & 1u) orkan::apply_x(sc.sv(), a_base + i);
        if ((b_val >> i) & 1u) orkan::apply_x(sc.sv(), b_base + i);
    }

    {
        sturm::qint_t<W> a, b;
        for (uint32_t i = 0; i < W; ++i) {
            a.qubits[i] = static_cast<int>(a_base + i);
            b.qubits[i] = static_cast<int>(b_base + i);
        }
        a.value = static_cast<int64_t>(a_val);
        b.value = static_cast<int64_t>(b_val);
        a.super_mask = b.super_mask = (1ULL << W) - 1ULL;
        a.owning_ = false; b.owning_ = false;

        sturm::qint_t<W> q, r;
        sturm::detail::divide_oop(a, b, q, r);

        // q, r must have their own freshly-allocated qubits (gate path).
        for (uint32_t i = 0; i < W; ++i) {
            assert(q.qubits[i] >= 0 && "q bit allocated on gate path");
            assert(r.qubits[i] >= 0 && "r bit allocated on gate path");
        }

        const uint32_t expect_q = (b_val != 0u) ? (a_val / b_val) : 0u;
        const uint32_t expect_r = (b_val != 0u) ? (a_val % b_val) : 0u;
        uint32_t q_sv = read_reg_idxs(sc.sv(), q.qubits.data(), W, n_orkan);
        uint32_t r_sv = read_reg_idxs(sc.sv(), r.qubits.data(), W, n_orkan);
        uint32_t a_sv = read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan);
        uint32_t b_sv = read_reg_idxs(sc.sv(), b.qubits.data(), W, n_orkan);
        assert(q_sv == expect_q && "forward: q register == a / b");
        assert(r_sv == expect_r && "forward: r register == a % b");
        assert(a_sv == a_val && "forward: a preserved");
        assert(b_sv == b_val && "forward: b preserved");

        // Adjoint roundtrip: post-swap-undo state has (q, r) satisfying
        // a == q*b + r. Running divide_oop_adj returns q and r to |0…0>.
        sturm::detail::divide_oop_adj(a, b, q, r);
        uint32_t q_after = read_reg_idxs(sc.sv(), q.qubits.data(), W, n_orkan);
        uint32_t r_after = read_reg_idxs(sc.sv(), r.qubits.data(), W, n_orkan);
        assert(q_after == 0u && "adjoint: q register returned to |0…0>");
        assert(r_after == 0u && "adjoint: r register returned to |0…0>");

        // a, b still preserved.
        assert(read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan) == a_val
               && "a preserved after adjoint");
        assert(read_reg_idxs(sc.sv(), b.qubits.data(), W, n_orkan) == b_val
               && "b preserved after adjoint");

        // Detach borrowed views before destruction.
        a.qubits.fill(-1); b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i)
        sturm::QubitPool::instance().release(reserved[i]);
}

template <std::size_t W>
static void run_classical_short_circuit(int64_t a_val, int64_t b_val) {
    sturm::QubitPool::instance().reset_for_testing();
    sturm::qint_t<W> a, b, q, r;
    a.value = a_val; b.value = b_val;
    // Both qubits arrays remain at -1 (default) -- classical short-circuit.
    sturm::detail::divide_oop(a, b, q, r);
    // q.value, r.value match the classical divide bookkeeping.
    if (b_val != 0) {
        assert(q.value == a_val / b_val);
        assert(r.value == a_val % b_val);
    } else {
        assert(q.value == 0);
        assert(r.value == 0);
    }

    sturm::detail::divide_oop_adj(a, b, q, r);
    assert(q.value == 0);
    assert(r.value == 0);
    assert(q.super_mask == 0);
    assert(r.super_mask == 0);

    // Detach borrowed views before destruction (q, r own their qubits even
    // on the classical bookkeeping path; clear the indices to avoid double
    // pool release across resets in adjacent test cases).
    q.qubits.fill(-1); r.qubits.fill(-1);
}

// invert<&divide_oop<W>>() must resolve to divide_oop_adj<W> at compile time.
static void test_invert_resolution() {
    constexpr auto adj_ptr =
        sturm::invert<&sturm::detail::divide_oop<2>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&divide_oop<W>>() must resolve to a registered adjoint");
    static_assert(adj_ptr == &sturm::detail::divide_oop_adj<2>,
                  "divide_oop_adj must be the registered structural inverse");
    std::puts("  PASS: test_divide_oop_invert_resolves_to_adj");
}

int main() {
    std::printf("sturm-nwwu LO-2: divide_oop / divide_oop_adj wrapper roundtrip:\n");

    test_invert_resolution();

    // Classical short-circuit path: divide_oop_adj is bookkeeping-only.
    run_classical_short_circuit<8>(7, 3);
    run_classical_short_circuit<8>(0, 1);
    run_classical_short_circuit<2>(3, 2);
    std::puts("  PASS: test_divide_oop_adj_classical_short_circuit");

    // Gate path roundtrip: divide_oop forward, divide_oop_adj reverse — q/r
    // back to |0…0>; a/b preserved. Mirror test_div_mod_dsl_adjoint coverage.
    run_divide_gate_path_roundtrip<2>(3u, 2u);
    std::puts("  PASS: divide_oop_adj 3/2=1 rem 1 round-trip");
    run_divide_gate_path_roundtrip<2>(2u, 1u);
    std::puts("  PASS: divide_oop_adj 2/1=2 rem 0 round-trip");
    run_divide_gate_path_roundtrip<2>(0u, 1u);
    std::puts("  PASS: divide_oop_adj 0/1=0 rem 0 round-trip");
    run_divide_gate_path_roundtrip<2>(3u, 3u);
    std::puts("  PASS: divide_oop_adj 3/3=1 rem 0 round-trip");
    run_divide_gate_path_roundtrip<2>(1u, 2u);
    std::puts("  PASS: divide_oop_adj 1/2=0 rem 1 round-trip");

    std::printf("All sturm-nwwu tests passed.\n");
    return 0;
}
