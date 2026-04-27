// test_and_oop.cpp -- sturm-ph6f.3: sturm::detail::and_oop reversible-network tests.
//
// Contract: and_oop(a, b, tmp) emits a Toffoli ladder (`tmp_i ^= a_i & b_i`)
// when at least one operand carries allocated qubits, and falls back to the
// pure classical update when both operands are on the qubits[0] < 0 short-
// circuit path (preserves example_qint_arith / example_phase_abc_demo).
// and_oop_adj is the structural inverse — re-applying the Toffoli ladder
// (CCX is self-inverse) returns tmp to |0…0> after the post-swap-undo state
// where tmp == a & b.
//
// Invariants pinned by this test:
//   (1) Classical short-circuit path: both qubits[0] < 0 -- bookkeeping only.
//   (2) Gate path: tmp is freshly allocated (W qubits) and the simulator
//       readout matches the classical bitwise AND.
//   (3) and_oop followed by and_oop_adj returns tmp register to |0…0>.
//   (4) `sturm::invert<&sturm::detail::and_oop<W>>()` resolves to and_oop_adj
//       at compile time via STURM_REGISTER_ADJOINT.

#define STURM_BACKEND_ENABLED 1
#include "sturm/qtypes/qint.hpp"
#include "sturm/detail/qtypes/lossy_oop.hpp"
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
    explicit SimCtx(uint32_t n_qubits, uint32_t max_q = 64u) {
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
static void run_and_gate_path(uint32_t a_val, uint32_t b_val) {
    // Layout: a in [0..W), b in [W..2W), tmp allocated by and_oop in [2W..3W).
    // Use a fixed Orkan budget large enough for the widest case the test
    // exercises (W=3 -> need 9 qubits) plus headroom; tracks STURM_MAX_QUBITS.
    static constexpr uint32_t n_orkan = 12u;
    const uint32_t a_base = 0u, b_base = W, n_reg = 2u * W;

    sturm::QubitPool::instance().reset_for_testing();

    int reserved[n_reg];
    for (uint32_t i = 0; i < n_reg; ++i)
        reserved[i] = sturm::QubitPool::instance().allocate();

    SimCtx sc{n_orkan, 64u};
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

        sturm::qint_t<W> tmp;
        sturm::detail::and_oop(a, b, tmp);

        // tmp must have its own freshly-allocated qubits (gate path).
        for (uint32_t i = 0; i < W; ++i) {
            assert(tmp.qubits[i] >= 0 && "tmp bit allocated on gate path");
            for (uint32_t j = 0; j < W; ++j) {
                assert(tmp.qubits[i] != a.qubits[j] && "tmp != a");
                assert(tmp.qubits[i] != b.qubits[j] && "tmp != b");
            }
        }

        const uint64_t mask = (1ULL << W) - 1ULL;
        const uint32_t expect = (a_val & b_val) & static_cast<uint32_t>(mask);

        assert(tmp.value == static_cast<int64_t>(expect)
               && "tmp.value bookkeeping = a & b");

        uint32_t tmp_sv = read_reg_idxs(sc.sv(), tmp.qubits.data(), W, n_orkan);
        uint32_t a_sv   = read_reg_idxs(sc.sv(), a.qubits.data(), W, n_orkan);
        uint32_t b_sv   = read_reg_idxs(sc.sv(), b.qubits.data(), W, n_orkan);
        assert(tmp_sv == expect && "tmp register == a & b in simulator");
        assert(a_sv == a_val && "a preserved");
        assert(b_sv == b_val && "b preserved");

        // Adjoint roundtrip: post-swap-undo state has tmp = a & b. Running
        // the adjoint clears tmp back to |0…0>.
        sturm::detail::and_oop_adj(a, b, tmp);
        uint32_t tmp_after = read_reg_idxs(sc.sv(), tmp.qubits.data(), W, n_orkan);
        assert(tmp_after == 0u && "adjoint zeros tmp register");

        // Operands still preserved.
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
    sturm::qint_t<W> a, b, tmp;
    a.value = a_val; b.value = b_val;
    // Both qubits arrays remain at -1 (default) -- classical short-circuit.
    sturm::detail::and_oop(a, b, tmp);
    const uint64_t mask = (1ULL << W) - 1ULL;
    assert(tmp.value == ((a_val & b_val) & static_cast<int64_t>(mask)));
    // tmp.qubits stay at -1 on the classical path (no allocation).
    for (auto q : tmp.qubits) assert(q == -1 && "tmp.qubits stay -1");

    sturm::detail::and_oop_adj(a, b, tmp);
    assert(tmp.value == 0);
    assert(tmp.super_mask == 0);
}

// invert<&and_oop<W>>() must resolve to and_oop_adj<W> at compile time.
static void test_invert_resolution() {
    constexpr auto adj_ptr =
        sturm::invert<&sturm::detail::and_oop<2>>();
    static_assert(adj_ptr != nullptr,
                  "invert<&and_oop<W>>() must resolve to a registered adjoint");
    static_assert(adj_ptr == &sturm::detail::and_oop_adj<2>,
                  "and_oop_adj must be the registered structural inverse");
    std::puts("  PASS: test_and_oop_invert_resolves_to_adj");
}

int main() {
    std::printf("sturm-ph6f.3 LO-2: and_oop reversible-network tests:\n");

    test_invert_resolution();

    // Classical short-circuit path -- must continue to work post-rewrite.
    run_classical_short_circuit<8>(6, 2);
    run_classical_short_circuit<8>(0xFF, 0x0F);
    run_classical_short_circuit<2>(0, 3);
    std::puts("  PASS: test_and_oop_classical_short_circuit");

    // Gate path -- W=2, all 16 (a,b) combinations.
    for (uint32_t a = 0; a < 4u; ++a)
        for (uint32_t b = 0; b < 4u; ++b)
            run_and_gate_path<2>(a, b);
    std::puts("  PASS: test_and_oop_gate_path_W2_full_matrix");

    // Gate path -- W=3, a couple of representative cases.
    run_and_gate_path<3>(0b101, 0b011);
    run_and_gate_path<3>(0b111, 0b111);
    run_and_gate_path<3>(0b000, 0b111);
    std::puts("  PASS: test_and_oop_gate_path_W3");

    std::printf("All sturm-ph6f.3 tests passed.\n");
    return 0;
}
