// test_divide_oop.cpp — LO-1e (sturm-a3qp): sturm::detail::divide_oop tests.
//
// Contract: divide_oop(a, b, q, r) fills fresh W-qubit registers q, r with
//   the quotient and remainder of a/b; inputs unchanged.
// Invariant: a == q * b + r  (verified via SIMULATE statevector readout).
// Matrix (all W=2 to fit the 17-qubit Orkan budget under SIMULATE):
//   3/2 = 1 rem 1 — non-zero remainder.
//   2/1 = 2 rem 0 — exact divide.
//   0/1 = 0 rem 0 — zero dividend.
//   3/3 = 1 rem 0 — equal operands.

#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint.hpp"
#include "sturm/detail/qtypes/divide_oop.hpp"
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
static void run_divide_and_check(uint32_t a_val, uint32_t b_val,
                                 uint32_t expect_q, uint32_t expect_r) {
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

        sturm::qint_t<W> q, r;
        sturm::detail::divide_oop(a, b, q, r);

        // Fresh ownership: q, r bits allocated and distinct from a, b.
        for (uint32_t i = 0; i < W; ++i) {
            assert(q.qubits[i] >= 0 && "q bit allocated");
            assert(r.qubits[i] >= 0 && "r bit allocated");
        }

        // Classical bookkeeping.
        assert(q.value == static_cast<int64_t>(expect_q));
        assert(r.value == static_cast<int64_t>(expect_r));

        // Statevector readout: q, r, a, b.
        uint32_t q_sv = read_reg_idxs(sc.sv(), q.qubits.data(), W, n_orkan);
        uint32_t r_sv = read_reg_idxs(sc.sv(), r.qubits.data(), W, n_orkan);
        uint32_t a_sv = read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan);
        uint32_t b_sv = read_reg_idxs(sc.sv(), b.qubits.data(), W, n_orkan);
        assert(q_sv == expect_q && "q register");
        assert(r_sv == expect_r && "r register");
        assert(a_sv == a_val && "a preserved");
        assert(b_sv == b_val && "b preserved");

        // Invariant: a == q*b + r.
        assert(q_sv * b_sv + r_sv == a_sv && "invariant a == q*b + r");

        // q / r disjoint qubit indices.
        for (uint32_t i = 0; i < W; ++i)
            for (uint32_t j = 0; j < W; ++j)
                assert(q.qubits[i] != r.qubits[j] && "q, r disjoint");

        a.qubits.fill(-1); b.qubits.fill(-1);
    }

    for (uint32_t i = 0; i < n_reg; ++i)
        sturm::QubitPool::instance().release(reserved[i]);
}

static void test_divide_oop_basic() {
    run_divide_and_check<2>(3u, 2u, 1u, 1u);
    std::puts("  PASS: test_divide_oop_basic (3/2 = 1 rem 1)");
}
static void test_divide_oop_exact_divide() {
    run_divide_and_check<2>(2u, 1u, 2u, 0u);
    std::puts("  PASS: test_divide_oop_exact_divide (2/1 = 2 rem 0)");
}
static void test_divide_oop_zero_dividend() {
    run_divide_and_check<2>(0u, 1u, 0u, 0u);
    std::puts("  PASS: test_divide_oop_zero_dividend (0/1 = 0 rem 0)");
}
static void test_divide_oop_equal_operands() {
    run_divide_and_check<2>(3u, 3u, 1u, 0u);
    std::puts("  PASS: test_divide_oop_equal_operands (3/3 = 1 rem 0)");
}

int main() {
    std::printf("sturm-a3qp LO-1e: divide_oop OOP division tests:\n");
    test_divide_oop_basic();
    test_divide_oop_exact_divide();
    test_divide_oop_zero_dividend();
    test_divide_oop_equal_operands();
    std::printf("All sturm-a3qp tests passed.\n");
    return 0;
}
